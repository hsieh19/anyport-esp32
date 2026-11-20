#include "SimulatorController.h"
#include "../domain/BusMonitorDomain.h"
#include "../domain/SimulatorDomain.h"
#include "../infra/ConfigStore.h"
#include "HttpApiServer.h"
#include <ArduinoJson.h>

static void handleGetRegisters(WebServer &server) {
  (void)server;
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.to<JsonArray>();
  simSyncValuesFromPool();

  const SimulatorVariable *vars = simGetVariables();
  size_t count = simGetVarCount();

  for (size_t i = 0; i < count; i++) {
    JsonObject obj = arr.createNestedObject();
    obj["addr"] = vars[i].address;
    obj["name"] = vars[i].name;
    obj["type"] = static_cast<int>(vars[i].type);
    obj["endian"] = static_cast<int>(vars[i].endian);
    obj["targetVal"] = vars[i].targetValue;
    obj["val"] = vars[i].currentValue;
    obj["isDyn"] = vars[i].isDynamic;
    obj["min"] = vars[i].dynamicMin;
    obj["max"] = vars[i].dynamicMax;
    obj["dynInterval"] = vars[i].dynInterval;
    obj["dynMode"] = static_cast<int>(vars[i].dynMode);
  }

  String out;
  serializeJson(doc, out);
  apiSendJson(200, out);
}

static void handleUpdateRegisters(WebServer &server) {
  if (!apiRequireJson()) return;
  if (!server.hasArg("plain")) {
    apiError(400, "missing_body");
    return;
  }

  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err || !doc.is<JsonArray>()) {
    Serial.printf("[Web] JSON Parse Error: %s\n", err.c_str());
    apiError(400, "invalid_json");
    return;
  }

  SimulatorVariable *vars = simGetVariables();
  size_t &count = simGetVarCount();

  JsonArray arr = doc.as<JsonArray>();
  count = 0;
  for (JsonVariant v : arr) {
    if (count >= MAX_SIM_VARIABLES) break;

    SimulatorVariable &var = vars[count++];
    var.address = v["addr"] | 40001;
    strlcpy(var.name, v["name"] | "", sizeof(var.name));
    var.type = static_cast<DataType>(v["type"] | 0);
    var.endian = static_cast<Endianness>(v["endian"] | 0);
    var.targetValue = v["targetVal"] | 0.0f;
    var.currentValue = v["val"] | var.targetValue;
    var.isDynamic = v["isDyn"] | false;
    var.dynamicMin = v["min"] | 0.0f;
    var.dynamicMax = v["max"] | 100.0f;
    var.dynInterval = v["dynInterval"] | 1;
    var.dynMode = v["dynMode"] | 0;
    var._lastDynUpdate = 0;

    simWriteValueToPool(var);
  }

  configSaveSimulator();
  apiOk(false);
}

static void handleGetSimConfig(WebServer &server) {
  (void)server;
  const SimulatorGlobalConfig &cfg = simGetConfig();
  StaticJsonDocument<384> doc;
  doc["rtuEnabled"] = cfg.rtuEnabled;
  doc["baud"] = cfg.baud;
  doc["stopBits"] = cfg.stopBits;
  doc["parity"] = cfg.parity;
  doc["dataBits"] = cfg.dataBits;
  doc["unitId"] = cfg.unitId;
  doc["tcpUnitId"] = cfg.tcpUnitId;
  doc["tcpEnabled"] = cfg.tcpEnabled;
  doc["tcpPort"] = cfg.tcpPort;
  doc["netInterface"] = cfg.netInterface;

  String out;
  serializeJson(doc, out);
  apiSendJson(200, out);
}

static void handleUpdateSimConfig(WebServer &server) {
  if (!apiRequireJson()) return;
  if (!server.hasArg("plain")) {
    apiError(400, "missing_body");
    return;
  }
  StaticJsonDocument<384> doc;
  if (deserializeJson(doc, server.arg("plain")) || !doc.is<JsonObject>()) {
    apiError(400, "invalid_json");
    return;
  }

  SimulatorGlobalConfig &cfg = simGetConfig();
  cfg.rtuEnabled = doc["rtuEnabled"] | false;
  cfg.baud = doc["baud"] | 9600;
  cfg.stopBits = doc["stopBits"] | 1;
  cfg.parity = doc["parity"] | 0;
  cfg.dataBits = doc["dataBits"] | 8;
  cfg.unitId = doc["unitId"] | 1;
  cfg.tcpUnitId = doc["tcpUnitId"] | 1;
  cfg.tcpEnabled = doc["tcpEnabled"] | false;
  cfg.tcpPort = doc["tcpPort"] | 502;
  cfg.netInterface = doc["netInterface"] | 0;

  configSaveSimulator();
  apiOk(false);
}

static void handleGetMonitorLogs(WebServer &server) {
  static const char *hexChars = "0123456789ABCDEF";
  server.sendHeader("Cache-Control", "no-cache");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");

  const BusQualityStats &st = busMonitorGetStats();
  const BusMonitorConfig &cfg = busMonitorGetConfig();
  uint32_t avgRtt =
      (st.respCount > 0)
          ? static_cast<uint32_t>(st.totalRttMs / st.respCount)
          : 0;

  String chunk;
  chunk.reserve(1536);

  char headerBuf[320];
  snprintf(headerBuf, sizeof(headerBuf),
           "{\"stats\":{\"totalFrames\":%u,\"reqCount\":%u,\"respCount\":%u,"
           "\"timeoutCount\":%u,\"crcErrorCount\":%u,\"exceptionCount\":%u,"
           "\"lastRttMs\":%u,\"minRttMs\":%u,\"maxRttMs\":%u,\"avgRttMs\":%u},"
           "\"config\":{\"filterSlaveId\":%u,\"filterFuncCode\":%u,"
           "\"timeoutMs\":%u},\"logs\":[",
           st.totalFrames, st.reqCount, st.respCount, st.timeoutCount,
           st.crcErrorCount, st.exceptionCount, st.lastRttMs, st.minRttMs,
           st.maxRttMs, avgRtt, cfg.filterSlaveId, cfg.filterFuncCode,
           cfg.timeoutMs);
  chunk += headerBuf;

  size_t count = busMonitorGetLogCount();
  for (size_t i = 0; i < count; i++) {
    const BusMonitorLog &log = busMonitorGetLogAt(i);
    char itemHead[112];
    snprintf(itemHead, sizeof(itemHead),
             "%s{\"t\":%lu,\"meta\":%u,\"uid\":%u,\"fc\":%u,\"rtt\":%u,\"hex\":\"",
             i ? "," : "", log.timestamp, static_cast<unsigned>(log.meta),
             static_cast<unsigned>(log.slaveId),
             static_cast<unsigned>(log.funcCode),
             static_cast<unsigned>(log.rttMs));
    chunk += itemHead;
    for (size_t j = 0; j < log.length; j++) {
      chunk += hexChars[log.data[j] >> 4];
      chunk += hexChars[log.data[j] & 0x0F];
      chunk += ' ';
    }
    chunk += "\"}";
    if (chunk.length() >= 1200) {
      server.sendContent(chunk);
      chunk = "";
    }
  }
  chunk += "]}";
  server.sendContent(chunk);
  server.sendContent("");
}

static void handleUpdateMonitorConfig(WebServer &server) {
  if (!apiRequireJson()) return;
  if (!server.hasArg("plain")) {
    apiError(400, "missing_body");
    return;
  }
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, server.arg("plain")) || !doc.is<JsonObject>()) {
    apiError(400, "invalid_json");
    return;
  }

  BusMonitorConfig &cfg = busMonitorGetConfig();
  if (doc.containsKey("filterSlaveId")) {
    int sid = doc["filterSlaveId"] | 0;
    if (sid >= 0 && sid <= 247) cfg.filterSlaveId = static_cast<uint8_t>(sid);
  }
  if (doc.containsKey("filterFuncCode")) {
    int fc = doc["filterFuncCode"] | 0;
    if (fc >= 0 && fc <= 127) cfg.filterFuncCode = static_cast<uint8_t>(fc);
  }
  if (doc.containsKey("timeoutMs")) {
    int to = doc["timeoutMs"] | 1000;
    if (to >= 100 && to <= 5000) cfg.timeoutMs = static_cast<uint16_t>(to);
  }
  apiOk(false);
}

void registerSimulatorRoutes(WebServer &server) {
  server.on("/api/registers", HTTP_GET,
            [&server]() { handleGetRegisters(server); });
  server.on("/api/registers", HTTP_POST,
            [&server]() { handleUpdateRegisters(server); });
  server.on("/api/simConfig", HTTP_GET,
            [&server]() { handleGetSimConfig(server); });
  server.on("/api/simConfig", HTTP_POST,
            [&server]() { handleUpdateSimConfig(server); });
  server.on("/api/monitor", HTTP_GET,
            [&server]() { handleGetMonitorLogs(server); });
  server.on("/api/monitor/config", HTTP_POST,
            [&server]() { handleUpdateMonitorConfig(server); });
  server.on("/api/monitor/clear", HTTP_POST, []() {
    if (!apiRequireJson()) return;
    busMonitorClear();
    apiOk(false);
  });
}
