#pragma once

#include "Globals.h"
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <esp_ota_ops.h>

/**
 * @brief 获取备用分区信息（用于回滚）
 * @param label  输出: 备用分区的固件版本号
 * @param canRollback 输出: 是否可回滚
 * 
 * 注意: Arduino 框架下 esp_app_desc_t.version 返回的是 ESP-IDF SDK 版本，
 * 而非应用版本号。因此我们在每次 OTA 升级前将当前 FIRMWARE_VERSION
 * 存入 NVS (key: "prevFwVer")，回滚时从 NVS 读取。
 */
inline void getBackupPartitionInfo(String &label, bool &canRollback) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
    canRollback = false;
    label = "";

    if (running && next && running != next) {
        // 检查备用分区是否有合法的 app 镜像
        esp_app_desc_t desc;
        if (esp_ota_get_partition_description(next, &desc) == ESP_OK) {
            // 从 NVS 读取升级前保存的版本号
            Preferences p;
            p.begin("anyport", true); // 只读
            label = p.getString("prevFwVer", "");
            p.end();
            canRollback = (label.length() > 0);
        }
    }
}

/**
 * @brief 执行固件回滚 - 切换启动分区到上一个版本
 * @return true=成功(即将重启), false=失败
 */
inline bool executeRollback() {
    const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
    if (!next) {
        APP_PRINTLN("[OTA] Rollback failed: no backup partition found");
        return false;
    }

    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(next, &desc) != ESP_OK) {
        APP_PRINTLN("[OTA] Rollback failed: backup partition has no valid app");
        return false;
    }

    APP_LOG("[OTA] Rolling back to: %s (partition: %s)", desc.version, next->label);

    if (esp_ota_set_boot_partition(next) != ESP_OK) {
        APP_PRINTLN("[OTA] Rollback failed: could not set boot partition");
        return false;
    }

    APP_PRINTLN("[OTA] Rollback OK. Rebooting...");
    return true;
}

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
inline bool validateFirmwareExistence() {
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
inline int checkOtaUpdate() {
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

inline void otaAutoCheckLoop() {
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
inline void executeFirmwareUpdate() {
    if (g_otaValidatedUrl == "") return;

    // 升级前保存当前版本号到 NVS，供回滚时显示
    Preferences p;
    p.begin("anyport", false);
    p.putString("prevFwVer", FIRMWARE_VERSION);
    p.end();

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
