#pragma once

#include "Globals.h"

// -----------------------
// 1. 主题构建辅助函数
// -----------------------
inline String buildMqttStatusTopic() {
    String prefix = g_mqttConfig.topicPrefix.length() > 0 ? g_mqttConfig.topicPrefix : String(MQTT_TOPIC_PREFIX);
    String site = g_mqttConfig.siteId.length() > 0 ? g_mqttConfig.siteId : String(MQTT_SITE_ID);
    String gateway = g_mqttConfig.gatewayId.length() > 0 ? g_mqttConfig.gatewayId : String(MQTT_GATEWAY_ID);
    return prefix + "/" + site + "/" + gateway + "/status";
}

inline String buildMqttRequestFilter() {
    String prefix = g_mqttConfig.topicPrefix.length() > 0 ? g_mqttConfig.topicPrefix : String(MQTT_TOPIC_PREFIX);
    String site = g_mqttConfig.siteId.length() > 0 ? g_mqttConfig.siteId : String(MQTT_SITE_ID);
    String gateway = g_mqttConfig.gatewayId.length() > 0 ? g_mqttConfig.gatewayId : String(MQTT_GATEWAY_ID);
    return prefix + "/" + site + "/" + gateway + "/request/+";
}

inline String buildMqttResponseTopic(const String &sessionId) {
    String prefix = g_mqttConfig.topicPrefix.length() > 0 ? g_mqttConfig.topicPrefix : String(MQTT_TOPIC_PREFIX);
    String site = g_mqttConfig.siteId.length() > 0 ? g_mqttConfig.siteId : String(MQTT_SITE_ID);
    String gateway = g_mqttConfig.gatewayId.length() > 0 ? g_mqttConfig.gatewayId : String(MQTT_GATEWAY_ID);
    return prefix + "/" + site + "/" + gateway + "/response/" + sessionId;
}

// -----------------------
// 2. 状态构建
// -----------------------
static String buildStatusPayload() {
    StaticJsonDocument<384> doc;
    doc["status"] = "online";
    doc["version"] = FIRMWARE_VERSION;

    time_t now = time(nullptr);
    if (now > 1000000000L) {
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        doc["time"] = buf;
    } else {
        doc["time"] = "unsynced";
    }

    doc["baud"] = (uint32_t)g_rs485CurrentBaud;
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
// 3. 消息处理与连接维护
// -----------------------
extern void handleMqttRequestMessage(const String &topic, const uint8_t *payload, unsigned int length);

static void mqttMessageReceived(String &topic, String &payload) {
    handleMqttRequestMessage(topic, (const uint8_t *)payload.c_str(), payload.length());
}

static void initMqttClient() {
    String host = g_mqttConfig.host.length() > 0 ? g_mqttConfig.host : String(MQTT_BROKER_HOST);
    uint16_t port = g_mqttConfig.port != 0 ? g_mqttConfig.port : MQTT_BROKER_PORT;
    
    g_mqttNetClient.setInsecure();
    g_mqttClient.begin(host.c_str(), port, g_mqttNetClient);
    g_mqttClient.onMessage(mqttMessageReceived);
}

static bool ensureMqttConnected() {
    if (g_mqttClient.connected()) {
        g_mqttRetryCount = 0;
        return true;
    }

    if (!g_mqttKeepTrying || WiFi.status() != WL_CONNECTED) return false;

    unsigned long now = millis();
    if (now - g_lastMqttReconnectAttempt < 5000) return false;
    g_lastMqttReconnectAttempt = now;

    String clientId = "anyport-esp32-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    String statusTopic = buildMqttStatusTopic();

    StaticJsonDocument<128> willDoc;
    willDoc["status"] = "offline";
    willDoc["version"] = FIRMWARE_VERSION;
    String willPayload;
    serializeJson(willDoc, willPayload);

    g_mqttClient.setWill(statusTopic.c_str(), willPayload.c_str(), true, 1);
    g_mqttClient.setOptions(60, true, 1000); 

    bool ok = false;
    if (g_mqttConfig.username.length() > 0) {
        ok = g_mqttClient.connect(clientId.c_str(), g_mqttConfig.username.c_str(), g_mqttConfig.password.c_str());
    } else {
        ok = g_mqttClient.connect(clientId.c_str());
    }

    if (!ok) {
        g_mqttRetryCount++;
        if (g_mqttRetryCount >= 3) g_mqttKeepTrying = false;
        return false;
    }

    g_mqttRetryCount = 0;
    g_mqttKeepTrying = true;
    g_mqttClient.publish(statusTopic.c_str(), buildStatusPayload().c_str(), true, 1);
    g_lastHeartbeatMs = millis();
    g_mqttClient.subscribe(buildMqttRequestFilter().c_str(), 1);

    return true;
}
