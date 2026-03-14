#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Ethernet.h>
#include <MQTT.h>
#include <Preferences.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "CommonTypes.h"

// -----------------------
// 1. 引脚定义
// -----------------------
static const int PIN_W5500_SCK = 6;
static const int PIN_W5500_MISO = 5;
static const int PIN_W5500_MOSI = 4;
static const int PIN_W5500_CS = 7;
static const int PIN_W5500_RST = 1;
static const int PIN_W5500_INT = 3;

static const int PIN_RS485_RX = 2;
static const int PIN_RS485_TX = 10;

// -----------------------
// 2. 常量定义
// -----------------------
static constexpr const char *WIFI_AP_SSID = "anyport";
static constexpr const char *WIFI_AP_PASSWORD = "12345678";
static constexpr const char *MDNS_DEFAULT_NAME = "anyport";

static constexpr const char *MQTT_BROKER_HOST = "anyport.example.com";
static const uint16_t MQTT_BROKER_PORT = 443;
static constexpr const char *MQTT_BROKER_PATH = "/";
static constexpr const char *MQTT_USERNAME = "";
static constexpr const char *MQTT_PASSWORD = "";
static constexpr const char *MQTT_TOPIC_PREFIX = "anyport";
static constexpr const char *MQTT_SITE_ID = "office";
static constexpr const char *MQTT_GATEWAY_ID = "gateway-01";
static const size_t MQTT_JSON_DOC_SIZE = 1024;

#define FIRMWARE_VERSION "1.2.0"

static const uint32_t RS485_DEFAULT_BAUDRATE = 9600;
static const uint8_t RS485_DEFAULT_DATABITS = 8;
static const uint8_t RS485_DEFAULT_STOPBITS = 1;
static const uint8_t RS485_DEFAULT_PARITY = 0;
static const unsigned long HEARTBEAT_INTERVAL_MS = 30000;

// -----------------------
// 3. 结构体定义
// -----------------------
struct MqttRuntimeConfig {
  bool valid;
  String host;
  uint16_t port;
  String path;
  String username;
  String password;
  String topicPrefix;
  String siteId;
  String gatewayId;
};

struct EthStaticConfig {
  bool valid;
  IPAddress ip;
  IPAddress subnet;
  IPAddress gateway;
  IPAddress dns;
};

struct WifiStaConfig {
  bool valid;
  String ssid;
  String password;
};

struct NtpConfig {
  bool valid;
  String server;
  uint32_t interval;
};

// -----------------------
// 4. 全局变量声明 (Extern)
// -----------------------
extern uint8_t g_macAddress[6];
extern HardwareSerial RS485Serial;
extern uint32_t g_rs485CurrentBaud;
extern uint32_t g_rs485CurrentConfig;
extern volatile bool g_rs485Busy;
extern WorkMode g_workMode;
extern Preferences g_prefs;
extern EthernetClient g_ethClient;
extern WiFiClientSecure g_mqttNetClient;
extern MQTTClient g_mqttClient;
extern unsigned long g_lastMqttReconnectAttempt;
extern int g_mqttRetryCount;
extern bool g_mqttKeepTrying;

extern EthStaticConfig g_ethConfig;
extern WifiStaConfig g_wifiStaConfig;
extern MqttRuntimeConfig g_mqttConfig;
extern NtpConfig g_ntpConfig;

extern WebServer g_httpServer;
extern uint8_t g_rtuRxBuffer[512];
extern size_t g_rtuRxLength;
extern bool g_needRestart;
extern String g_mdnsName;
extern uint32_t g_transBaud;
extern uint8_t g_transDataBits;
extern uint8_t g_transParity;
extern uint8_t g_transStopBits;
extern unsigned long g_lastHeartbeatMs;
