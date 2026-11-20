#include "GatewayMode.h"
#include "../domain/AppTypes.h"
#include "../domain/ProtocolUtils.h"
#include "../infra/Rs485Driver.h"
#include <ArduinoJson.h>
#include <Ethernet.h>
#include <MQTT.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// -----------------------
// 1. 模块私有状态
// -----------------------
static EthernetClient s_ethClient;
static WiFiClientSecure s_mqttNetClient;
static MQTTClient s_mqttClient(2048);
static unsigned long s_lastMqttReconnectAttempt = 0;
static int s_mqttRetryCount = 0;
static bool s_mqttKeepTrying = true;
static unsigned long s_lastHeartbeatMs = 0;

static IPAddress s_lastTcpTargetIp;
static uint16_t s_lastTcpTargetPort = 0;
static uint8_t s_rtuRxBuffer[512] __attribute__((aligned(4)));
static size_t s_rtuRxLength = 0;

// -----------------------
// 2. MQTT Topic 与状态负载构建
// -----------------------
static String buildMqttStatusTopic() {
  String prefix = g_mqttConfig.topicPrefix.length() > 0
                      ? g_mqttConfig.topicPrefix
                      : String(MQTT_TOPIC_PREFIX);
  String site = g_mqttConfig.siteId.length() > 0 ? g_mqttConfig.siteId
                                                 : String(MQTT_SITE_ID);
  String gateway = g_mqttConfig.gatewayId.length() > 0
                       ? g_mqttConfig.gatewayId
                       : String(MQTT_GATEWAY_ID);
  return prefix + "/" + site + "/" + gateway + "/status";
}

static String buildMqttRequestFilter() {
  String prefix = g_mqttConfig.topicPrefix.length() > 0
                      ? g_mqttConfig.topicPrefix
                      : String(MQTT_TOPIC_PREFIX);
  String site = g_mqttConfig.siteId.length() > 0 ? g_mqttConfig.siteId
                                                 : String(MQTT_SITE_ID);
  String gateway = g_mqttConfig.gatewayId.length() > 0
                       ? g_mqttConfig.gatewayId
                       : String(MQTT_GATEWAY_ID);
  return prefix + "/" + site + "/" + gateway + "/request/+";
}

static String buildMqttResponseTopic(const String &sessionId) {
  String prefix = g_mqttConfig.topicPrefix.length() > 0
                      ? g_mqttConfig.topicPrefix
                      : String(MQTT_TOPIC_PREFIX);
  String site = g_mqttConfig.siteId.length() > 0 ? g_mqttConfig.siteId
                                                 : String(MQTT_SITE_ID);
  String gateway = g_mqttConfig.gatewayId.length() > 0
                       ? g_mqttConfig.gatewayId
                       : String(MQTT_GATEWAY_ID);
  return prefix + "/" + site + "/" + gateway + "/response/" + sessionId;
}

static String buildStatusPayload() {
  StaticJsonDocument<384> doc;
  doc["status"] = "online";
  doc["version"] = FIRMWARE_VERSION;
  doc["uptime"] = millis() / 1000;

  doc["baud"] = static_cast<uint32_t>(g_rs485CurrentBaud);
  if (g_rs485CurrentConfig == SERIAL_8N1) {
    doc["parity"] = "none";
    doc["stopBits"] = 1;
  } else if (g_rs485CurrentConfig == SERIAL_8E1) {
    doc["parity"] = "even";
    doc["stopBits"] = 1;
  } else if (g_rs485CurrentConfig == SERIAL_8O1) {
    doc["parity"] = "odd";
    doc["stopBits"] = 1;
  } else if (g_rs485CurrentConfig == SERIAL_8N2) {
    doc["parity"] = "none";
    doc["stopBits"] = 2;
  } else if (g_rs485CurrentConfig == SERIAL_8E2) {
    doc["parity"] = "even";
    doc["stopBits"] = 2;
  } else if (g_rs485CurrentConfig == SERIAL_8O2) {
    doc["parity"] = "odd";
    doc["stopBits"] = 2;
  } else {
    doc["parity"] = "none";
    doc["stopBits"] = 1;
  }

  IPAddress ethIp = Ethernet.localIP();
  if (ethIp != IPAddress(0, 0, 0, 0)) doc["ethIp"] = ethIp.toString();

  if (WiFi.status() == WL_CONNECTED) {
    doc["wifiIp"] = WiFi.localIP().toString();
  } else if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
    doc["wifiIp"] = WiFi.softAPIP().toString();
  }

  String out;
  serializeJson(doc, out);
  return out;
}

// -----------------------
// 3. Modbus TCP / RTU 转发与 Ping 探测执行器
// -----------------------
static bool modbusTcpForward(JsonDocument &doc, JsonDocument &resp) {
  if (!doc.containsKey("tcpTarget") || !doc.containsKey("hex")) {
    resp["message"] = "missing_tcp_target_or_hex";
    return false;
  }

  JsonObject tcp = doc["tcpTarget"].as<JsonObject>();
  const char *ipStr = tcp["ip"] | "";
  uint16_t port = tcp["port"] | 502;

  IPAddress targetIp;
  if (!parseIpAddress(ipStr, targetIp)) {
    resp["message"] = "invalid_tcp_ip";
    return false;
  }

  const char *hex = doc["hex"] | "";
  uint8_t buffer[260];
  size_t length = 0;
  if (!hexStringToBytes(hex, buffer, sizeof(buffer), length)) {
    resp["message"] = "invalid_hex";
    return false;
  }

  bool needReconnect = false;
  if (!s_ethClient.connected()) {
    needReconnect = true;
  } else if (targetIp != s_lastTcpTargetIp || port != s_lastTcpTargetPort) {
    s_ethClient.stop();
    delay(50);
    needReconnect = true;
  }

  if (needReconnect) {
    if (!s_ethClient.connect(targetIp, port)) {
      resp["message"] = "tcp_connect_failed";
      return false;
    }
    s_lastTcpTargetIp = targetIp;
    s_lastTcpTargetPort = port;
  }

  while (s_ethClient.available()) s_ethClient.read();

  s_ethClient.write(buffer, length);
  s_ethClient.flush();

  unsigned long start = millis();
  const unsigned long timeoutMs = 2000;
  size_t offset = 0;

  while ((millis() - start) < timeoutMs) {
    while (s_ethClient.available() > 0 && offset < sizeof(buffer)) {
      int b = s_ethClient.read();
      if (b < 0) break;
      buffer[offset++] = static_cast<uint8_t>(b);
      start = millis();
    }
    if (offset > 0 && s_ethClient.available() == 0) {
      break;
    }
    delay(5);
  }

  if (offset == 0) {
    resp["message"] = "tcp_timeout_or_empty";
    return false;
  }

  g_telemetry.master2FrameCount++;
  resp["hex"] = bytesToHexString(buffer, offset);
  resp["length"] = static_cast<uint32_t>(offset);
  return true;
}

static bool modbusRtuForward(JsonDocument &doc, JsonDocument &resp) {
  if (!doc.containsKey("hex")) {
    resp["message"] = "missing_hex";
    return false;
  }

  uint32_t requestedBaud = RS485_DEFAULT_BAUDRATE;
  uint8_t stopBits = RS485_DEFAULT_STOPBITS;
  const char *parityStr = "none";

  if (doc.containsKey("rtuTarget")) {
    JsonObject rtu = doc["rtuTarget"].as<JsonObject>();
    requestedBaud = rtu["baudRate"] | RS485_DEFAULT_BAUDRATE;
    stopBits = rtu["stopBits"] | RS485_DEFAULT_STOPBITS;
    parityStr = rtu["parity"] | "none";
  }

  uint32_t baud = 0;
  uint32_t config = 0;
  if (!buildRs485Config(requestedBaud, stopBits, parityStr, baud, config)) {
    resp["message"] = "invalid_rtu_config";
    return false;
  }

  if (g_rs485Busy) {
    resp["message"] = "rtu_busy";
    return false;
  }
  g_rs485Busy = true;

  if (baud != g_rs485CurrentBaud || config != g_rs485CurrentConfig) {
    RS485Serial.flush();
    RS485Serial.end();
    delay(2);
    rs485Begin(baud, config);
    g_rs485CurrentBaud = baud;
    g_rs485CurrentConfig = config;
  }

  const char *hex = doc["hex"] | "";
  size_t length = 0;
  if (!hexStringToBytes(hex, s_rtuRxBuffer, sizeof(s_rtuRxBuffer), length)) {
    resp["message"] = "invalid_hex";
    g_rs485Busy = false;
    return false;
  }

  while (RS485Serial.available() > 0) {
    RS485Serial.read();
  }

  RS485Serial.write(s_rtuRxBuffer, length);
  RS485Serial.flush();

  s_rtuRxLength = 0;
  unsigned long start = millis();
  const unsigned long frameTimeoutMs = 50;
  const unsigned long overallTimeoutMs = 1000;
  unsigned long lastByteTime = millis();

  while ((millis() - start) < overallTimeoutMs) {
    while (RS485Serial.available() > 0 &&
           s_rtuRxLength < sizeof(s_rtuRxBuffer)) {
      int b = RS485Serial.read();
      if (b < 0) break;
      s_rtuRxBuffer[s_rtuRxLength++] = static_cast<uint8_t>(b);
      lastByteTime = millis();
    }

    if (s_rtuRxLength > 0 && (millis() - lastByteTime) > frameTimeoutMs) {
      break;
    }
    delay(2);
  }

  if (s_rtuRxLength == 0) {
    resp["message"] = "rtu_timeout_or_empty";
    g_rs485Busy = false;
    return false;
  }

  g_telemetry.lastBusLatencyMs = millis() - start;
  g_telemetry.master1FrameCount++;
  if (s_rtuRxLength >= 4) {
    uint16_t rxCrc =
        (s_rtuRxBuffer[s_rtuRxLength - 1] << 8) | s_rtuRxBuffer[s_rtuRxLength - 2];
    if (calculateCRC16(s_rtuRxBuffer, s_rtuRxLength - 2) != rxCrc) {
      g_telemetry.busCrcErrorCount++;
    }
  }
  resp["hex"] = bytesToHexString(s_rtuRxBuffer, s_rtuRxLength);
  resp["length"] = static_cast<uint32_t>(s_rtuRxLength);
  g_rs485Busy = false;
  return true;
}

static void executePingForward(JsonDocument &doc, const String &sessionId,
                               JsonDocument &resp) {
  resp["sessionId"] = sessionId;
  resp["type"] = "ping";

  if (!doc.containsKey("pingTarget")) {
    resp["success"] = false;
    resp["error"] = "missing_ping_target";
    return;
  }

  JsonObject target = doc["pingTarget"].as<JsonObject>();
  const char *ipStr = target["ip"] | "";
  uint16_t port = target["port"] | 502;
  uint16_t seq = doc["seq"] | 0;

  IPAddress targetIp;
  if (!parseIpAddress(ipStr, targetIp)) {
    resp["success"] = false;
    resp["error"] = "invalid_ip";
    resp["seq"] = seq;
    return;
  }

  resp["ip"] = ipStr;
  resp["port"] = port;
  resp["seq"] = seq;

  IPAddress localIp = Ethernet.localIP();
  resp["localIp"] = localIp.toString();
  auto link = Ethernet.linkStatus();
  resp["link"] = (link == LinkON)    ? "up"
                 : (link == LinkOFF) ? "down"
                                     : "unknown";

  if (link == LinkOFF) {
    resp["success"] = false;
    resp["error"] = "eth_link_down";
    return;
  }

  EthernetClient pingClient;
  unsigned long startMs = millis();
  bool connected = pingClient.connect(targetIp, port);
  unsigned long latency = millis() - startMs;

  if (connected) {
    pingClient.stop();
    resp["success"] = true;
    resp["latency"] = static_cast<uint32_t>(latency);
  } else {
    resp["success"] = false;
    resp["latency"] = static_cast<uint32_t>(latency);
    resp["error"] =
        (latency >= 1000) ? "timeout_unreachable" : "connection_refused";
  }
}

// -----------------------
// 4. MQTT 消息路由与连接保活
// -----------------------
static void publishResponse(const String &sessionId, const JsonDocument &resp) {
  String out;
  serializeJson(resp, out);
  s_mqttClient.publish(buildMqttResponseTopic(sessionId).c_str(), out.c_str(),
                       false, 1);
}

static void handleMqttRequestMessage(const String &topic,
                                     const uint8_t *payload,
                                     unsigned int length) {
  String sessionId;
  int idx = topic.lastIndexOf('/');
  if (idx >= 0 && idx + 1 < static_cast<int>(topic.length())) {
    sessionId = topic.substring(idx + 1);
  }

  DynamicJsonDocument doc(MQTT_JSON_DOC_SIZE);
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err != DeserializationError::Ok) {
    StaticJsonDocument<256> resp;
    resp["sessionId"] = sessionId;
    resp["success"] = false;
    resp["error"] = String("json_error:") + err.c_str();
    publishResponse(sessionId, resp);
    return;
  }

  const char *transport = doc["transport"] | "";

  if (strcmp(transport, "ping") == 0) {
    StaticJsonDocument<512> resp;
    executePingForward(doc, sessionId, resp);
    publishResponse(sessionId, resp);
    return;
  }

  const char *payloadHex = doc["payloadHex"] | "";
  if (!transport[0] || !payloadHex[0]) {
    StaticJsonDocument<256> resp;
    resp["sessionId"] = sessionId;
    resp["success"] = false;
    resp["error"] = "missing_transport_or_payloadHex";
    publishResponse(sessionId, resp);
    return;
  }

  doc["hex"] = payloadHex;

  DynamicJsonDocument modbusResp(512);
  bool ok = false;

  if (strcmp(transport, "tcp") == 0) {
    ok = modbusTcpForward(doc, modbusResp);
  } else if (strcmp(transport, "rtu") == 0) {
    ok = modbusRtuForward(doc, modbusResp);
  } else {
    StaticJsonDocument<256> resp;
    resp["sessionId"] = sessionId;
    resp["success"] = false;
    resp["error"] = "unsupported_transport";
    publishResponse(sessionId, resp);
    return;
  }

  StaticJsonDocument<512> resp;
  resp["sessionId"] = sessionId;
  resp["success"] = ok;

  if (ok) {
    const char *outHex = modbusResp["hex"] | "";
    resp["payloadHex"] = outHex;
  } else {
    const char *msg = modbusResp["message"] | "modbus_forward_failed";
    resp["error"] = msg;
  }

  publishResponse(sessionId, resp);
}

static void mqttMessageReceived(String &topic, String &payload) {
  handleMqttRequestMessage(
      topic, reinterpret_cast<const uint8_t *>(payload.c_str()),
      payload.length());
}

static bool ensureMqttConnected() {
  if (s_mqttClient.connected()) {
    s_mqttRetryCount = 0;
    return true;
  }

  if (!s_mqttKeepTrying || WiFi.status() != WL_CONNECTED) return false;

  unsigned long now = millis();
  if (now - s_lastMqttReconnectAttempt < 5000) return false;
  s_lastMqttReconnectAttempt = now;

  String clientId =
      "anyport-esp32-" + String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX);
  String statusTopic = buildMqttStatusTopic();

  StaticJsonDocument<128> willDoc;
  willDoc["status"] = "offline";
  willDoc["version"] = FIRMWARE_VERSION;
  String willPayload;
  serializeJson(willDoc, willPayload);

  s_mqttClient.setWill(statusTopic.c_str(), willPayload.c_str(), true, 1);
  s_mqttClient.setOptions(60, true, 1000);

  bool ok = false;
  if (g_mqttConfig.username.length() > 0) {
    ok = s_mqttClient.connect(clientId.c_str(), g_mqttConfig.username.c_str(),
                              g_mqttConfig.password.c_str());
  } else {
    ok = s_mqttClient.connect(clientId.c_str());
  }

  if (!ok) {
    s_mqttRetryCount++;
    if (s_mqttRetryCount >= 3) s_mqttKeepTrying = false;
    return false;
  }

  s_mqttRetryCount = 0;
  s_mqttKeepTrying = true;
  s_mqttClient.publish(statusTopic.c_str(), buildStatusPayload().c_str(), true,
                       1);
  s_lastHeartbeatMs = millis();
  s_mqttClient.subscribe(buildMqttRequestFilter().c_str(), 1);
  return true;
}

// -----------------------
// 5. IWorkMode 生命周期实现
// -----------------------
void GatewayMode::begin() {
  String host = g_mqttConfig.host.length() > 0 ? g_mqttConfig.host
                                               : String(MQTT_BROKER_HOST);
  uint16_t port =
      g_mqttConfig.port != 0 ? g_mqttConfig.port : MQTT_BROKER_PORT;

  s_mqttNetClient.setInsecure();
  s_mqttClient.begin(host.c_str(), port, s_mqttNetClient);
  s_mqttClient.onMessage(mqttMessageReceived);
}

void GatewayMode::loop() {
  if (ensureMqttConnected()) {
    s_mqttClient.loop();
  }

  if (s_mqttClient.connected()) {
    unsigned long now = millis();
    if (now - s_lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
      s_lastHeartbeatMs = now;
      s_mqttClient.publish(buildMqttStatusTopic().c_str(),
                           buildStatusPayload().c_str(), true, 1);
    }
  }
}
