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
 *   - 在 AnyPort 前端的 “WiFi 网关” 模式下，浏览器通过 WebSocket 连接到 ESP32 网关；
 *   - 当前端选择 Modbus TCP 时，ESP32 通过 W5500 建立 TCP 连接转发报文；
 *   - 当前端选择 Modbus RTU 时，ESP32 通过 RS485 接口转发报文。
 *
 * 约束：
 *   - 必须包含完整的初始化逻辑，不使用 TODO 省略；
 *   - 必须给出详细的引脚定义说明，方便在不同 ESP32-C3 开发板上迁移。
 */

#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <Ethernet.h>
#include <Preferences.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

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
 *   - 本配置选用了常见且在大多数板子上可用的引脚组合，推荐在硬件定型后同步更新本处注释。
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
static const int PIN_W5500_SCK  = 6;   // SPI SCK
static const int PIN_W5500_MISO = 5;   // SPI MISO
static const int PIN_W5500_MOSI = 4;   // SPI MOSI

// W5500 专用控制引脚
static const int PIN_W5500_CS   = 7;   // W5500 芯片片选（CS）
static const int PIN_W5500_RST  = 1;   // W5500 复位引脚（RST）
static const int PIN_W5500_INT  = 3;   // W5500 中断引脚（INT，可选）

// -----------------------
// 1.2 RS485 引脚定义
// -----------------------
//
// RS485 接口在本硬件上具备自动流控功能：
//   - 仅需连接 UART TX/RX 到 RS485 模块，无需单独的 DE/RE 控制引脚。
//
// 下列引脚定义假设：
//   - 使用 UART1 作为 RS485 通道；
//   - TX 使用 GPIO21，RX 使用 GPIO20。
//
static const int PIN_RS485_TX    = 21;  // RS485 TX (UART1 TX)
static const int PIN_RS485_RX    = 20;  // RS485 RX (UART1 RX)

// -----------------------
// 1.3 WiFi 与系统相关配置
// -----------------------

// WiFi AP（软 AP）模式配置：AnyPort 前端通过此 SSID 连接到 ESP32 网关
static constexpr const char* WIFI_AP_SSID     = "AnyPort-Gateway";
static constexpr const char* WIFI_AP_PASSWORD = "AnyPort1234";

// WebSocket 网关默认端口，与 AnyPort 前端的 wsPort 对应，推荐保持一致
static const uint16_t DEFAULT_WEBSOCKET_PORT = 81;

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
static const uint8_t  RS485_DEFAULT_DATABITS = 8;
static const uint8_t  RS485_DEFAULT_STOPBITS = 1;
static const uint8_t  RS485_DEFAULT_PARITY   = 0; // 0: 无校验（N）; 1: 奇校验; 2: 偶校验

// -----------------------
// 2. 全局对象与句柄
// -----------------------

// W5500 Ethernet 使用 Arduino Ethernet 库
// 注意：Ethernet 库会使用 SPI 全局实例，因此需在初始化函数中先配置 SPI 引脚。
static byte g_macAddress[6] = { 0x02, 0x00, 0x00, 0x00, 0xC3, 0x01 };

// RS485 使用 UART1
static HardwareSerial RS485Serial(1);

// WebSocket 服务器与配置存储
static Preferences g_prefs;
static WebSocketsServer g_webSocket(0);

// W5500 TCP 客户端，用于 Modbus TCP 透传
static EthernetClient g_ethClient;

// 当前 WebSocket 端口和以太网静态 IP 配置
struct EthStaticConfig {
    bool     valid;
    IPAddress ip;
    IPAddress subnet;
    IPAddress gateway;
    IPAddress dns;
};

struct WifiStaConfig {
    bool   valid;
    String ssid;
    String password;
};

static uint16_t       g_wsPort = DEFAULT_WEBSOCKET_PORT;
static EthStaticConfig g_ethConfig = {};
static WifiStaConfig   g_wifiStaConfig = {};

// Modbus RTU 接收缓冲区
static uint8_t g_rtuRxBuffer[256];
static size_t  g_rtuRxLength = 0;

// 全局标记：配置更新后是否需要重启
static bool g_needRestart = false;

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
static void startWebSocketServer();
static void handleWebSocketLoop();
static void handleWebSocketMessage(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
static void handleConfigMessage(JsonDocument& doc);
static void handleModbusMessage(JsonDocument& doc, uint8_t clientId);
static bool parseIpAddress(const char* str, IPAddress& outIp);
static bool hexStringToBytes(const char* hex, uint8_t* buffer, size_t bufferSize, size_t& outLength);
static String bytesToHexString(const uint8_t* data, size_t length);
static bool modbusTcpForward(const JsonDocument& doc, JsonDocument& resp);
static bool modbusRtuForward(const JsonDocument& doc, JsonDocument& resp);

// -----------------------
// 4. 硬件初始化函数实现
// -----------------------

void anyportHardwareInit() {
    initGpioPins();
    initRs485Port();
    loadPersistentConfig();
    initWifi();
    initSpiAndEthernet();
    startWebSocketServer();
}

void anyportGatewayLoop() {
    handleWebSocketLoop();

    if (g_needRestart) {
        delay(100);
        ESP.restart();
    }
}

static void initGpioPins() {
    // 配置 W5500 控制引脚
    pinMode(PIN_W5500_CS, OUTPUT);
    digitalWrite(PIN_W5500_CS, HIGH);   // 默认不选中 W5500

    pinMode(PIN_W5500_RST, OUTPUT);
    digitalWrite(PIN_W5500_RST, HIGH);  // 拉高表示正常工作，下面会给一个复位脉冲

    pinMode(PIN_W5500_INT, INPUT_PULLUP); // 中断引脚上拉，避免浮空
}

static void initRs485Port() {
    // 将 UART1 映射到指定引脚，用于 RS485 通讯
    RS485Serial.begin(
        RS485_DEFAULT_BAUDRATE,
        SERIAL_8N1,
        PIN_RS485_RX,
        PIN_RS485_TX
    );
}

static void initWifi() {
    g_prefs.begin("anyport", false);

    if (g_wifiStaConfig.valid) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(g_wifiStaConfig.ssid.c_str(), g_wifiStaConfig.password.c_str());

        unsigned long start = millis();
        const unsigned long timeoutMs = 10000;
        while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
            delay(100);
        }

        if (WiFi.status() != WL_CONNECTED) {
            WiFi.disconnect(true);
            WiFi.mode(WIFI_AP);
            WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
        }
    } else {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
    }
}

static void initSpiAndEthernet() {
    // 配置 SPI 引脚并初始化 SPI 总线
    SPI.begin(
        PIN_W5500_SCK,
        PIN_W5500_MISO,
        PIN_W5500_MOSI,
        PIN_W5500_CS
    );

    // 对 W5500 执行硬件复位：先拉低一段时间，再拉高
    digitalWrite(PIN_W5500_RST, LOW);
    delay(10);  // 保持低电平 10ms，确保复位稳定
    digitalWrite(PIN_W5500_RST, HIGH);
    delay(100); // 复位后等待 W5500 内部时钟和寄存器稳定

    Ethernet.init(static_cast<uint8_t>(PIN_W5500_CS));

    applyEthConfig();
}

static void loadPersistentConfig() {
    g_prefs.begin("anyport", true);

    g_wsPort = static_cast<uint16_t>(g_prefs.getUShort("wsPort", DEFAULT_WEBSOCKET_PORT));

    if (g_prefs.isKey("ethIp") && g_prefs.isKey("ethMask")) {
        uint32_t ipRaw   = g_prefs.getUInt("ethIp", 0);
        uint32_t maskRaw = g_prefs.getUInt("ethMask", 0);
        uint32_t gwRaw   = g_prefs.getUInt("ethGw", 0);
        uint32_t dnsRaw  = g_prefs.getUInt("ethDns", 0);

        if (ipRaw != 0 && maskRaw != 0) {
            g_ethConfig.valid   = true;
            g_ethConfig.ip      = IPAddress(ipRaw);
            g_ethConfig.subnet  = IPAddress(maskRaw);
            g_ethConfig.gateway = IPAddress(gwRaw);
            g_ethConfig.dns     = IPAddress(dnsRaw);
        }
    }

    if (g_prefs.isKey("wifiSsid")) {
        g_wifiStaConfig.valid = true;
        g_wifiStaConfig.ssid = g_prefs.getString("wifiSsid", "");
        g_wifiStaConfig.password = g_prefs.getString("wifiPwd", "");
    }

    g_prefs.end();
}

static void applyEthConfig() {
    if (g_ethConfig.valid) {
        Ethernet.begin(
            g_macAddress,
            g_ethConfig.ip,
            g_ethConfig.dns,
            g_ethConfig.gateway,
            g_ethConfig.subnet
        );
    } else {
        Ethernet.begin(g_macAddress);
    }
}

static void startWebSocketServer() {
    g_webSocket.begin(g_wsPort);
    g_webSocket.onEvent(handleWebSocketMessage);
}

static void handleWebSocketLoop() {
    g_webSocket.loop();
}

static void handleWebSocketMessage(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    if (type != WStype_TEXT) {
        return;
    }

    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err != DeserializationError::Ok) {
        DynamicJsonDocument resp(256);
        resp["status"] = "error";
        resp["message"] = "invalid_json";
        String out;
        serializeJson(resp, out);
        g_webSocket.sendTXT(num, out);
        return;
    }

    if (doc.containsKey("type")) {
        const char* t = doc["type"];
        if (strcmp(t, "config") == 0) {
            handleConfigMessage(doc);

            DynamicJsonDocument resp(256);
            resp["status"] = "ok";
            resp["type"] = "config";
            String out;
            serializeJson(resp, out);
            g_webSocket.sendTXT(num, out);
            return;
        }
    }

    if (doc.containsKey("transport")) {
        DynamicJsonDocument resp(512);
        bool ok = false;

        const char* transport = doc["transport"];
        if (strcmp(transport, "tcp") == 0) {
            ok = modbusTcpForward(doc, resp);
        } else if (strcmp(transport, "rtu") == 0) {
            ok = modbusRtuForward(doc, resp);
        }

        resp["status"] = ok ? "ok" : "error";
        resp["transport"] = transport;

        String out;
        serializeJson(resp, out);
        g_webSocket.sendTXT(num, out);
    }
}

static void handleConfigMessage(JsonDocument& doc) {
    bool needSave = false;

    if (doc.containsKey("wsPort")) {
        g_wsPort = static_cast<uint16_t>(doc["wsPort"].as<uint16_t>());
        needSave = true;
    }

    if (doc.containsKey("eth")) {
        JsonObject eth = doc["eth"].as<JsonObject>();

        EthStaticConfig newCfg = {};
        newCfg.valid = true;

        const char* ipStr   = eth["ip"]   | "";
        const char* maskStr = eth["mask"] | "";
        const char* gwStr   = eth["gateway"] | "0.0.0.0";
        const char* dnsStr  = eth["dns"] | "0.0.0.0";

        if (parseIpAddress(ipStr, newCfg.ip) && parseIpAddress(maskStr, newCfg.subnet)) {
            parseIpAddress(gwStr, newCfg.gateway);
            parseIpAddress(dnsStr, newCfg.dns);
            g_ethConfig = newCfg;
            applyEthConfig();
            needSave = true;
        }
    }

    if (doc.containsKey("wifi")) {
        JsonObject wifi = doc["wifi"].as<JsonObject>();
        const char* ssid = wifi["ssid"] | "";
        const char* pwd  = wifi["password"] | "";

        if (strlen(ssid) > 0) {
            g_wifiStaConfig.valid = true;
            g_wifiStaConfig.ssid = ssid;
            g_wifiStaConfig.password = pwd;
            needSave = true;
            g_needRestart = true;
        }
    }

    if (needSave) {
        g_prefs.begin("anyport", false);
        g_prefs.putUShort("wsPort", g_wsPort);

        if (g_ethConfig.valid) {
            g_prefs.putUInt("ethIp",   static_cast<uint32_t>(g_ethConfig.ip));
            g_prefs.putUInt("ethMask", static_cast<uint32_t>(g_ethConfig.subnet));
            g_prefs.putUInt("ethGw",   static_cast<uint32_t>(g_ethConfig.gateway));
            g_prefs.putUInt("ethDns",  static_cast<uint32_t>(g_ethConfig.dns));
        }

        if (g_wifiStaConfig.valid) {
            g_prefs.putString("wifiSsid", g_wifiStaConfig.ssid);
            g_prefs.putString("wifiPwd",  g_wifiStaConfig.password);
        }

        g_prefs.end();
    }
}

static bool parseIpAddress(const char* str, IPAddress& outIp) {
    int parts[4] = {0, 0, 0, 0};
    int count = sscanf(str, "%d.%d.%d.%d", &parts[0], &parts[1], &parts[2], &parts[3]);
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

static bool hexStringToBytes(const char* hex, uint8_t* buffer, size_t bufferSize, size_t& outLength) {
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
        char low  = hex[i * 2 + 1];

        uint8_t h = (high >= '0' && high <= '9') ? high - '0' :
                    (high >= 'A' && high <= 'F') ? high - 'A' + 10 :
                    (high >= 'a' && high <= 'f') ? high - 'a' + 10 : 0xFF;
        uint8_t l = (low  >= '0' && low  <= '9') ? low  - '0' :
                    (low  >= 'A' && low  <= 'F') ? low  - 'A' + 10 :
                    (low  >= 'a' && low  <= 'f') ? low  - 'a' + 10 : 0xFF;

        if (h == 0xFF || l == 0xFF) {
            return false;
        }

        buffer[i] = (h << 4) | l;
    }

    outLength = byteCount;
    return true;
}

static String bytesToHexString(const uint8_t* data, size_t length) {
    String result;
    static const char* hexDigits = "0123456789ABCDEF";
    result.reserve(length * 2);
    for (size_t i = 0; i < length; ++i) {
        uint8_t b = data[i];
        result += hexDigits[b >> 4];
        result += hexDigits[b & 0x0F];
    }
    return result;
}

static bool modbusTcpForward(const JsonDocument& doc, JsonDocument& resp) {
    if (!doc.containsKey("tcpTarget") || !doc.containsKey("hex")) {
        resp["message"] = "missing_tcp_target_or_hex";
        return false;
    }

    JsonObject tcp = doc["tcpTarget"].as<JsonObject>();
    const char* ipStr = tcp["ip"] | "";
    uint16_t port = tcp["port"] | 502;

    IPAddress targetIp;
    if (!parseIpAddress(ipStr, targetIp)) {
        resp["message"] = "invalid_tcp_ip";
        return false;
    }

    const char* hex = doc["hex"] | "";
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

static bool modbusRtuForward(const JsonDocument& doc, JsonDocument& resp) {
    if (!doc.containsKey("hex")) {
        resp["message"] = "missing_hex";
        return false;
    }

    if (doc.containsKey("rtuTarget")) {
        JsonObject rtu = doc["rtuTarget"].as<JsonObject>();
        uint32_t baud = rtu["baudRate"] | RS485_DEFAULT_BAUDRATE;
        uint8_t  stopBits = rtu["stopBits"] | RS485_DEFAULT_STOPBITS;
        const char* parityStr = rtu["parity"] | "none";

        uint32_t config = SERIAL_8N1;
        if (strcmp(parityStr, "even") == 0) {
            config = SERIAL_8E1;
        } else if (strcmp(parityStr, "odd") == 0) {
            config = SERIAL_8O1;
        }

        RS485Serial.updateBaudRate(baud);
        (void)stopBits;
        (void)config;
    }

    const char* hex = doc["hex"] | "";
    size_t length = 0;
    if (!hexStringToBytes(hex, g_rtuRxBuffer, sizeof(g_rtuRxBuffer), length)) {
        resp["message"] = "invalid_hex";
        return false;
    }

    RS485Serial.write(g_rtuRxBuffer, length);
    RS485Serial.flush();

    g_rtuRxLength = 0;
    unsigned long start = millis();
    const unsigned long frameTimeoutMs = 50;
    const unsigned long overallTimeoutMs = 1000;
    unsigned long lastByteTime = millis();

    while ((millis() - start) < overallTimeoutMs) {
        while (RS485Serial.available() > 0 && g_rtuRxLength < sizeof(g_rtuRxBuffer)) {
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
        return false;
    }

    resp["hex"] = bytesToHexString(g_rtuRxBuffer, g_rtuRxLength);
    resp["length"] = static_cast<uint32_t>(g_rtuRxLength);
    return true;
}
