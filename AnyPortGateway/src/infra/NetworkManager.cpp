#include "NetworkManager.h"
#include <esp_wifi.h>

uint8_t g_macAddress[6];

static bool s_tempApActive = false;
static unsigned long s_tempApIdleSinceMs = 0;
static const unsigned long TEMP_AP_IDLE_TIMEOUT_MS = 180000UL; // 无客户端连接持续 3 分钟后关闭临时 AP
static unsigned long s_lastReconnectAttemptMs = 0;
static const unsigned long WIFI_RECONNECT_INTERVAL_MS = 20000UL; // STA 断线后台重连间隔 (20s)

// BOOT 键 (GPIO 9) 非阻塞消抖状态
static bool s_bootKeyPressed = false;
static bool s_bootKeyHandled = false;
static unsigned long s_bootKeyDownMs = 0;
static const unsigned long BOOT_KEY_DEBOUNCE_MS = 80UL;

// 统一应用工业级 WiFi 射频与低延迟优化：
// 1. 彻底关闭 Modem Sleep 省电休眠 (WIFI_PS_NONE)，消除数千毫秒的 DTIM 唤醒延迟与弱信号丢包
// 2. 锁定最大发射功率 19.5dBm，增强弱信号边缘上行回传穿透力
// 3. 锁定 20MHz 窄频宽 (HT20)，降低 3dB 底噪并提升工业现场抗邻频干扰与接收灵敏度
static void applyWifiRfOptimizations() {
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
}

static void enableTempMaintenanceAp(const char *reason) {
  IPAddress staIp = WiFi.localIP();
  if (staIp[0] == 192 && staIp[1] == 168 && staIp[2] == 4) {
    APP_PRINTLN("[WiFi] Warning: STA subnet (192.168.4.x) conflicts with SoftAP, skipping temporary AP");
    s_tempApActive = false;
    return;
  }

  if (!s_tempApActive) {
    WiFi.mode(WIFI_AP_STA);
    applyWifiRfOptimizations();
    delay(50);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
    s_tempApActive = true;
  }
  s_tempApIdleSinceMs = millis();
  APP_LOG("[WiFi] Temporary maintenance AP active (%s, SSID: %s, AP IP: %s, STA IP: %s, auto-off after 180s idle)",
          reason, WIFI_AP_SSID, WiFi.softAPIP().toString().c_str(),
          staIp.toString().c_str());
}

// -----------------------
// 1. W5500 PHYCFGR 寄存器底层操作 (0x002E)
// -----------------------
static void writePhyCfgr(uint8_t value) {
  uint8_t cmd[3] = {0x00, 0x2E, 0x04};
  digitalWrite(PIN_W5500_CS, LOW);
  SPI.transfer(cmd, 3);
  SPI.transfer(value);
  digitalWrite(PIN_W5500_CS, HIGH);
}

static uint8_t readPhyCfgr() {
  uint8_t cmd[3] = {0x00, 0x2E, 0x00};
  digitalWrite(PIN_W5500_CS, LOW);
  SPI.transfer(cmd, 3);
  uint8_t val = SPI.transfer(0x00);
  digitalWrite(PIN_W5500_CS, HIGH);
  return val;
}

static void resetW5500Phy(uint8_t opmdc, bool hardwareMode) {
  uint8_t before = readPhyCfgr();
  APP_LOG("[W5500] PHYCFGR before: 0x%02X", before);

  uint8_t opmd = hardwareMode ? 0 : 1;

  // Step 1: 触发 PHY 复位 (RST=0)
  uint8_t resetVal = (0 << 7) | (opmd << 6) | ((opmdc & 0x07) << 3);
  writePhyCfgr(resetVal);
  delay(50);

  // Step 2: 释放复位 (RST=1)
  uint8_t normalVal = (1 << 7) | (opmd << 6) | ((opmdc & 0x07) << 3);
  writePhyCfgr(normalVal);
  delay(10);

  uint8_t after = readPhyCfgr();
  APP_LOG("[W5500] PHYCFGR after:  0x%02X (OPMD=%d, mode: %s)", after, opmd,
          opmdc == 0x07 ? "Auto-Negotiation" : "Forced 100M FD");
}

static bool waitForLink(unsigned long timeoutMs) {
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (Ethernet.linkStatus() == LinkON) return true;
    delay(100);
  }
  return false;
}

static void applyEthConfig() {
  WiFi.macAddress(g_macAddress);
  g_macAddress[0] = (g_macAddress[0] & 0xFC) | 0x02;

  if (g_ethConfig.valid) {
    Ethernet.begin(g_macAddress, g_ethConfig.ip, g_ethConfig.dns,
                   g_ethConfig.gateway, g_ethConfig.subnet);
    APP_PRINT("[W5500] Static IP: ");
    APP_PRINTLN(Ethernet.localIP());
  } else {
    Ethernet.begin(g_macAddress, IPAddress(0, 0, 0, 0));
    APP_PRINTLN("[W5500] Initialized (No Static IP configured)");
  }
}

// -----------------------
// 2. WiFi / NTP / W5500 初始化接口实现
// -----------------------
void initWifi() {
  pinMode(PIN_BOOT_KEY, INPUT_PULLUP);
  WiFi.persistent(false);
  WiFi.disconnect(true);
  delay(100);

  if (g_wifiStaConfig.valid) {
    WiFi.mode(WIFI_STA);
    applyWifiRfOptimizations();
    // 全信道扫描同名 SSID 并按最强信号 (RSSI) 择优接入，避免多 AP 环境下误连远端弱信号节点
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    WiFi.setAutoReconnect(true);

    if (g_wifiStaConfig.staticIp &&
        static_cast<uint32_t>(g_wifiStaConfig.ip) != 0) {
      WiFi.config(g_wifiStaConfig.ip, g_wifiStaConfig.gateway,
                  g_wifiStaConfig.subnet, g_wifiStaConfig.dns);
      APP_PRINT("[WiFi] Static IP configured: ");
      APP_PRINTLN(g_wifiStaConfig.ip);
    }
    APP_PRINT("WiFi Connecting to: ");
    APP_PRINTLN(g_wifiStaConfig.ssid);
    WiFi.begin(g_wifiStaConfig.ssid.c_str(), g_wifiStaConfig.password.c_str());

    unsigned long start = millis();
    const unsigned long timeoutMs = 15000;
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
      delay(100);
    }

    if (WiFi.status() != WL_CONNECTED) {
      APP_PRINTLN("WiFi Timed out, using AP+STA mode with background retry");
      WiFi.mode(WIFI_AP_STA);
      applyWifiRfOptimizations();
      delay(100);
      WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
      s_tempApActive = false;
      s_lastReconnectAttemptMs = millis();
    } else {
      IPAddress staIp = WiFi.localIP();
      APP_LOG("[WiFi] STA connected, IP: %s, BSSID: %s, RSSI: %d dBm",
              staIp.toString().c_str(), WiFi.BSSIDstr().c_str(), WiFi.RSSI());

      // 无论 DHCP 还是静态 IP，每次开机联网后均开启 3 分钟临时维护 AP，防止 IP 漂移、遗忘或静态 IP 配错失联
      enableTempMaintenanceAp("boot");
    }
  } else {
    WiFi.mode(WIFI_AP);
    applyWifiRfOptimizations();
    delay(100);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
    s_tempApActive = false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    APP_PRINT("WiFi AP mode started, SSID: ");
    APP_PRINTLN(WIFI_AP_SSID);
    APP_PRINT("AP IP: ");
    APP_PRINTLN(WiFi.softAPIP());
  }
}

void networkLoop() {
  unsigned long now = millis();

  // 1. 非阻塞轮询板载 BOOT 键 (GPIO 9，按下为 LOW)，80ms 消抖唤醒/续期 3 分钟临时维护 AP
  bool isLow = (digitalRead(PIN_BOOT_KEY) == LOW);
  if (isLow) {
    if (!s_bootKeyPressed) {
      s_bootKeyPressed = true;
      s_bootKeyHandled = false;
      s_bootKeyDownMs = now;
    } else if (!s_bootKeyHandled && (now - s_bootKeyDownMs >= BOOT_KEY_DEBOUNCE_MS)) {
      s_bootKeyHandled = true;
      if (WiFi.status() == WL_CONNECTED) {
        enableTempMaintenanceAp("BOOT key wakeup");
      }
    }
  } else {
    s_bootKeyPressed = false;
    s_bootKeyHandled = false;
  }

  // 2. STA 异常掉线后台自动重连兜底巡检 (无 AP 客户端正在配网时每 20s 触发一次重连)
  if (g_wifiStaConfig.valid && WiFi.status() != WL_CONNECTED) {
    if (now - s_lastReconnectAttemptMs >= WIFI_RECONNECT_INTERVAL_MS) {
      s_lastReconnectAttemptMs = now;
      if (WiFi.softAPgetStationNum() == 0) {
        APP_PRINTLN("[WiFi] STA disconnected, triggering background reconnect...");
        WiFi.reconnect();
      }
    }
  }

  // 3. 临时维护 AP 空闲超时自毁巡检
  if (!s_tempApActive) return;

  // 仅在 STA 已连网且 AP 无客户端接入时累计空闲时间；有客户端连接时自动顺延
  if (WiFi.status() != WL_CONNECTED || WiFi.softAPgetStationNum() > 0) {
    s_tempApIdleSinceMs = now;
    return;
  }

  if (now - s_tempApIdleSinceMs >= TEMP_AP_IDLE_TIMEOUT_MS) {
    APP_PRINTLN("[WiFi] Temporary maintenance AP idle for 180s, shutting down SoftAP...");
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    applyWifiRfOptimizations();
    s_tempApActive = false;
  }
}

void initSpiAndEthernet() {
  // === 步骤 1: 硬件复位 ===
  pinMode(PIN_W5500_CS, OUTPUT);
  digitalWrite(PIN_W5500_CS, HIGH);
  pinMode(PIN_W5500_RST, OUTPUT);

  digitalWrite(PIN_W5500_RST, LOW);
  delay(50);
  digitalWrite(PIN_W5500_RST, HIGH);
  delay(300);

  // === 步骤 2: 启动 SPI ===
  pinMode(PIN_W5500_INT, INPUT_PULLUP);
  SPI.begin(PIN_W5500_SCK, PIN_W5500_MISO, PIN_W5500_MOSI, -1);
  Ethernet.init(static_cast<uint8_t>(PIN_W5500_CS));

  // === 步骤 3: 初始化 MAC/IP ===
  applyEthConfig();

  uint8_t hwStatus = Ethernet.hardwareStatus();
  if (hwStatus == EthernetNoHardware) {
    APP_PRINTLN("[W5500] No hardware detected. Skipping Ethernet.");
    return;
  }
  APP_LOG("[W5500] Hardware detected (chip: %d)", hwStatus);
  APP_LOG("[W5500] IP: %s", Ethernet.localIP().toString().c_str());

  // === 步骤 4: MAC 地址抖动 ===
  unsigned long jitter =
      (((unsigned long)g_macAddress[4] << 8) | g_macAddress[5]) % 501;
  APP_LOG("[W5500] PHY init jitter: %lu ms", jitter);
  if (jitter > 0) delay(jitter);

  uint8_t phyCfg = readPhyCfgr();
  APP_LOG("[W5500] PHYCFGR after init: 0x%02X", phyCfg);

  // === 步骤 5: 多轮自适应链路建立 ===
  APP_PRINTLN("[W5500] Round 0: Waiting for natural link...");
  if (waitForLink(4000)) {
    APP_PRINTLN("[W5500] Link UP (natural)");
    return;
  }

  APP_PRINTLN("[W5500] Round 1: PHY reset (HW mode)...");
  resetW5500Phy(0x07, true);
  if (waitForLink(3000)) {
    APP_PRINTLN("[W5500] Link UP (HW mode PHY reset)");
    return;
  }

  APP_PRINTLN("[W5500] Round 2: Forced 100M FD (SW mode)...");
  resetW5500Phy(0x03, false);
  if (waitForLink(2000)) {
    APP_PRINTLN("[W5500] Link UP (Forced 100M FD)");
    return;
  }

  APP_PRINTLN("[W5500] Round 3: Back to HW mode...");
  resetW5500Phy(0x07, true);
  if (waitForLink(3000)) {
    APP_PRINTLN("[W5500] Link UP (HW mode, retry)");
    return;
  }

  APP_PRINTLN("[W5500] Link DOWN (no peer detected)");
}
