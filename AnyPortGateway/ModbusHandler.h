#pragma once

#include "Globals.h"

// -----------------------
// 1. 辅助工具函数
// -----------------------
static bool hexStringToBytes(const char *hex, uint8_t *buffer, size_t bufferSize, size_t &outLength) {
    size_t len = strlen(hex);
    if ((len % 2) != 0) return false;
    size_t byteCount = len / 2;
    if (byteCount > bufferSize) return false;
    for (size_t i = 0; i < byteCount; ++i) {
        char high = hex[i * 2];
        char low = hex[i * 2 + 1];
        uint8_t h = (high >= '0' && high <= '9') ? high - '0' : (high >= 'A' && high <= 'F') ? high - 'A' + 10 : (high >= 'a' && high <= 'f') ? high - 'a' + 10 : 0xFF;
        uint8_t l = (low >= '0' && low <= '9') ? low - '0' : (low >= 'A' && low <= 'F') ? low - 'A' + 10 : (low >= 'a' && low <= 'f') ? low - 'a' + 10 : 0xFF;
        if (h == 0xFF || l == 0xFF) return false;
        buffer[i] = (h << 4) | l;
    }
    outLength = byteCount;
    return true;
}

static String bytesToHexString(const uint8_t *data, size_t length) {
    String result;
    static const char *hexDigits = "0123456789ABCDEF";
    result.reserve(length * 2);
    for (size_t i = 0; i < length; ++i) {
        uint8_t b = data[i];
        result += hexDigits[b >> 4];
        result += hexDigits[b & 0x0F];
    }
    return result;
}

// -----------------------
// 2. Modbus 核心逻辑
// -----------------------

inline void initRs485Port() {
    g_rs485CurrentBaud = RS485_DEFAULT_BAUDRATE;
    g_rs485CurrentConfig = SERIAL_8N1;
    RS485Serial.begin(RS485_DEFAULT_BAUDRATE, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
    Serial.println("[RS485] hardware serial initialized");
}

inline bool buildRs485Config(uint32_t requestedBaud, uint8_t stopBits,
                             const char *parityStr, uint32_t &baudOut,
                             uint32_t &configOut) {
  if (requestedBaud < 1200 || requestedBaud > 1152000) {
    return false;
  }
  if (stopBits != 1 && stopBits != 2) {
    return false;
  }

  uint32_t config = 0;

  if (strcmp(parityStr, "none") == 0) {
    config = (stopBits == 1) ? SERIAL_8N1 : SERIAL_8N2;
  } else if (strcmp(parityStr, "even") == 0) {
    config = (stopBits == 1) ? SERIAL_8E1 : SERIAL_8E2;
  } else if (strcmp(parityStr, "odd") == 0) {
    config = (stopBits == 1) ? SERIAL_8O1 : SERIAL_8O2;
  } else {
    return false;
  }

  baudOut = requestedBaud;
  configOut = config;
  return true;
}

inline bool modbusTcpForward(JsonDocument &doc, JsonDocument &resp) {
  if (!doc.containsKey("tcpTarget") || !doc.containsKey("hex")) {
    resp["message"] = "missing_tcp_target_or_hex";
    return false;
  }

  JsonObject tcp = doc["tcpTarget"].as<JsonObject>();
  const char *ipStr = tcp["ip"] | "";
  uint16_t port = tcp["port"] | 502;

  IPAddress targetIp;
  extern bool parseIpAddress(const char *str, IPAddress &outIp);
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

  if (g_ethClient.connected()) {
    g_ethClient.stop();
  }

  if (!g_ethClient.connect(targetIp, port)) {
    resp["message"] = "tcp_connect_failed";
    return false;
  }

  g_ethClient.write(buffer, length);
  g_ethClient.flush();

  unsigned long start = millis();
  const unsigned long timeoutMs = 2000;
  size_t offset = 0;

  while ((millis() - start) < timeoutMs) {
    while (g_ethClient.available() > 0 && offset < sizeof(buffer)) {
      int b = g_ethClient.read();
      if (b < 0) {
        break;
      }
      buffer[offset++] = static_cast<uint8_t>(b);
      start = millis();
    }
    if (offset > 0 && g_ethClient.available() == 0) {
      break;
    }
    delay(5);
  }

  g_ethClient.stop();

  if (offset == 0) {
    resp["message"] = "tcp_timeout_or_empty";
    return false;
  }

  resp["hex"] = bytesToHexString(buffer, offset);
  resp["length"] = static_cast<uint32_t>(offset);
  return true;
}

inline void pingForward(JsonDocument &doc, const String &sessionId) {
  StaticJsonDocument<512> resp;
  resp["sessionId"] = sessionId;
  resp["type"] = "ping";

  if (!doc.containsKey("pingTarget")) {
    resp["success"] = false;
    resp["error"] = "missing_ping_target";
    String out;
    serializeJson(resp, out);
    extern String buildMqttResponseTopic(const String &sessionId);
    g_mqttClient.publish(buildMqttResponseTopic(sessionId).c_str(), out.c_str(), false, 1);
    return;
  }

  JsonObject target = doc["pingTarget"].as<JsonObject>();
  const char *ipStr = target["ip"] | "";
  uint16_t port = target["port"] | 502;
  uint16_t seq = doc["seq"] | 0;

  IPAddress targetIp;
  extern bool parseIpAddress(const char *str, IPAddress &outIp);
  if (!parseIpAddress(ipStr, targetIp)) {
    resp["success"] = false;
    resp["error"] = "invalid_ip";
    resp["seq"] = seq;
    String out;
    serializeJson(resp, out);
    extern String buildMqttResponseTopic(const String &sessionId);
    g_mqttClient.publish(buildMqttResponseTopic(sessionId).c_str(), out.c_str(), false, 1);
    return;
  }

  resp["ip"] = ipStr;
  resp["port"] = port;
  resp["seq"] = seq;

  // 诊断信息：W5500 本机 IP 和链路状态
  IPAddress localIp = Ethernet.localIP();
  resp["localIp"] = localIp.toString();
  auto link = Ethernet.linkStatus();
  resp["link"] = (link == LinkON)    ? "up"
                 : (link == LinkOFF) ? "down"
                                     : "unknown";

  // 链路未连接直接返回
  if (link == LinkOFF) {
    resp["success"] = false;
    resp["error"] = "eth_link_down";
    String out;
    serializeJson(resp, out);
    extern String buildMqttResponseTopic(const String &sessionId);
    g_mqttClient.publish(buildMqttResponseTopic(sessionId).c_str(), out.c_str(), false, 1);
    return;
  }

  // TCP Connect 探测
  EthernetClient pingClient;
  unsigned long startMs = millis();
  bool connected = pingClient.connect(targetIp, port);
  unsigned long latency = millis() - startMs;

  if (connected) {
    pingClient.stop();
    resp["success"] = true;
    resp["latency"] = (uint32_t)latency;
  } else {
    resp["success"] = false;
    resp["latency"] = (uint32_t)latency;
    // Ethernet 库内 connect 的默认超时就是 1000ms
    if (latency >= 1000) {
      resp["error"] = "timeout_unreachable";
    } else if (latency < 100) {
      resp["error"] = "socket_error";
    } else {
      resp["error"] = "port_refused";
    }
  }

  String out;
  serializeJson(resp, out);
  extern String buildMqttResponseTopic(const String &sessionId);
  g_mqttClient.publish(buildMqttResponseTopic(sessionId).c_str(), out.c_str(), false, 1);
}

inline bool modbusRtuForward(JsonDocument &doc, JsonDocument &resp) {
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
    RS485Serial.begin(baud, config, PIN_RS485_RX, PIN_RS485_TX);
    g_rs485CurrentBaud = baud;
    g_rs485CurrentConfig = config;
  }

  const char *hex = doc["hex"] | "";
  size_t length = 0;
  if (!hexStringToBytes(hex, g_rtuRxBuffer, sizeof(g_rtuRxBuffer), length)) {
    resp["message"] = "invalid_hex";
    g_rs485Busy = false;
    return false;
  }

  // 关键保护：强制清空串口中所有的历史数据或回显干扰垃圾
  while (RS485Serial.available() > 0) {
    RS485Serial.read();
  }

  RS485Serial.write(g_rtuRxBuffer, length);
  RS485Serial.flush();

  g_rtuRxLength = 0;
  unsigned long start = millis();
  const unsigned long frameTimeoutMs = 50;
  const unsigned long overallTimeoutMs = 1000;
  unsigned long lastByteTime = millis();

  while ((millis() - start) < overallTimeoutMs) {
    while (RS485Serial.available() > 0 &&
           g_rtuRxLength < sizeof(g_rtuRxBuffer)) {
      int b = RS485Serial.read();
      if (b < 0) {
        break;
      }
      g_rtuRxBuffer[g_rtuRxLength++] = static_cast<uint8_t>(b);
      lastByteTime = millis();
    }

    if (g_rtuRxLength > 0 && (millis() - lastByteTime) > frameTimeoutMs) {
      break;
    }

    delay(2);
  }

  if (g_rtuRxLength == 0) {
    resp["message"] = "rtu_timeout_or_empty";
    g_rs485Busy = false;
    return false;
  }

  resp["hex"] = bytesToHexString(g_rtuRxBuffer, g_rtuRxLength);
  resp["length"] = static_cast<uint32_t>(g_rtuRxLength);
  g_rs485Busy = false;
  return true;
}
