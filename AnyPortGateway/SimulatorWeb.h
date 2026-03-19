#pragma once

#include <stdint.h>
#include <string.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include "CommonTypes.h"
#include "Globals.h"

/**
 * @brief 从站模拟器 Web API 处理逻辑
 */

#include "SimulatorCore.h"

// 外部引用的工作模式
extern WorkMode g_workMode;

/**
 * @brief 构建寄存器列表的 JSON 响应
 */
static void handleGetRegisters(WebServer& server) {
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.to<JsonArray>();
    syncValuesFromPool();

    for (size_t i = 0; i < g_simVarCount; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["addr"] = g_simVariables[i].address;
        obj["name"] = g_simVariables[i].name;
        obj["type"] = (int)g_simVariables[i].type;
        obj["endian"] = (int)g_simVariables[i].endian;
        obj["targetVal"] = g_simVariables[i].targetValue;
        obj["val"] = g_simVariables[i].currentValue;
        obj["isDyn"] = g_simVariables[i].isDynamic;
        obj["min"] = g_simVariables[i].dynamicMin;
        obj["max"] = g_simVariables[i].dynamicMax;
        obj["dynInterval"] = g_simVariables[i].dynInterval;
        obj["dynMode"] = (int)g_simVariables[i].dynMode;
    }

    String out;
    serializeJson(doc, out);
    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "application/json", out);
}

/**
 * @brief 处理更新寄存器的 POST 请求
 */
static void handleUpdateRegisters(WebServer& server) {
    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "Missing body");
        return;
    }

    DynamicJsonDocument doc(8192); // 提升至 8KB，防止寄存器较多时解析失败
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
        Serial.printf("[Web] JSON Parse Error: %s\n", err.c_str());
        server.send(400, "text/plain", "Invalid JSON");
        return;
    }

    JsonArray arr = doc.as<JsonArray>();
    g_simVarCount = 0;
    for (JsonVariant v : arr) {
        if (g_simVarCount >= MAX_SIM_VARIABLES) break;
        
        SimulatorVariable& var = g_simVariables[g_simVarCount++];
        var.address = v["addr"] | 40001;
        strlcpy(var.name, v["name"] | "", sizeof(var.name));
        var.type = static_cast<DataType>(v["type"] | 0);
        var.endian = static_cast<Endianness>(v["endian"] | 0);
        var.targetValue = v["targetVal"] | 0.0f;
        var.currentValue = v["val"] | var.targetValue; // 初始时让状态值等于设定值
        var.isDynamic = v["isDyn"] | false;
        var.dynamicMin = v["min"] | 0.0f;
        var.dynamicMax = v["max"] | 100.0f;
        var.dynInterval = v["dynInterval"] | 1;
        var.dynMode = v["dynMode"] | 0;
        var._lastDynUpdate = 0;
        
        writeValueToPool(var);
    }

    saveSimConfig();
    server.send(200, "text/plain", "OK");
}

static void handleGetSimConfig(WebServer& server) {
    StaticJsonDocument<512> doc;
    doc["rtuEnabled"] = g_simConfig.rtuEnabled;
    doc["baud"] = g_simConfig.baud;
    doc["stopBits"] = g_simConfig.stopBits;
    doc["parity"] = g_simConfig.parity;
    doc["dataBits"] = g_simConfig.dataBits;
    doc["unitId"] = g_simConfig.unitId;
    doc["tcpEnabled"] = g_simConfig.tcpEnabled;
    doc["tcpPort"] = g_simConfig.tcpPort;
    doc["monitorEnabled"] = g_simConfig.monitorEnabled;
    doc["monitorFilter"] = g_simConfig.monitorFilter;

    String out;
    serializeJson(doc, out);
    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "application/json", out);
}

static void handleUpdateSimConfig(WebServer& server) {
    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "Missing body");
        return;
    }
    StaticJsonDocument<512> doc;
    deserializeJson(doc, server.arg("plain"));
    
    g_simConfig.rtuEnabled = doc["rtuEnabled"] | false;
    g_simConfig.baud = doc["baud"] | 9600;
    g_simConfig.stopBits = doc["stopBits"] | 1;
    g_simConfig.parity = doc["parity"] | 0;
    g_simConfig.dataBits = doc["dataBits"] | 8;
    g_simConfig.unitId = doc["unitId"] | 1;
    g_simConfig.tcpEnabled = doc["tcpEnabled"] | false;
    g_simConfig.tcpPort = doc["tcpPort"] | 502;
    g_simConfig.monitorEnabled = doc["monitorEnabled"] | false;
    g_simConfig.monitorFilter = doc["monitorFilter"] | 0;
    
    saveSimConfig();
    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "text/plain", "OK");
}

/**
 * @brief 获取报文监听数据的 API
 */
static void handleGetMonitorLogs(WebServer& server) {
    DynamicJsonDocument doc(8192);
    JsonArray arr = doc.to<JsonArray>();

    for (size_t i = 0; i < g_monitorCount; i++) {
        size_t idx = (g_monitorHead + i) % MAX_MONITOR_LOGS;
        JsonObject obj = arr.createNestedObject();
        obj["t"] = g_monitorLogs[idx].timestamp;
        obj["dir"] = g_monitorLogs[idx].isOutgoing ? "TX" : "RX";
        obj["type"] = g_monitorLogs[idx].type == 0 ? "RTU" : "TCP";
        
        static const char* hexChars = "0123456789ABCDEF";
        String hex = "";
        hex.reserve(g_monitorLogs[idx].length * 3);
        for (size_t j = 0; j < g_monitorLogs[idx].length; j++) {
            uint8_t b = g_monitorLogs[idx].data[j];
            hex += hexChars[b >> 4];
            hex += hexChars[b & 0x0F];
            hex += " ";
        }
        obj["hex"] = hex;
    }

    String out;
    serializeJson(doc, out);
    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "application/json", out);
}

/**
 * @brief 处理工作模式切换的 POST 请求
 */
static void handleSetMode(WebServer& server) {
    if (server.hasArg("mode")) {
        uint8_t mode = server.arg("mode").toInt();
        extern Preferences g_prefs;
        extern bool g_needRestart;
        
        g_prefs.begin("anyport", false);
        g_prefs.putUChar("workMode", mode);
        g_prefs.end();
        
        g_needRestart = true;
        server.send(200, "text/plain", "Mode updated, restarting...");
    } else {
        server.send(400, "text/plain", "Missing mode");
    }
}

/**
 * @brief 模拟器专属 Web 路由挂载逻辑
 */
void initSimulatorWeb(WebServer& server) {
    server.on("/api/registers", HTTP_GET, [&server]() { handleGetRegisters(server); });
    server.on("/api/registers", HTTP_POST, [&server]() { handleUpdateRegisters(server); });
    server.on("/api/simConfig", HTTP_GET, [&server]() { handleGetSimConfig(server); });
    server.on("/api/simConfig", HTTP_POST, [&server]() { handleUpdateSimConfig(server); });
    server.on("/api/monitor", HTTP_GET, [&server]() { handleGetMonitorLogs(server); });
    server.on("/api/monitor/clear", HTTP_POST, [&server]() {
        clearMonitorLogs();
        server.send(200, "text/plain", "OK");
    });
    server.on("/api/mode", HTTP_POST, [&server]() { handleSetMode(server); });
    
    // 异步状态同步接口
    server.on("/api/status", HTTP_GET, [&server]() {
        StaticJsonDocument<256> doc;
        doc["mode"] = (int)g_workMode;
        doc["uptime"] = millis() / 1000;
        
        String out;
        serializeJson(doc, out);
        server.sendHeader("Cache-Control", "no-cache");
        server.send(200, "application/json", out);
    });
}
