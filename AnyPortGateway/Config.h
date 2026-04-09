#pragma once

#include "Globals.h"

// -----------------------
// 1. 全局变量定义 (只能在一个地方定义)
// -----------------------
uint8_t g_macAddress[6];
HardwareSerial RS485Serial(1);
uint32_t g_rs485CurrentBaud = RS485_DEFAULT_BAUDRATE;
uint32_t g_rs485CurrentConfig = SERIAL_8N1;
volatile bool g_rs485Busy = false;
WorkMode g_workMode = WorkMode::GATEWAY;
Preferences g_prefs;
EthernetClient g_ethClient;
WiFiClientSecure g_mqttNetClient;
MQTTClient g_mqttClient(2048);
unsigned long g_lastMqttReconnectAttempt = 0;
int g_mqttRetryCount = 0;
bool g_mqttKeepTrying = true;

EthStaticConfig g_ethConfig = {};
WifiStaConfig g_wifiStaConfig = {};
MqttRuntimeConfig g_mqttConfig = {};
NtpConfig g_ntpConfig = {};
String g_mdnsName = MDNS_DEFAULT_NAME;
uint32_t g_transBaud = 9600;
uint8_t g_transDataBits = 8;
uint8_t g_transParity = 0; // 0:None, 1:Even, 2:Odd
uint8_t g_transStopBits = 1;

BridgeConfig g_bridgeConfig = {0, 0, 1, 502, "", 502, 9600, 1, 0, 8};

WebServer g_httpServer(80);
uint8_t g_rtuRxBuffer[512] __attribute__((aligned(4)));
size_t g_rtuRxLength = 0;
volatile bool g_needRestart = false;
#include <ESPmDNS.h>
unsigned long g_lastHeartbeatMs = 0;

// -----------------------
// 2. 包含模块实现 (头文件守卫和引用顺序)
// -----------------------
#include "NetworkManager.h"
#include "ModbusHandler.h"
#include "MqttHandler.h"
#include "WebHandler.h"
#include "SimulatorCore.h"
#include "SimulatorWeb.h"
#include "BridgeHandler.h"
#include "TransparentHandler.h"

// -----------------------
// 3. 顶层业务逻辑实现
// -----------------------

static void loadPersistentConfig() {
    g_prefs.begin("anyport", true);
    g_workMode = static_cast<WorkMode>(g_prefs.getUChar("workMode", 0));
    
    if (g_prefs.isKey("ethIp") && g_prefs.isKey("ethMask")) {
        uint32_t ipRaw = g_prefs.getUInt("ethIp", 0);
        uint32_t maskRaw = g_prefs.getUInt("ethMask", 0);
        if (ipRaw != 0 && maskRaw != 0) {
            g_ethConfig.ip = IPAddress(ipRaw);
            g_ethConfig.subnet = IPAddress(maskRaw);
            g_ethConfig.gateway = IPAddress(g_prefs.getUInt("ethGw", 0));
            g_ethConfig.dns = IPAddress(g_prefs.getUInt("ethDns", 0));
            g_ethConfig.valid = true;
        }
    }

    if (g_prefs.isKey("wifiSsid")) {
        g_wifiStaConfig.ssid = g_prefs.getString("wifiSsid", "");
        g_wifiStaConfig.password = g_prefs.getString("wifiPwd", "");
        g_wifiStaConfig.valid = true;
    }

    g_ntpConfig.server = g_prefs.getString("ntpServer", "ntp.aliyun.com");
    g_ntpConfig.interval = g_prefs.getUInt("ntpInterval", 3600);
    g_ntpConfig.valid = true;

    if (g_prefs.isKey("mqttHost")) {
        g_mqttConfig.host = g_prefs.getString("mqttHost", MQTT_BROKER_HOST);
        g_mqttConfig.port = g_prefs.getUShort("mqttPort", MQTT_BROKER_PORT);
        g_mqttConfig.path = g_prefs.getString("mqttPath", MQTT_BROKER_PATH);
        g_mqttConfig.username = g_prefs.getString("mqttUser", "");
        g_mqttConfig.password = g_prefs.getString("mqttPwd", "");
        g_mqttConfig.topicPrefix = g_prefs.getString("mqttPrefix", MQTT_TOPIC_PREFIX);
        g_mqttConfig.siteId = g_prefs.getString("mqttSite", MQTT_SITE_ID);
        g_mqttConfig.gatewayId = g_prefs.getString("mqttGateway", MQTT_GATEWAY_ID);
        g_mqttConfig.valid = true;
    } else {
        g_mqttConfig.host = MQTT_BROKER_HOST;
        g_mqttConfig.port = MQTT_BROKER_PORT;
        g_mqttConfig.path = MQTT_BROKER_PATH;
        g_mqttConfig.username = MQTT_USERNAME;
        g_mqttConfig.password = MQTT_PASSWORD;
        g_mqttConfig.topicPrefix = MQTT_TOPIC_PREFIX;
        g_mqttConfig.siteId = MQTT_SITE_ID;
        g_mqttConfig.gatewayId = MQTT_GATEWAY_ID;
        g_mqttConfig.valid = true;
    }

    g_mdnsName = g_prefs.getString("mdnsName", MDNS_DEFAULT_NAME);
    g_transBaud = g_prefs.getUInt("tBaud", 9600);
    g_transDataBits = g_prefs.getUChar("tData", 8);
    g_transParity = g_prefs.getUChar("tParity", 0);
    g_transStopBits = g_prefs.getUChar("tStop", 1);
    
    // Bridge 加载
    g_bridgeConfig.direction = g_prefs.getUChar("bDir", 0);
    g_bridgeConfig.bridgeMode = g_prefs.getUChar("bMode", 0);
    g_bridgeConfig.slaveId = g_prefs.getUChar("bSlaveId", 1);
    g_bridgeConfig.tcpPort = g_prefs.getUShort("bTcpPort", 502);
    g_bridgeConfig.targetIp = g_prefs.getString("bTargetIp", "192.168.1.100");
    g_bridgeConfig.targetPort = g_prefs.getUShort("bTargetPort", 502);
    g_bridgeConfig.baud = g_prefs.getUInt("bBaud", 9600);
    g_bridgeConfig.dataBits = g_prefs.getUChar("bData", 8);
    g_bridgeConfig.parity = g_prefs.getUChar("bParity", 0);
    g_bridgeConfig.stopBits = g_prefs.getUChar("bStop", 1);

    g_prefs.end();
}

void anyportHardwareInit() {
    Serial.begin(115200);
    delay(500);

    // 关键修复：必须首先加载配置，才能确定是否处于静默的透传模式
    loadPersistentConfig();

    APP_PRINTLN("\n=== AnyPort Gateway v" FIRMWARE_VERSION " Starting ===");

    initRs485Port();
    initWifi();
    initMdns();
    syncNtpTime();
    initSpiAndEthernet();
    initMqttClient();
    initHttpServer();
    initSimulatorWeb(g_httpServer);

    if (g_workMode == WorkMode::TRANSPARENT) {
        initTransparentMode();
    } else {
        APP_PRINTLN("=== Initialization Complete ===");
    }
}

void anyportGatewayLoop() {
    if (g_workMode == WorkMode::GATEWAY) {
        if (ensureMqttConnected()) {
            g_mqttClient.loop();
        }
    } else if (g_workMode == WorkMode::SIMULATOR) {
        simulatorLoop();
    } else if (g_workMode == WorkMode::TRANSPARENT) {
        loopTransparentMode();
    } else if (g_workMode == WorkMode::BRIDGE) {
        bridgeLoop();
    }

    otaAutoCheckLoop();
    g_httpServer.handleClient();

    // 心跳逻辑
    if (g_workMode == WorkMode::GATEWAY && g_mqttClient.connected()) {
        unsigned long now = millis();
        if (now - g_lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
            g_lastHeartbeatMs = now;
            g_mqttClient.publish(buildMqttStatusTopic().c_str(), buildStatusPayload().c_str(), true, 1);
        }
    }

    if (g_needRestart) {
        delay(1000);
        ESP.restart();
    }
}
