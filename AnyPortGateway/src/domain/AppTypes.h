#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief AnyPort 领域层：全局常量、硬件引脚定义、枚举与核心配置结构体
 */

// -----------------------
// 1. 硬件引脚定义 (Dual485-Controller-V1 / ESP32-C3)
// -----------------------
// 注意: GPIO12/13 复用 C3 SPI Flash 引脚，编译时必须使用 FlashMode=dio
static const int PIN_W5500_SCK = 12;
static const int PIN_W5500_MISO = 10;
static const int PIN_W5500_MOSI = 13;
static const int PIN_W5500_CS = 1;
static const int PIN_W5500_RST = 7;
static const int PIN_W5500_INT = 0;

// RS485 通道引脚: 通道 0 = 485-1 (J1, 主站侧), 通道 1 = 485-2 (J3, 从站侧)
static const int PIN_RS485_1_RX = 2;
static const int PIN_RS485_1_TX = 3;
static const int PIN_RS485_2_RX = 5;
static const int PIN_RS485_2_TX = 4;

// -----------------------
// 2. 系统常量定义
// -----------------------
#define FIRMWARE_VERSION "2.0"

static constexpr const char *WIFI_AP_SSID = "anyport";
static constexpr const char *WIFI_AP_PASSWORD = "12345678";

static constexpr const char *MQTT_BROKER_HOST = "anyport.example.com";
static const uint16_t MQTT_BROKER_PORT = 443;
static constexpr const char *MQTT_BROKER_PATH = "/";
static constexpr const char *MQTT_USERNAME = "";
static constexpr const char *MQTT_PASSWORD = "";
static constexpr const char *MQTT_TOPIC_PREFIX = "anyport";
static constexpr const char *MQTT_SITE_ID = "office";
static constexpr const char *MQTT_GATEWAY_ID = "gateway-01";
static const size_t MQTT_JSON_DOC_SIZE = 1024;

static const uint32_t RS485_DEFAULT_BAUDRATE = 9600;
static const uint8_t RS485_DEFAULT_DATABITS = 8;
static const uint8_t RS485_DEFAULT_STOPBITS = 1;
static const uint8_t RS485_DEFAULT_PARITY = 0;
static const unsigned long HEARTBEAT_INTERVAL_MS = 30000;

// -----------------------
// 3. 业务枚举定义
// -----------------------
enum class WorkMode : uint8_t {
  GATEWAY = 0,         // MQTT 网关模式
  SIMULATOR = 1,       // 从站模拟器模式
  TRANSPARENT = 2,     // USB 转 RS485 透传模式
  BRIDGE = 3,          // 协议互转模式 (Modbus TCP <-> RTU)
  BUS_MONITOR = 4,     // 总线监听与质量诊断模式 (纯被动嗅探)
  ETH_WIFI_BRIDGE = 5, // 以太网转 WiFi 桥接模式
  DUAL_MASTER = 6      // 双主站中继网关模式
};

enum class NetInterface : uint8_t {
  AUTO = 0,  // 同时启用或自动切换
  W5500 = 1, // 仅以太网
  WIFI = 2   // 仅 WiFi
};

// -----------------------
// 4. 核心配置数据结构
// -----------------------
struct Rs485PortConfig {
  uint32_t baud;
  uint8_t dataBits;
  uint8_t parity;   // 0:None, 1:Even, 2:Odd
  uint8_t stopBits; // 1, 2
};

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
  bool staticIp;
  IPAddress ip;
  IPAddress subnet;
  IPAddress gateway;
  IPAddress dns;
};

struct BridgeConfig {
  uint8_t direction;    // 0: TCP to RTU, 1: RTU to TCP
  uint8_t bridgeMode;   // 0: 总线级 (Bus-level), 1: 单个设备 (Single Device)
  uint8_t slaveId;      // 单个设备模式下的从站地址码 (1-247)
  uint8_t netInterface; // 0: Auto, 1: W5500, 2: WiFi
  uint16_t tcpPort;     // Server 模式监听端口 (默认 502)
  String targetIp;      // Client 模式转发到此 IP
  uint16_t targetPort;  // Client 模式转发到此端口
  uint32_t baud;
  uint8_t stopBits;
  uint8_t parity;
  uint8_t dataBits;
};

struct EthWifiConfig {
  uint8_t direction;   // 0: WiFi -> W5500, 1: W5500 -> WiFi
  uint16_t listenPort; // W5500 / WiFi 监听端口
  String targetIp;     // 目标 IP
  uint16_t targetPort; // 目标端口
  uint8_t protocol;    // 0: TCP, 1: UDP
};

struct DualMasterConfig {
  String gatewayName;        // 网关设备名称 (全局标识 / AnyControl 平台标识)
  uint32_t masterBaud;       // RS485_A（原主站侧）波特率
  uint8_t masterData;        // 数据位 (7/8)
  uint8_t masterParity;      // 校验位 (0:None, 1:Even, 2:Odd)
  uint8_t masterStop;        // 停止位 (1/2)
  uint16_t wifiPort;         // WiFi TCP Server 监听端口 (默认 9502)
  uint16_t masterTimeout;    // RS485 从站物理响应超时 (ms，默认 1000)
  uint32_t heartbeatInterval;// AnyControl 心跳保活周期 (秒，默认 30)
};

// 全局硬件与总线通信遥测指标 (跨所有工作模式采集)
struct SystemTelemetry {
  uint32_t lastBusLatencyMs;  // 最近一次 RS485 从站物理应答耗时 (ms)
  uint32_t master1FrameCount; // RS485 串口侧 / 物理主站1 累计有效通信帧数
  uint32_t master2FrameCount; // 网络侧 (TCP/MQTT) / WiFi主站2 累计有效通信帧数
  uint32_t busCrcErrorCount;  // RS485 总线 CRC16 校验错误累计计数
};

// -----------------------
// 5. 全局配置上下文声明 (由 ConfigStore 统一管理生命周期)
// -----------------------
extern WorkMode g_workMode;
extern volatile bool g_needRestart;
extern uint8_t g_rs485Channel;
extern String g_otaApiBase;

extern Rs485PortConfig g_rs485_1Config;
extern Rs485PortConfig g_rs485_2Config;
extern EthStaticConfig g_ethConfig;
extern WifiStaConfig g_wifiStaConfig;
extern MqttRuntimeConfig g_mqttConfig;

extern uint32_t g_transBaud;
extern uint8_t g_transDataBits;
extern uint8_t g_transParity;
extern uint8_t g_transStopBits;

extern BridgeConfig g_bridgeConfig;
extern EthWifiConfig g_ethWifiConfig;
extern DualMasterConfig g_dualMasterConfig;
extern NetInterface g_netInterface;
extern SystemTelemetry g_telemetry;

// -----------------------
// 6. 统一调试日志宏 (透传模式下自动静默)
// -----------------------
#define APP_LOG(fmt, ...)                                                      \
  do {                                                                         \
    if (g_workMode != WorkMode::TRANSPARENT) {                                 \
      Serial.printf(fmt, ##__VA_ARGS__);                                       \
      Serial.println();                                                        \
    }                                                                          \
  } while (0)

#define APP_PRINT(x)                                                           \
  do {                                                                         \
    if (g_workMode != WorkMode::TRANSPARENT) {                                 \
      Serial.print(x);                                                         \
    }                                                                          \
  } while (0)

#define APP_PRINTLN(x)                                                         \
  do {                                                                         \
    if (g_workMode != WorkMode::TRANSPARENT) {                                 \
      Serial.println(x);                                                       \
    }                                                                          \
  } while (0)
