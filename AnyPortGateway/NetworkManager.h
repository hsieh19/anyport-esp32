#pragma once

#include "Globals.h"

// -----------------------
// 1. 辅助函数
// -----------------------
inline bool parseIpAddress(const char *str, IPAddress &outIp) {
    int parts[4] = {0, 0, 0, 0};
    int count = sscanf(str, "%d.%d.%d.%d", &parts[0], &parts[1], &parts[2], &parts[3]);
    if (count != 4) return false;
    for (int i = 0; i < 4; ++i) {
        if (parts[i] < 0 || parts[i] > 255) return false;
    }
    outIp = IPAddress(parts[0], parts[1], parts[2], parts[3]);
    return true;
}

// -----------------------
// 2. 内部逻辑实现
// -----------------------

static void applyEthConfig() {
    if (!g_ethConfig.valid) {
        return;
    }
    WiFi.macAddress(g_macAddress);
    g_macAddress[0] = (g_macAddress[0] & 0xFC) | 0x02;

    Ethernet.begin(g_macAddress, g_ethConfig.ip, g_ethConfig.dns,
                   g_ethConfig.gateway, g_ethConfig.subnet);
    APP_PRINT("[W5500] Static IP Fixed: ");
    APP_PRINTLN(Ethernet.localIP());
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
        const unsigned long timeoutMs = 15000; // 15s 超时确保 DHCP IP
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
        configTime(8 * 3600, 0, g_ntpConfig.server.c_str(), "pool.ntp.org", "time.nist.gov");
        return; 
    }

    APP_PRINTLN("[NTP] Initializing time sync...");
    configTime(8 * 3600, 0, g_ntpConfig.server.c_str(), "pool.ntp.org", "time.nist.gov");

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
    pinMode(PIN_W5500_CS, OUTPUT);
    digitalWrite(PIN_W5500_CS, HIGH);
    pinMode(PIN_W5500_RST, OUTPUT);
    digitalWrite(PIN_W5500_RST, HIGH);
    pinMode(PIN_W5500_INT, INPUT_PULLUP);
    SPI.begin(PIN_W5500_SCK, PIN_W5500_MISO, PIN_W5500_MOSI, -1);

    pinMode(PIN_W5500_RST, OUTPUT);
    digitalWrite(PIN_W5500_RST, LOW);
    delay(10);
    digitalWrite(PIN_W5500_RST, HIGH);
    delay(200);

    Ethernet.init(static_cast<uint8_t>(PIN_W5500_CS));
    applyEthConfig();

    uint8_t ver = Ethernet.hardwareStatus();
    APP_PRINT("[Init] Ethernet lib hardware status: ");
    APP_PRINTLN(ver);
}

static void initMdns() {
    if (g_mdnsName.length() == 0) g_mdnsName = MDNS_DEFAULT_NAME;
    
    if (MDNS.begin(g_mdnsName.c_str())) {
        APP_PRINT("[mDNS] Started, host: ");
        APP_PRINT(g_mdnsName);
        APP_PRINTLN(".local");
        MDNS.addService("http", "tcp", 80);
    } else {
        APP_PRINTLN("[mDNS] Start FAILED");
    }
}
