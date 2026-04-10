#pragma once

#include "Globals.h"

// -----------------------
// 1. 辅助函数
// -----------------------
inline bool parseIpAddress(const char *str, IPAddress &outIp) {
  int parts[4] = {0, 0, 0, 0};
  int count =
      sscanf(str, "%d.%d.%d.%d", &parts[0], &parts[1], &parts[2], &parts[3]);
  if (count != 4)
    return false;
  for (int i = 0; i < 4; ++i) {
    if (parts[i] < 0 || parts[i] > 255)
      return false;
  }
  outIp = IPAddress(parts[0], parts[1], parts[2], parts[3]);
  return true;
}

// -----------------------
// 2. 内部逻辑实现
// -----------------------

/**
 * @brief 强制 W5500 PHY 软复位，触发重新 Auto-Negotiation
 *
 * W5500 PHYCFGR 寄存器 (地址 0x002E, Common Register Block):
 *   Bit 7: RST   - 0=触发复位, 1=正常运行
 *   Bit 6: OPMD  - 0=硬件配置, 1=软件配置
 *   Bit 5-3: OPMDC - 111=全能力自动协商 (默认)
 *   Bit 2: DPX   - 全双工状态 (只读)
 *   Bit 1: SPD   - 速率状态 (只读)
 *   Bit 0: LNK   - 链路状态 (只读)
 *
 * Arduino Ethernet 库的 W5100.init() 和 softReset() 都不操作 PHYCFGR，
 * 导致 PHY 可能因初始化时序中的 SPI 毛刺处于异常状态。
 * 手动写入 PHYCFGR 可强制 PHY 干净地重启 auto-negotiation。
 */
/**
 * @brief 写入 W5500 PHYCFGR 寄存器
 *
 * PHYCFGR (0x002E) 格式:
 *   Bit 7:   RST   - 0=触发PHY复位, 1=正常运行
 *   Bit 6:   OPMD  - 0=硬件引脚配置, 1=软件寄存器配置
 *   Bit 5-3: OPMDC - PHY 工作模式:
 *     111 = 全能力 Auto-Negotiation (默认)
 *     011 = 100Base-TX Full Duplex (强制，无协商)
 *     010 = 100Base-TX Half Duplex (强制)
 *     001 = 10Base-T Full Duplex (强制)
 *     000 = 10Base-T Half Duplex (强制)
 *   Bit 2: DPX (只读), Bit 1: SPD (只读), Bit 0: LNK (只读)
 */
static void writePhyCfgr(uint8_t value) {
  uint8_t cmd[3] = {0x00, 0x2E, 0x04}; // Addr=0x002E, Ctrl=Write Common Reg
  digitalWrite(PIN_W5500_CS, LOW);
  SPI.transfer(cmd, 3);
  SPI.transfer(value);
  digitalWrite(PIN_W5500_CS, HIGH);
}

static uint8_t readPhyCfgr() {
  uint8_t cmd[3] = {0x00, 0x2E, 0x00}; // Addr=0x002E, Ctrl=Read Common Reg
  digitalWrite(PIN_W5500_CS, LOW);
  SPI.transfer(cmd, 3);
  uint8_t val = SPI.transfer(0x00);
  digitalWrite(PIN_W5500_CS, HIGH);
  return val;
}

/**
 * @brief 对 W5500 PHY 执行软复位并设置指定的工作模式
 * @param opmdc        PHY 模式 (3 bits): 0b111=Auto-Negotiation, 0b011=100M FD
 * @param hardwareMode true=OPMD=0 (硬件引脚配置, 保留 Auto-MDIX)
 *                     false=OPMD=1 (软件寄存器配置, Auto-MDIX 可能不工作)
 *
 * 关键：许多 W5500 模块在 OPMD=1 时 Auto-MDIX 不生效，
 * 导致两个 W5500 直连时因 MDI 极性相同而无法建立物理链路。
 * 标准 Auto-Negotiation 应使用 OPMD=0 保留硬件 Auto-MDIX。
 * 仅在需要强制指定速率/双工（绕过缺陷模块）时使用 OPMD=1。
 */
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

/**
 * @brief 等待以太网链路建立
 * @param timeoutMs 超时时间 (毫秒)
 * @return true=链路已建立, false=超时
 */
static bool waitForLink(unsigned long timeoutMs) {
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (Ethernet.linkStatus() == LinkON)
      return true;
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

static void initWifi() {
  WiFi.persistent(false);
  WiFi.disconnect(true);
  delay(100);

  if (g_wifiStaConfig.valid) {
    WiFi.mode(WIFI_STA);
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

static void syncNtpTime() {
  if (g_workMode == WorkMode::TRANSPARENT) {
    configTime(8 * 3600, 0, g_ntpConfig.server.c_str(), "pool.ntp.org",
               "time.nist.gov");
    return;
  }

  APP_PRINTLN("[NTP] Initializing time sync...");
  configTime(8 * 3600, 0, g_ntpConfig.server.c_str(), "pool.ntp.org",
             "time.nist.gov");

  if (WiFi.status() != WL_CONNECTED) {
    APP_PRINTLN("[NTP] WiFi not connected, skip sync");
    return;
  }

  unsigned long start = millis();
  bool synced = false;
  Serial.print("[NTP] Waiting for sync");
  while (millis() - start < 3000) {
    time_t now = time(nullptr);
    if (now > 1000000000L) {
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
    APP_PRINT("[NTP] System time synced: ");
    APP_PRINTLN(timeStr);
  } else {
    APP_PRINTLN("[NTP] Sync FAILED (timeout), proceeding with default time");
  }
}

static void initSpiAndEthernet() {
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
  // 必须先调用 applyEthConfig (内部执行 Ethernet.begin)
  // 因为 hardwareStatus() 依赖 Ethernet.begin 触发的 softReset
  applyEthConfig();

  uint8_t hwStatus = Ethernet.hardwareStatus();
  if (hwStatus == EthernetNoHardware) {
    APP_PRINTLN("[W5500] No hardware detected. Skipping Ethernet.");
    return;
  }
  APP_LOG("[W5500] Hardware detected (chip: %d)", hwStatus);
  APP_LOG("[W5500] IP: %s", Ethernet.localIP().toString().c_str());

  // === 步骤 4: MAC 地址抖动 ===
  // 两台 W5500 同时上电时制造确定性启动偏差
  unsigned long jitter = (((unsigned long)g_macAddress[4] << 8) | g_macAddress[5]) % 501;
  APP_LOG("[W5500] PHY init jitter: %lu ms", jitter);
  if (jitter > 0) delay(jitter);

  // 读取当前 PHYCFGR 状态（诊断用）
  uint8_t phyCfg = readPhyCfgr();
  APP_LOG("[W5500] PHYCFGR after init: 0x%02X", phyCfg);

  // === 步骤 5: 多轮自适应链路建立 ===
  //
  // Round 0: 不碰 PHYCFGR，使用硬件复位后的自然状态
  // Round 1: 通过 PHYCFGR 显式触发 PHY 软复位 (OPMD=0)
  // Round 2: 强制 100M Full Duplex (OPMD=1, OPMDC=011)
  //   给有缺陷的、无法自动协商的模块使用
  // Round 3: 恢复硬件模式（给对端设备第二次机会）

  // --- Round 0: 自然建链（最小干预）---
  APP_PRINTLN("[W5500] Round 0: Waiting for natural link...");
  if (waitForLink(4000)) {
    APP_PRINTLN("[W5500] Link UP (natural)");
    return;
  }

  // --- Round 1: PHY 软复位（硬件模式）---
  APP_PRINTLN("[W5500] Round 1: PHY reset (HW mode)...");
  resetW5500Phy(0x07, true);
  if (waitForLink(3000)) {
    APP_PRINTLN("[W5500] Link UP (HW mode PHY reset)");
    return;
  }

  // --- Round 2: 强制 100M FD（软件模式）---
  APP_PRINTLN("[W5500] Round 2: Forced 100M FD (SW mode)...");
  resetW5500Phy(0x03, false);
  if (waitForLink(2000)) {
    APP_PRINTLN("[W5500] Link UP (Forced 100M FD)");
    return;
  }

  // --- Round 3: 恢复硬件模式（给对端设备第二次机会）---
  APP_PRINTLN("[W5500] Round 3: Back to HW mode...");
  resetW5500Phy(0x07, true);
  if (waitForLink(3000)) {
    APP_PRINTLN("[W5500] Link UP (HW mode, retry)");
    return;
  }

  APP_PRINTLN("[W5500] Link DOWN (no peer detected)");
}

static void initMdns() {
  if (g_mdnsName.length() == 0)
    g_mdnsName = MDNS_DEFAULT_NAME;

  if (MDNS.begin(g_mdnsName.c_str())) {
    APP_PRINT("[mDNS] Started, host: ");
    APP_PRINT(g_mdnsName);
    APP_PRINTLN(".local");
    MDNS.addService("http", "tcp", 80);
  } else {
    APP_PRINTLN("[mDNS] Start FAILED");
  }
}
