#pragma once

/*
 * AnyPort ESP32-C3 网关硬件与外设主配置文件
 *
 * 硬件平台：
 *   - MCU: ESP32-C3 (例如 ESP32-C3-DevKitM 或同类开发板)
 *   - 以太网: W5500 SPI 以太网模块
 *   - RS485: UART + RS485 收发器（自带自动方向控制，无需 DE/RE 引脚）
 *
 * 设计目标：
 *   - 当前端选择 Modbus TCP 时，ESP32 通过 W5500 建立 TCP 连接转发报文；
 *   - 当前端选择 Modbus RTU 时，ESP32 通过 RS485 接口转发报文。
 *
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Ethernet.h>
#include <MQTT.h>
#include <Preferences.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

/*
 * ---------------------------
 * 1. 引脚定义（可根据实际硬件调整）
 * ---------------------------
 *
 * 下述引脚选择基于典型 ESP32-C3 开发板可用 GPIO，
 * 如使用其他板型，请根据实际原理图修改。
 *
 * ESP32-C3 典型可用 GPIO 概览（仅供参考）：
 *   - GPIO0, GPIO1, GPIO2, GPIO3, GPIO4
 *   - GPIO5, GPIO6, GPIO7, GPIO8, GPIO9, GPIO10
 *   - GPIO18, GPIO19, GPIO20, GPIO21
 *
 * 注意事项：
 *   - 某些 GPIO 具有启动配置（strapping）功能，硬件设计时需避开上拉/下拉冲突；
 *   -
 * 本配置选用了常见且在大多数板子上可用的引脚组合，推荐在硬件定型后同步更新本处注释。
 */

// -----------------------
// 1.1 W5500 SPI 引脚定义
// -----------------------
//
// W5500 与 ESP32-C3 通过 SPI 连接，典型引脚：
//   - SCK  : SPI 时钟
//   - MISO : SPI 主输入从输出
//   - MOSI : SPI 主输出从输入
//   - CS   : 片选信号（低电平有效）
//   - RST  : W5500 硬件复位引脚（低电平保持复位，高电平工作）
//   - INT  : W5500 中断引脚（可选，用于链路状态或数据到达中断）
//
// 下列引脚仅为推荐值，使用前请与实际硬件连线确认：

// SPI 引脚（按照实际硬件连线）
//   CS  : GPIO7
//   SCK : GPIO6
//   MISO: GPIO5
//   MOSI: GPIO4
static const int PIN_W5500_SCK = 6;  // SPI SCK
static const int PIN_W5500_MISO = 5; // SPI MISO
static const int PIN_W5500_MOSI = 4; // SPI MOSI

// W5500 专用控制引脚
static const int PIN_W5500_CS = 7;  // W5500 芯片片选（CS）
static const int PIN_W5500_RST = 1; // W5500 复位引脚（RST）
static const int PIN_W5500_INT = 3; // W5500 中断引脚（INT，可选）

// -----------------------
// 1.2 RS485 引脚定义
// -----------------------
//
// RS485 接口在本硬件上具备自动流控功能：
//   - 仅需连接 UART TX/RX 到 RS485 模块，无需单独的 DE/RE 控制引脚。
//
// 下列引脚定义假设：
//   - 使用 UART1 作为 RS485 通道；
//   - TX 使用 GPIO21，RX 使用 GPIO20（对齐 README.md 推荐值）。
//
// 重要：ESP32-C3 的 GPIO20(RX) 和 GPIO21(TX) 经常在带串口芯片（如
// CH343）的开发板上 物理短接在 USB 芯片引脚上！继续接在这上面会导致
// RS485与电脑USB串口物理打架，接收发不出。 我们在此将默认 RS485
// 转移至安全空闲的引脚：
static const int PIN_RS485_RX = 2;  // 请将 RS485 的 RO/RX 接到 GPIO2
static const int PIN_RS485_TX = 10; // 请将 RS485 的 DI/TX 接到 GPIO10

// -----------------------
// 1.3 WiFi 与系统相关配置
// -----------------------

// WiFi AP（软 AP）模式配置：AnyPort 前端通过此 SSID 连接到 ESP32 网关
static constexpr const char *WIFI_AP_SSID = "anyport";
static constexpr const char *WIFI_AP_PASSWORD =
    "12345678"; // WPA2 要求最少 8 位，6 位密码会导致 AP 创建失败并回退到默认
                // SSID

static constexpr const char *MQTT_BROKER_HOST =
    "anyport.example.com"; // 默认域名
static const uint16_t MQTT_BROKER_PORT = 443;
static constexpr const char *MQTT_BROKER_PATH = "/"; // 默认改为根路径
static constexpr const char *MQTT_USERNAME = "";
static constexpr const char *MQTT_PASSWORD = "";
static constexpr const char *MQTT_TOPIC_PREFIX = "anyport";
static constexpr const char *MQTT_SITE_ID = "office";
static constexpr const char *MQTT_GATEWAY_ID = "gateway-01";
static const size_t MQTT_JSON_DOC_SIZE = 1024;
static constexpr const char *FIRMWARE_VERSION = "1.0.0";

// -----------------------
// 1.4 Modbus 相关配置（占位定义）
// -----------------------
//
// 在后续实现中，这些参数将用于：
//   - 网关接收到前端 JSON 消息时，决定使用 Modbus TCP 还是 RTU；
//   - 为 W5500 建立到目标设备的 TCP 连接；
//   - 为 RS485 配置 UART 波特率等。

// RS485 默认串口参数
static const uint32_t RS485_DEFAULT_BAUDRATE = 9600;
static const uint8_t RS485_DEFAULT_DATABITS = 8;
static const uint8_t RS485_DEFAULT_STOPBITS = 1;
static const uint8_t RS485_DEFAULT_PARITY =
    0; // 0: 无校验（N）; 1: 奇校验; 2: 偶校验

// -----------------------
// 2. 全局对象与句柄
// -----------------------

// W5500 Ethernet 使用 Arduino Ethernet 库
// 注意：Ethernet 库会使用 SPI 全局实例，因此需在初始化函数中先配置 SPI 引脚。
static uint8_t g_macAddress[6];

// RS485 使用 UART1
static HardwareSerial RS485Serial(1);

static uint32_t g_rs485CurrentBaud = RS485_DEFAULT_BAUDRATE;
static uint32_t g_rs485CurrentConfig = SERIAL_8N1;
static volatile bool g_rs485Busy = false;

static Preferences g_prefs;

// W5500 TCP 客户端，用于 Modbus TCP 透传（保持原有逻辑）
static EthernetClient g_ethClient;

// -----------------------
// 2. MQTT/TLS 客户端全局对象
// -----------------------
static WiFiClientSecure g_mqttNetClient;
static MQTTClient g_mqttClient(2048);
static unsigned long g_lastMqttReconnectAttempt = 0;

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
  uint32_t interval; // 同步间隔/超时设置 (秒)
};

static EthStaticConfig g_ethConfig = {};
static WifiStaConfig g_wifiStaConfig = {};
static MqttRuntimeConfig g_mqttConfig = {};
static NtpConfig g_ntpConfig = {};

static WebServer g_httpServer(80);

// Modbus RTU 接收缓冲区，显式声明在静态内存并对齐，避免堆内存抢占
static uint8_t g_rtuRxBuffer[512] __attribute__((aligned(4)));
static size_t g_rtuRxLength = 0;

// 全局标记：配置更新后是否需要重启
static bool g_needRestart = false;

// 心跳定时器
static unsigned long g_lastHeartbeatMs = 0;
static const unsigned long HEARTBEAT_INTERVAL_MS = 30000; // 30s 心跳

// -----------------------
// 3. 硬件初始化函数声明
// -----------------------

// 初始化所有硬件：GPIO、串口、WiFi、W5500 等
void anyportHardwareInit();

// 网关主循环入口：后续将添加 WebSocket 处理、Modbus 转发等逻辑
void anyportGatewayLoop();

// 内部辅助函数
static void initGpioPins();
static void initRs485Port();
static void initWifi();
static void initSpiAndEthernet();
static void loadPersistentConfig();
static void applyEthConfig();
static void initMqttClient();
static void mqttLoop();
static bool ensureMqttConnected();
static void mqttMessageReceived(String &topic, String &payload);
static String buildMqttStatusTopic();
static String buildMqttRequestFilter();
static String buildMqttResponseTopic(const String &sessionId);
static void handleMqttRequestMessage(const String &topic,
                                     const uint8_t *payload,
                                     unsigned int length);
static void initHttpServer();
static void httpLoop();
static void handleHttpRoot();
static void handleHttpConfig();
static bool parseIpAddress(const char *str, IPAddress &outIp);
static bool hexStringToBytes(const char *hex, uint8_t *buffer,
                             size_t bufferSize, size_t &outLength);
static String bytesToHexString(const uint8_t *data, size_t length);
static bool modbusTcpForward(JsonDocument &doc, JsonDocument &resp);
static bool modbusRtuForward(JsonDocument &doc, JsonDocument &resp);
static String buildStatusPayload(); // 构建完整状态 JSON
static void syncNtpTime();          // NTP 对时逻辑
static void pingForward(JsonDocument &doc,
                        const String &sessionId); // TCP Ping 探测

// -----------------------
// 4. 硬件初始化函数实现
// -----------------------

void anyportHardwareInit() {
  // ESP32-C3 有两种串口：
  //   - USB CDC (Serial)  : 通过 USB 口虚拟串口，Arduino IDE
  //   串口监视器默认用这个
  //   - UART0  (Serial0)  : 硬件 UART，GPIO20(RX)/GPIO21(TX)
  //
  // USB CDC 模式下：
  //   1. Serial.begin() 的波特率参数被忽略（USB 速率固定）
  //   2. !Serial 判断不可靠，会导致死等
  //   3. 正确做法：begin() 后固定延迟等待 USB 枚举完成即可
  Serial.begin(115200);
  delay(500); // 等待 USB CDC 枚举完成，固定延迟比 while(!Serial) 更可靠

  Serial.println();
  Serial.println("=== AnyPort ESP32 Gateway Starting ===");
  Serial.flush();

  initGpioPins();
  Serial.println("[Init] GPIO OK");
  Serial.flush();

  initRs485Port();
  Serial.println("[Init] RS485 OK");
  Serial.flush();

  loadPersistentConfig();
  Serial.println("[Init] Config loaded");
  Serial.print("  WiFi STA config valid: ");
  Serial.println(g_wifiStaConfig.valid ? "YES" : "NO");
  if (g_wifiStaConfig.valid) {
    Serial.print("  STA SSID: ");
    Serial.println(g_wifiStaConfig.ssid);
  }
  Serial.flush();

  initWifi();
  Serial.flush();

  // 在网络就绪后，MQTTS 握手前进行对时
  syncNtpTime();

  initSpiAndEthernet();
  Serial.println("[Init] SPI/Ethernet OK");
  Serial.flush();

  initMqttClient();
  Serial.println("[Init] MQTT client configured");
  Serial.flush();

  initHttpServer();
  Serial.println("[Init] HTTP server started");
  Serial.flush();

  Serial.println("=== Hardware Init Done ===");
  Serial.flush();
}

void anyportGatewayLoop() {
  mqttLoop();
  httpLoop();

  // 心跳：每 30s 重新发布完整状态消息，让前端持续感知网关在线
  if (g_mqttClient.connected()) {
    unsigned long now = millis();
    if (now - g_lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
      g_lastHeartbeatMs = now;
      String statusTopic = buildMqttStatusTopic();
      String hbPayload = buildStatusPayload();
      g_mqttClient.publish(statusTopic.c_str(), hbPayload.c_str(), true, 1);
    }
  }

  if (g_needRestart) {
    delay(100);
    ESP.restart();
  }
}

static void initGpioPins() {
  // 配置 W5500 控制引脚
  pinMode(PIN_W5500_CS, OUTPUT);
  digitalWrite(PIN_W5500_CS, HIGH); // 默认不选中 W5500

  pinMode(PIN_W5500_RST, OUTPUT);
  digitalWrite(PIN_W5500_RST, HIGH); // 拉高表示正常工作，下面会给一个复位脉冲

  pinMode(PIN_W5500_INT, INPUT_PULLUP); // 中断引脚上拉，避免浮空
}

static void initRs485Port() {
  g_rs485CurrentBaud = RS485_DEFAULT_BAUDRATE;
  g_rs485CurrentConfig = SERIAL_8N1;
  RS485Serial.begin(RS485_DEFAULT_BAUDRATE, SERIAL_8N1, PIN_RS485_RX,
                    PIN_RS485_TX);
}

static void initWifi() {
  // 关键：禁止 ESP32 从 NVS 读取/写入 WiFi 配置
  // 不加此行，ESP32 会从 NVS 中读取旧的 SSID（如 ESP32_XXXXXX）覆盖代码设置
  WiFi.persistent(false);
  WiFi.disconnect(true); // 清除当前连接状态，确保从干净状态启动
  delay(100);

  if (g_wifiStaConfig.valid) {
    WiFi.mode(WIFI_STA);
    Serial.print("WiFi STA connecting to SSID: ");
    Serial.println(g_wifiStaConfig.ssid);
    WiFi.begin(g_wifiStaConfig.ssid.c_str(), g_wifiStaConfig.password.c_str());

    unsigned long start = millis();
    const unsigned long timeoutMs = 10000;
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
      delay(100);
    }

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi STA connect failed, fallback to AP mode");
      WiFi.disconnect(true);
      delay(200);
      WiFi.mode(WIFI_AP);
      delay(100);
      bool apOk = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
      Serial.print("softAP result: ");
      Serial.println(apOk ? "OK" : "FAILED");
    }
  } else {
    WiFi.mode(WIFI_AP);
    delay(100);
    bool apOk = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
    Serial.print("softAP result: ");
    Serial.println(apOk ? "OK" : "FAILED");
  }

  if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi STA connected, IP: ");
    Serial.println(WiFi.localIP());
  } else if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
    Serial.print("WiFi AP mode started, SSID: ");
    Serial.println(WIFI_AP_SSID);
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
  }
}

static void initSpiAndEthernet() {
  // 配置 SPI 引脚并初始化 SPI 总线
  // [关键修复]：这里必须将 CS 引脚传 -1！
  // 因为 Ethernet 库内部是通过普通的 digitalWrite 来拉低/拉高 CS 的。
  // 如果在 SPI.begin() 中传入了 CS 引脚，ESP32 底层硬件 SPI
  // 就会“劫持”这个引脚， 导致 Ethernet 库的 digitalWrite 操作完全无效，从而读出
  // 0。
  pinMode(PIN_W5500_CS, OUTPUT);
  digitalWrite(PIN_W5500_CS, HIGH);
  SPI.begin(PIN_W5500_SCK, PIN_W5500_MISO, PIN_W5500_MOSI, -1);

  // 对 W5500 执行硬件复位
  digitalWrite(PIN_W5500_RST, LOW);
  delay(10);
  digitalWrite(PIN_W5500_RST, HIGH);
  delay(200); // 等待 W5500 内部初始化

  Ethernet.init(static_cast<uint8_t>(PIN_W5500_CS));

  // 必须先调用 applyEthConfig() 里的 Ethernet.begin()，
  // 然后 Ethernet 库底层才会去初始化 W5500 并让 hardwareStatus()
  // 生效，否则永远是0！
  applyEthConfig();

  // 检测 W5500 是否存在：读取版本寄存器（应为 0x04）
  uint8_t ver = Ethernet.hardwareStatus();
  Serial.print("[Init] Ethernet lib hardware status: ");
  Serial.println(ver);
}

static void loadPersistentConfig() {
  g_prefs.begin("anyport", true);

  g_ntpConfig.valid = false;
  g_ntpConfig.server = "ntp.aliyun.com";
  g_ntpConfig.interval = 3600;

  if (g_prefs.isKey("ethIp") && g_prefs.isKey("ethMask")) {
    uint32_t ipRaw = g_prefs.getUInt("ethIp", 0);
    uint32_t maskRaw = g_prefs.getUInt("ethMask", 0);
    uint32_t gwRaw = g_prefs.getUInt("ethGw", 0);
    uint32_t dnsRaw = g_prefs.getUInt("ethDns", 0);

    if (ipRaw != 0 && maskRaw != 0) {
      g_ethConfig.valid = true;
      g_ethConfig.ip = IPAddress(ipRaw);
      g_ethConfig.subnet = IPAddress(maskRaw);
      g_ethConfig.gateway = IPAddress(gwRaw);
      g_ethConfig.dns = IPAddress(dnsRaw);
    }
  }

  if (g_prefs.isKey("wifiSsid")) {
    g_wifiStaConfig.valid = true;
    g_wifiStaConfig.ssid = g_prefs.getString("wifiSsid", "");
    g_wifiStaConfig.password = g_prefs.getString("wifiPwd", "");
  }

  if (g_prefs.isKey("ntpServer")) {
    g_ntpConfig.server = g_prefs.getString("ntpServer", g_ntpConfig.server);
    g_ntpConfig.interval = g_prefs.getUInt("ntpInterval", g_ntpConfig.interval);
    g_ntpConfig.valid = true;
  }

  // MQTT 配置加载逻辑
  if (g_prefs.isKey("mqttHost")) {
    g_mqttConfig.host = g_prefs.getString("mqttHost", MQTT_BROKER_HOST);
    g_mqttConfig.port = g_prefs.getUShort("mqttPort", MQTT_BROKER_PORT);
    g_mqttConfig.path = g_prefs.getString("mqttPath", MQTT_BROKER_PATH);
    g_mqttConfig.username = g_prefs.getString("mqttUser", MQTT_USERNAME);
    g_mqttConfig.password = g_prefs.getString("mqttPwd", MQTT_PASSWORD);
    g_mqttConfig.topicPrefix =
        g_prefs.getString("mqttPrefix", MQTT_TOPIC_PREFIX);
    g_mqttConfig.siteId = g_prefs.getString("mqttSite", MQTT_SITE_ID);
    g_mqttConfig.gatewayId = g_prefs.getString("mqttGateway", MQTT_GATEWAY_ID);
    g_mqttConfig.valid = true;

    Serial.println("[Init] MQTT config loaded from NVS");
    Serial.print("  Host: ");
    Serial.println(g_mqttConfig.host);
    Serial.print("  Path: ");
    Serial.println(g_mqttConfig.path);
  } else {
    Serial.println("[Init] No MQTT config in NVS, using defaults");
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
  g_prefs.end();
}

static void applyEthConfig() {
  if (!g_ethConfig.valid) {
    Serial.println("[ETH] No config, skip");
    return;
  }

  // 获取 ESP32 的基准 MAC 地址，并将首字节第二位置 1 （Locally Administered
  // MAC） 以保证网络中 MAC 是唯一的，防止与局域网内其他设备或默认 W5500 MAC
  // 冲突导致 ARP 丢失
  WiFi.macAddress(g_macAddress);
  g_macAddress[0] = (g_macAddress[0] & 0xFC) | 0x02;

  Ethernet.begin(g_macAddress, g_ethConfig.ip, g_ethConfig.dns,
                 g_ethConfig.gateway, g_ethConfig.subnet);
  Serial.print("[ETH] IP=");
  Serial.println(Ethernet.localIP());
}

static void syncNtpTime() {
  Serial.println("[NTP] Initializing time sync...");
  // 强制设置时区为 UTC+8 (北京时间)
  configTime(8 * 3600, 0, g_ntpConfig.server.c_str(), "pool.ntp.org",
             "time.nist.gov");

  // 如果 WiFi 未连接，直接退出等待
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[NTP] WiFi not connected, skip sync");
    return;
  }

  // 非阻塞超时等待逻辑 (最多等待 5s)
  unsigned long start = millis();
  bool synced = false;
  Serial.print("[NTP] Waiting for sync");
  while (millis() - start < 5000) {
    time_t now = time(nullptr);
    if (now > 1000000000L) { // 如果年份大于 2001，说明同步成功
      synced = true;
      break;
    }
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (synced) {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    Serial.print("[NTP] System time synced: ");
    Serial.println(timeStr);
  } else {
    Serial.println("[NTP] Sync FAILED (timeout), proceeding with default time");
  }
}

static String buildMqttStatusTopic() {
  String prefix = g_mqttConfig.topicPrefix.length() > 0
                      ? g_mqttConfig.topicPrefix
                      : String(MQTT_TOPIC_PREFIX);
  String site = g_mqttConfig.siteId.length() > 0 ? g_mqttConfig.siteId
                                                 : String(MQTT_SITE_ID);
  String gateway = g_mqttConfig.gatewayId.length() > 0
                       ? g_mqttConfig.gatewayId
                       : String(MQTT_GATEWAY_ID);

  String topic = prefix;
  topic += "/";
  topic += site;
  topic += "/";
  topic += gateway;
  topic += "/status";
  return topic;
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

  String topic = prefix;
  topic += "/";
  topic += site;
  topic += "/";
  topic += gateway;
  topic += "/request/+";
  return topic;
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

  String topic = prefix;
  topic += "/";
  topic += site;
  topic += "/";
  topic += gateway;
  topic += "/response/";
  topic += sessionId;
  return topic;
}

static void mqttMessageReceived(String &topic, String &payload) {
  handleMqttRequestMessage(topic, (const uint8_t *)payload.c_str(),
                           payload.length());
}

static void initMqttClient() {
  String host = g_mqttConfig.host.length() > 0 ? g_mqttConfig.host
                                               : String(MQTT_BROKER_HOST);
  uint16_t port = g_mqttConfig.port != 0 ? g_mqttConfig.port : MQTT_BROKER_PORT;
  g_mqttNetClient.setInsecure();
  g_mqttClient.begin(host.c_str(), port, g_mqttNetClient);
  g_mqttClient.onMessage(mqttMessageReceived);
}

static bool ensureMqttConnected() {
  if (g_mqttClient.connected()) {
    return true;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  unsigned long now = millis();
  if (now - g_lastMqttReconnectAttempt < 5000) {
    return false;
  }
  g_lastMqttReconnectAttempt = now;

  String clientId = "anyport-esp32-";
  clientId += String((uint32_t)ESP.getEfuseMac(), HEX);
  String statusTopic = buildMqttStatusTopic();

  StaticJsonDocument<128> willDoc;
  willDoc["status"] = "offline";
  willDoc["version"] = FIRMWARE_VERSION;
  String willPayload;
  serializeJson(willDoc, willPayload);

  g_mqttClient.setWill(statusTopic.c_str(), willPayload.c_str(), true, 1);

  Serial.print("[MQTT] Connecting to broker as ");
  Serial.println(clientId);

  g_mqttClient.setOptions(60, true, 5000);

  bool ok = false;
  if (g_mqttConfig.username.length() > 0) {
    ok = g_mqttClient.connect(clientId.c_str(), g_mqttConfig.username.c_str(),
                              g_mqttConfig.password.c_str());
  } else {
    ok = g_mqttClient.connect(clientId.c_str());
  }

  if (!ok) {
    Serial.print("[MQTT] Connect FAILED, lastError=");
    Serial.print((int)g_mqttClient.lastError());
    Serial.print(" returnCode=");
    Serial.println((int)g_mqttClient.returnCode());
    return false;
  }

  Serial.println("[MQTT] Connected!");

  String onlinePayload = buildStatusPayload();
  g_mqttClient.publish(statusTopic.c_str(), onlinePayload.c_str(), true, 1);
  g_lastHeartbeatMs = millis();

  g_mqttClient.subscribe(buildMqttRequestFilter().c_str(), 1);

  return true;
}

static void mqttLoop() {
  if (ensureMqttConnected()) {
    g_mqttClient.loop();
  }
}

static void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String t(topic);
  handleMqttRequestMessage(t, payload, length);
}

static void initHttpServer() {
  g_httpServer.on("/", HTTP_GET, handleHttpRoot);
  g_httpServer.on("/config", HTTP_POST, handleHttpConfig);
  g_httpServer.begin();
}

static void httpLoop() { g_httpServer.handleClient(); }

static void handleHttpRoot() {
  String html;
  html.reserve(1024);
  html += "<!DOCTYPE html><html><head><meta charset='utf-8'><title>AnyPort "
          "ESP32 配置</title></head><body>";
  html += "<h1>AnyPort ESP32 配置</h1>";
  html += "<form method='POST' action='/config'>";
  html += "<h2>WiFi STA</h2>";
  html += "SSID: <input name='wifiSsid' value='";
  html += g_wifiStaConfig.ssid;
  html += "'><br>";
  html += "密码: <input name='wifiPwd' type='password' value='";
  html += g_wifiStaConfig.password;
  html += "'><br>";
  html += "<h2>MQTT</h2>";
  html += "Broker Host: <input name='mqttHost' value='";
  html += g_mqttConfig.host;
  html += "'><br>";
  html += "Broker Port: <input name='mqttPort' value='";
  html += String(g_mqttConfig.port);
  html += "'><br>";
  html += "WebSocket Path: <input name='mqttPath' value='";
  html += g_mqttConfig.path;
  html += "'><br>";
  html += "用户名: <input name='mqttUser' value='";
  html += g_mqttConfig.username;
  html += "'><br>";
  html += "密码: <input name='mqttPwd' type='password' value='";
  html += g_mqttConfig.password;
  html += "'><br>";
  html += "Topic 前缀: <input name='mqttPrefix' value='";
  html += g_mqttConfig.topicPrefix.length() > 0 ? g_mqttConfig.topicPrefix
                                                : String(MQTT_TOPIC_PREFIX);
  html += "'><br>";
  html += "Site ID: <input name='mqttSite' value='";
  html += g_mqttConfig.siteId.length() > 0 ? g_mqttConfig.siteId
                                           : String(MQTT_SITE_ID);
  html += "'><br>";
  html += "Gateway ID: <input name='mqttGateway' value='";
  html += g_mqttConfig.gatewayId;
  html += "'><br>";
  html += "<h2>W5500 以太网 (静态 IP)</h2>";
  html += "IP 地址: <input name='ethIp' placeholder='192.168.1.100' value='";
  if (g_ethConfig.valid)
    html += g_ethConfig.ip.toString();
  html += "'><br>";
  html += "子网掩码: <input name='ethMask' placeholder='255.255.255.0' value='";
  if (g_ethConfig.valid)
    html += g_ethConfig.subnet.toString();
  html += "'><br>";
  html += "网关地址: <input name='ethGw' placeholder='192.168.1.1' value='";
  if (g_ethConfig.valid)
    html += g_ethConfig.gateway.toString();
  html += "'><br>";
  html += "DNS 服务器: <input name='ethDns' placeholder='8.8.8.8' value='";
  if (g_ethConfig.valid)
    html += g_ethConfig.dns.toString();
  html += "'><br>";

  html += "<h2>NTP</h2>";
  html += "NTP Server: <input name='ntpServer' value='";
  html += g_ntpConfig.server;
  html += "'><br>";
  html += "同步间隔 (s): <input name='ntpInterval' value='";
  html += String(g_ntpConfig.interval);
  html += "'><br>";
  html += "<button type='submit'>保存并重启</button>";
  html += "</form>";
  html += "</body></html>";
  g_httpServer.send(200, "text/html; charset=utf-8", html);
}

static void handleHttpConfig() {
  String wifiSsid = g_httpServer.arg("wifiSsid");
  String wifiPwd = g_httpServer.arg("wifiPwd");
  String mqttHost = g_httpServer.arg("mqttHost");
  String mqttPortStr = g_httpServer.arg("mqttPort");
  String mqttPath = g_httpServer.arg("mqttPath");
  String mqttUser = g_httpServer.arg("mqttUser");
  String mqttPwd = g_httpServer.arg("mqttPwd");
  String mqttPrefix = g_httpServer.arg("mqttPrefix");
  String mqttSite = g_httpServer.arg("mqttSite");
  String mqttGateway = g_httpServer.arg("mqttGateway");
  String ntpServer = g_httpServer.arg("ntpServer");
  String ntpIntervalStr = g_httpServer.arg("ntpInterval");

  g_prefs.begin("anyport", false);

  if (wifiSsid.length() > 0) {
    g_prefs.putString("wifiSsid", wifiSsid);
    g_prefs.putString("wifiPwd", wifiPwd);
    g_wifiStaConfig.valid = true;
    g_wifiStaConfig.ssid = wifiSsid;
    g_wifiStaConfig.password = wifiPwd;
  }

  if (mqttHost.length() > 0) {
    uint16_t mqttPort = static_cast<uint16_t>(mqttPortStr.toInt());
    if (mqttPort == 0) {
      mqttPort = MQTT_BROKER_PORT;
    }

    // 如果网页提交的 path 为空，则存为一个 / ，保证格式合法，或者直接允许为空
    String finalPath = mqttPath.length() > 0 ? mqttPath : "/";

    g_prefs.putString("mqttHost", mqttHost);
    g_prefs.putUShort("mqttPort", mqttPort);
    g_prefs.putString("mqttPath", finalPath);
    g_prefs.putString("mqttUser", mqttUser);
    g_prefs.putString("mqttPwd", mqttPwd);
    g_prefs.putString("mqttPrefix", mqttPrefix);
    g_prefs.putString("mqttSite", mqttSite);
    g_prefs.putString("mqttGateway", mqttGateway);

    g_mqttConfig.host = mqttHost;
    g_mqttConfig.port = mqttPort;
    g_mqttConfig.path = finalPath;
    g_mqttConfig.username = mqttUser;
    g_mqttConfig.password = mqttPwd;
    g_mqttConfig.topicPrefix = mqttPrefix;
    g_mqttConfig.siteId = mqttSite;
    g_mqttConfig.gatewayId = mqttGateway;
    g_mqttConfig.valid = true;
  }

  if (ntpServer.length() > 0) {
    g_prefs.putString("ntpServer", ntpServer);
    g_prefs.putUInt("ntpInterval", ntpIntervalStr.toInt());
    g_ntpConfig.server = ntpServer;
    g_ntpConfig.interval = ntpIntervalStr.toInt();
    g_ntpConfig.valid = true;
  }

  // 以太网配置解析与保存
  String ethIpStr = g_httpServer.arg("ethIp");
  String ethMaskStr = g_httpServer.arg("ethMask");
  String ethGwStr = g_httpServer.arg("ethGw");
  String ethDnsStr = g_httpServer.arg("ethDns");

  if (ethIpStr.length() > 0 && ethMaskStr.length() > 0) {
    IPAddress ip, mask, gw, dns;
    if (parseIpAddress(ethIpStr.c_str(), ip) &&
        parseIpAddress(ethMaskStr.c_str(), mask)) {
      parseIpAddress(ethGwStr.c_str(), gw);
      parseIpAddress(ethDnsStr.c_str(), dns);

      g_prefs.putUInt("ethIp", (uint32_t)ip);
      g_prefs.putUInt("ethMask", (uint32_t)mask);
      g_prefs.putUInt("ethGw", (uint32_t)gw);
      g_prefs.putUInt("ethDns", (uint32_t)dns);

      g_ethConfig.ip = ip;
      g_ethConfig.subnet = mask;
      g_ethConfig.gateway = gw;
      g_ethConfig.dns = dns;
      g_ethConfig.valid = true;
    }
  }

  g_prefs.end();

  g_needRestart = true;

  g_httpServer.send(
      200, "text/html; charset=utf-8",
      "<html><body><h1>配置已保存，设备即将重启</h1></body></html>");
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
    String out;
    serializeJson(resp, out);
    String respTopic = buildMqttResponseTopic(sessionId);
    g_mqttClient.publish(respTopic.c_str(), out.c_str(), false, 1);
    return;
  }

  const char *transport = doc["transport"] | "";

  // Ping 探测：无需 payloadHex，独立处理
  if (strcmp(transport, "ping") == 0) {
    pingForward(doc, sessionId);
    return;
  }

  const char *payloadHex = doc["payloadHex"] | "";

  if (!transport[0] || !payloadHex[0]) {
    StaticJsonDocument<256> resp;
    resp["sessionId"] = sessionId;
    resp["success"] = false;
    resp["error"] = "missing_transport_or_payloadHex";
    String out;
    serializeJson(resp, out);
    String respTopic = buildMqttResponseTopic(sessionId);
    g_mqttClient.publish(respTopic.c_str(), out.c_str(), false, 1);
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
    String out;
    serializeJson(resp, out);
    String respTopic = buildMqttResponseTopic(sessionId);
    g_mqttClient.publish(respTopic.c_str(), out.c_str(), false, 1);
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

  String out;
  serializeJson(resp, out);
  String respTopic = buildMqttResponseTopic(sessionId);
  g_mqttClient.publish(respTopic.c_str(), out.c_str(), false, 1);
}

static bool parseIpAddress(const char *str, IPAddress &outIp) {
  int parts[4] = {0, 0, 0, 0};
  int count =
      sscanf(str, "%d.%d.%d.%d", &parts[0], &parts[1], &parts[2], &parts[3]);
  if (count != 4) {
    return false;
  }
  for (int i = 0; i < 4; ++i) {
    if (parts[i] < 0 || parts[i] > 255) {
      return false;
    }
  }
  outIp = IPAddress(parts[0], parts[1], parts[2], parts[3]);
  return true;
}

static bool hexStringToBytes(const char *hex, uint8_t *buffer,
                             size_t bufferSize, size_t &outLength) {
  size_t len = strlen(hex);
  if ((len % 2) != 0) {
    return false;
  }
  size_t byteCount = len / 2;
  if (byteCount > bufferSize) {
    return false;
  }

  for (size_t i = 0; i < byteCount; ++i) {
    char high = hex[i * 2];
    char low = hex[i * 2 + 1];

    uint8_t h = (high >= '0' && high <= '9')   ? high - '0'
                : (high >= 'A' && high <= 'F') ? high - 'A' + 10
                : (high >= 'a' && high <= 'f') ? high - 'a' + 10
                                               : 0xFF;
    uint8_t l = (low >= '0' && low <= '9')   ? low - '0'
                : (low >= 'A' && low <= 'F') ? low - 'A' + 10
                : (low >= 'a' && low <= 'f') ? low - 'a' + 10
                                             : 0xFF;

    if (h == 0xFF || l == 0xFF) {
      return false;
    }

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

static bool buildRs485Config(uint32_t requestedBaud, uint8_t stopBits,
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

static void pingForward(JsonDocument &doc, const String &sessionId) {
  StaticJsonDocument<512> resp;
  resp["sessionId"] = sessionId;
  resp["type"] = "ping";

  if (!doc.containsKey("pingTarget")) {
    resp["success"] = false;
    resp["error"] = "missing_ping_target";
    String out;
    serializeJson(resp, out);
    String respTopic = buildMqttResponseTopic(sessionId);
    g_mqttClient.publish(respTopic.c_str(), out.c_str(), false, 1);
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
    String out;
    serializeJson(resp, out);
    String respTopic = buildMqttResponseTopic(sessionId);
    g_mqttClient.publish(respTopic.c_str(), out.c_str(), false, 1);
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
    String respTopic = buildMqttResponseTopic(sessionId);
    g_mqttClient.publish(respTopic.c_str(), out.c_str(), false, 1);
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
  String respTopic = buildMqttResponseTopic(sessionId);
  g_mqttClient.publish(respTopic.c_str(), out.c_str(), false, 1);
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

// 构建完整状态 JSON 字符串（上线消息 + 心跳共用）
// 包含：status, version, 串口参数, W5500 IP, WiFi IP
static String buildStatusPayload() {
  StaticJsonDocument<384> doc;
  doc["status"] = "online";
  doc["version"] = FIRMWARE_VERSION;

  // 添加系统时间字段
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

  // RS485 串口参数
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
  } else {
    doc["parity"] = "none";
    doc["stopBits"] = 1;
  }

  // W5500 以太网 IP（TCP 模式目标设备通过此网口通信）
  IPAddress ethIp = ::Ethernet.localIP();
  if (ethIp != IPAddress(0, 0, 0, 0)) {
    doc["ethIp"] = ethIp.toString();
  }

  // WiFi IP（MQTT 连接走此接口）
  if (WiFi.status() == WL_CONNECTED) {
    doc["wifiIp"] = WiFi.localIP().toString();
  } else if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
    doc["wifiIp"] = WiFi.softAPIP().toString();
  }

  String out;
  serializeJson(doc, out);
  return out;
}
