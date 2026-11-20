#include "OtaService.h"
#include "ConfigStore.h"
#include <ArduinoJson.h>
#include <Ethernet.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>

static String s_otaRemoteVersion = "";
static String s_otaChangelog = "";
static String s_otaValidatedUrl = "";
static bool s_otaUpdateFound = false;
static unsigned long s_lastOtaCheck = 0;

const String &otaGetRemoteVersion() { return s_otaRemoteVersion; }
const String &otaGetChangelog() { return s_otaChangelog; }
bool otaIsUpdateFound() { return s_otaUpdateFound; }

void getBackupPartitionInfo(String &label, bool &canRollback) {
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
  canRollback = false;
  label = "";

  if (running && next && running != next) {
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(next, &desc) == ESP_OK) {
      label = configGetPrevFirmwareVersion();
      canRollback = (label.length() > 0);
    }
  }
}

bool executeRollback() {
  const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
  if (!next) {
    APP_PRINTLN("[OTA] Rollback failed: no backup partition found");
    return false;
  }

  esp_app_desc_t desc;
  if (esp_ota_get_partition_description(next, &desc) != ESP_OK) {
    APP_PRINTLN("[OTA] Rollback failed: backup partition has no valid app");
    return false;
  }

  APP_LOG("[OTA] Rolling back to: %s (partition: %s)", desc.version,
          next->label);

  if (esp_ota_set_boot_partition(next) != ESP_OK) {
    APP_PRINTLN("[OTA] Rollback failed: could not set boot partition");
    return false;
  }

  APP_PRINTLN("[OTA] Rollback OK. Rebooting...");
  return true;
}

bool validateFirmwareExistence() {
  if (s_otaRemoteVersion == "") return false;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String checkUrl =
      g_otaApiBase + "/api/ota/check?project=anyport&chip=ESP32C3";

  APP_LOG("[OTA] Refreshing URL: %s", checkUrl.c_str());
  http.begin(client, checkUrl);
  http.setUserAgent("Mozilla/5.0 AnyPort-Validator");
  int httpCode = http.GET();

  bool success = false;
  if (httpCode == 200) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, http.getString());
    s_otaValidatedUrl = doc["url"] | "";
    if (s_otaValidatedUrl != "") success = true;
  } else {
    APP_LOG("[OTA] Validation failed, HTTP Code: %d", httpCode);
  }
  http.end();
  return success;
}

bool isOtaApiConfigured() {
  if (g_otaApiBase.length() == 0 || !g_otaApiBase.startsWith("http")) {
    return false;
  }
  if (g_otaApiBase.indexOf("your-firmware-worker.workers.dev") >= 0) {
    return false;
  }
  return true;
}

int checkOtaUpdate() {
  if (!isOtaApiConfigured()) return -3;
  if (WiFi.status() != WL_CONNECTED && Ethernet.linkStatus() != LinkON)
    return -2;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String checkUrl =
      g_otaApiBase + "/api/ota/check?project=anyport&chip=ESP32C3";

  if (!http.begin(client, checkUrl)) return -1;
  http.setUserAgent("Mozilla/5.0 AnyPort-Collector/" FIRMWARE_VERSION);
  int httpCode = http.GET();

  if (httpCode == 200) {
    DynamicJsonDocument doc(1024);
    DeserializationError err = deserializeJson(doc, http.getString());
    http.end();
    if (err || !doc.containsKey("version")) {
      return -1;
    }
    s_otaRemoteVersion = doc["version"] | "";
    s_otaChangelog = doc["changelog"] | "";
    s_otaValidatedUrl = doc["url"] | "";
    if (s_otaRemoteVersion == "") {
      return -1;
    }
    if (s_otaRemoteVersion != FIRMWARE_VERSION) {
      s_otaUpdateFound = true;
      return 1;
    }
    s_otaUpdateFound = false;
    return 0;
  }
  http.end();
  return -1;
}

void otaAutoCheckLoop() {
  if (!isOtaApiConfigured()) return;
  static bool firstCheckDone = false;
  unsigned long now = millis();
  if (!firstCheckDone && now > 30000) {
    if (WiFi.status() == WL_CONNECTED || Ethernet.linkStatus() == LinkON) {
      checkOtaUpdate();
      s_lastOtaCheck = now;
      firstCheckDone = true;
    }
  }
  if (now - s_lastOtaCheck > 86400000UL) {
    if (WiFi.status() == WL_CONNECTED || Ethernet.linkStatus() == LinkON) {
      checkOtaUpdate();
      s_lastOtaCheck = now;
    }
  }
}

void executeFirmwareUpdate() {
  if (s_otaValidatedUrl == "") return;

  // 升级前保存当前版本号到 NVS，供回滚时显示
  configSavePrevFirmwareVersion(FIRMWARE_VERSION);

  WiFiClientSecure client;
  client.setInsecure();

  APP_LOG("[OTA] Starting Update with validated URL...");

  httpUpdate.onProgress([](size_t progress, size_t total) {
    if (g_workMode != WorkMode::TRANSPARENT) {
      Serial.printf("[OTA] Updating: %d%%\r", (progress * 100) / total);
    }
  });

  t_httpUpdate_return ret = httpUpdate.update(client, s_otaValidatedUrl);
  if (ret == HTTP_UPDATE_FAILED) {
    APP_LOG("[OTA] Fatal Error: %s", httpUpdate.getLastErrorString().c_str());
    delay(3000);
  }
  ESP.restart();
}
