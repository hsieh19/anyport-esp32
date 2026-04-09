#pragma once

#include "Globals.h"
#include <HTTPClient.h>
#include <HTTPUpdate.h>

// 你的 Cloudflare Worker API 地址
#define OTA_API_BASE "https://update.anyport.one/anyport"

static String g_otaRemoteVersion = "";
static String g_otaChangelog = "";
static String g_otaValidatedUrl = ""; // 经过预检的有效下载地址
static bool g_otaUpdateFound = false;
static unsigned long g_lastOtaCheck = 0;

/**
 * @brief 内部函数：获取并校验下载链接
 * @return true: 链接有效, false: 文件不存在或获取失败
 */
static bool validateFirmwareExistence() {
    if (g_otaRemoteVersion == "") return false;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String getLinkUrl = String(OTA_API_BASE) + "/get-link?ver=" + g_otaRemoteVersion;
    
    APP_LOG("[OTA] Validating existence: %s", getLinkUrl.c_str());
    http.begin(client, getLinkUrl);
    http.setUserAgent("Mozilla/5.0 AnyPort-Validator");
    int httpCode = http.GET();
    
    bool success = false;
    if (httpCode == 200) {
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, http.getString());
        g_otaValidatedUrl = doc["url"] | ""; 
        if (g_otaValidatedUrl != "") success = true;
    } else {
        APP_LOG("[OTA] Validation failed, HTTP Code: %d", httpCode);
    }
    http.end();
    return success;
}

/**
 * @brief 步骤 1: 检查是否存在新版本
 */
static int checkOtaUpdate() {
    if (WiFi.status() != WL_CONNECTED && Ethernet.linkStatus() != LinkON) return -2;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String checkUrl = String(OTA_API_BASE) + "/check";

    if (!http.begin(client, checkUrl)) return -1;
    http.setUserAgent("Mozilla/5.0 AnyPort-Collector/" FIRMWARE_VERSION);
    int httpCode = http.GET();

    if (httpCode == 200) {
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, http.getString());
        g_otaRemoteVersion = doc["version"] | "";
        g_otaChangelog = doc["changelog"] | "";
        if (g_otaRemoteVersion != "" && g_otaRemoteVersion != FIRMWARE_VERSION) {
            g_otaUpdateFound = true;
            return 1;
        }
        g_otaUpdateFound = false;
        return 0;
    }
    return -1;
}

static void otaAutoCheckLoop() {
    static bool firstCheckDone = false;
    unsigned long now = millis();
    if (!firstCheckDone && now > 30000) {
        if (WiFi.status() == WL_CONNECTED || Ethernet.linkStatus() == LinkON) {
            checkOtaUpdate();
            g_lastOtaCheck = now;
            firstCheckDone = true;
        }
    }
    if (now - g_lastOtaCheck > 86400000) {
        if (WiFi.status() == WL_CONNECTED || Ethernet.linkStatus() == LinkON) {
            checkOtaUpdate();
            g_lastOtaCheck = now;
        }
    }
}

/**
 * @brief 步骤 2: 执行固件更新 (使用已预检的链接)
 */
static void executeFirmwareUpdate() {
    if (g_otaValidatedUrl == "") return;

    WiFiClientSecure client;
    client.setInsecure();

    APP_LOG("[OTA] Starting Update with validated URL...");
    
    httpUpdate.onProgress([](size_t progress, size_t total) {
        if (g_workMode != WorkMode::TRANSPARENT) {
            Serial.printf("[OTA] Updating: %d%%\r", (progress * 100) / total);
        }
    });

    t_httpUpdate_return ret = httpUpdate.update(client, g_otaValidatedUrl);
    if (ret == HTTP_UPDATE_FAILED) {
        APP_LOG("[OTA] Fatal Error: %s", httpUpdate.getLastErrorString().c_str());
        delay(3000);
    }
    ESP.restart();
}
