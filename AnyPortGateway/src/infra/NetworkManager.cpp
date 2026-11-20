#include "NetworkManager.h"

uint8_t g_macAddress[6];

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
  WiFi.persistent(false);
  WiFi.disconnect(true);
  delay(100);

  if (g_wifiStaConfig.valid) {
    WiFi.mode(WIFI_STA);
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
      APP_PRINTLN("WiFi Timed out, using AP mode");
      WiFi.disconnect(true);
      delay(200);
      WiFi.mode(WIFI_AP);
      delay(100);
      WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
    }
  } else {
    WiFi.mode(WIFI_AP);
    delay(100);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  }

  if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) {
    APP_PRINT("WiFi STA connected, IP: ");
    APP_PRINTLN(WiFi.localIP());
  } else {
    APP_PRINT("WiFi AP mode started, SSID: ");
    APP_PRINTLN(WIFI_AP_SSID);
    APP_PRINT("AP IP: ");
    APP_PRINTLN(WiFi.softAPIP());
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
