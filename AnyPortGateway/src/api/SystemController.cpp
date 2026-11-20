#include "SystemController.h"
#include "../domain/AppTypes.h"
#include "../infra/ConfigStore.h"
#include "../infra/OtaService.h"
#include "../modes/DualMasterMode.h"
#include "HttpApiServer.h"
#include <Ethernet.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>

static const char EMERGENCY_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>AnyPort Emergency Setup</title><style>body{font-family:system-ui,sans-serif;max-width:480px;margin:20px auto;padding:0 12px;background:#f5f7fa;color:#222}.c{background:#fff;padding:16px;border-radius:8px;box-shadow:0 1px 4px rgba(0,0,0,.08);margin-bottom:14px}h2{margin:0 0 10px;font-size:16px;color:#2563eb}label{display:block;font-size:13px;margin:8px 0 3px;color:#555}input,select{width:100%;box-sizing:border-box;padding:7px;border:1px solid #cbd5e1;border-radius:4px}button{width:100%;margin-top:12px;padding:9px;background:#2563eb;color:#fff;border:0;border-radius:4px;cursor:pointer;font-weight:600}.s{font-size:12px;color:#64748b;line-height:1.7}</style></head><body><div class="c"><h2>AnyPort 应急配网与状态</h2><div id="st" class="s">加载中...</div></div><div class="c"><h2>基础网络与通道配置</h2><label>工作模式</label><select id="wm"><option value="0">0 - MQTT 网关模式</option><option value="1">1 - 从站模拟器模式</option><option value="2">2 - USB 转 RS485 透传</option><option value="3">3 - Modbus 协议互转</option><option value="4">4 - 总线监听与质量诊断</option><option value="5">5 - 以太网-WiFi 桥接</option><option value="6">6 - 双主站中继网关</option></select><label>单串口 RS485 通道</label><select id="ch"><option value="0">0 - 485-1 (主站侧端子)</option><option value="1">1 - 485-2 (从站侧端子)</option></select><label>WiFi SSID</label><input id="ws"><label>WiFi 密码</label><input id="wp" type="password" placeholder="不修改请留空"><label>WiFi 静态 IP (留空为 DHCP)</label><input id="wi" placeholder="留空自动获取"><label>WiFi 子网掩码</label><input id="wmk" placeholder="255.255.255.0"><label>WiFi 默认网关</label><input id="wg"><label>以太网静态 IP (留空不启用)</label><input id="ei"><label>以太网掩码</label><input id="em" placeholder="255.255.255.0"><label>以太网网关</label><input id="eg"><button onclick="save()">保存配置并重启</button></div><script>const $=id=>document.getElementById(id);async function load(){try{let[s,c]=await Promise.all([fetch('/api/sys/status').then(r=>r.json()),fetch('/api/config').then(r=>r.json())]);$('st').innerHTML=`版本: v${s.version} | 运行: ${s.uptime}s | CPU: ${s.cpu}MHz | RAM: ${s.ram}% | Flash: ${s.flash}%<br>WiFi IP: <b>${s.wifiIp}</b> (${s.wifiConnected?'STA':'AP'}) | ETH IP: <b>${s.ethIp}</b>`;$('wm').value=c.workMode;$('ch').value=c.rs485Channel;$('ws').value=c.wifi.ssid;$('wi').value=c.wifi.ip||'';$('wmk').value=c.wifi.mask||'';$('wg').value=c.wifi.gateway||'';$('ei').value=c.eth.ip;$('em').value=c.eth.mask;$('eg').value=c.eth.gateway;}catch(e){$('st').innerText='连接失败: '+e.message;}}async function save(){let b={workMode:+$('wm').value,rs485Channel:+$('ch').value,wifi:{ssid:$('ws').value,password:$('wp').value,ip:$('wi').value,mask:$('wmk').value,gateway:$('wg').value},eth:{ip:$('ei').value,mask:$('em').value,gateway:$('eg').value}};let r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});let d=await r.json();alert(d.ok?'已保存，设备即将重启':'失败: '+d.error);}load();</script></body></html>)rawliteral";

// -----------------------
// 1. 应急配网页与系统状态
// -----------------------
static void handleHttpRoot() {
  g_httpServer.sendHeader("Cache-Control", "no-cache");
  g_httpServer.send_P(200, "text/html; charset=utf-8", EMERGENCY_HTML);
}

// 获取当前活跃 WiFi 信号强度 (STA 模式读路由器 RSSI；AP 模式读当前接入客户端 RSSI)
static int getActiveWifiRssi(bool wifiSta) {
  if (wifiSta) {
    return WiFi.RSSI();
  }
  wifi_sta_list_t staList;
  if (esp_wifi_ap_get_sta_list(&staList) == ESP_OK && staList.num > 0) {
    return staList.sta[0].rssi;
  }
  return 0;
}

static void handleSystemStatus() {
  DynamicJsonDocument doc(768);
  doc["version"] = FIRMWARE_VERSION;
  doc["mode"] = static_cast<uint8_t>(g_workMode);
  doc["rs485Channel"] = g_rs485Channel;

  bool wifiSta = (WiFi.status() == WL_CONNECTED);
  doc["wifiConnected"] = wifiSta;
  doc["wifiIp"] = (wifiSta ? WiFi.localIP() : WiFi.softAPIP()).toString();
  doc["wifiMask"] =
      (wifiSta ? WiFi.subnetMask() : IPAddress(255, 255, 255, 0)).toString();
  doc["wifiGw"] = (wifiSta ? WiFi.gatewayIP() : WiFi.softAPIP()).toString();
  doc["wifiDns"] = (wifiSta ? WiFi.dnsIP() : IPAddress(0, 0, 0, 0)).toString();
  doc["wifiIsStatic"] = g_wifiStaConfig.staticIp;

  bool ethLink = (Ethernet.linkStatus() == LinkON);
  doc["ethLink"] = ethLink;
  doc["ethIp"] = Ethernet.localIP().toString();
  doc["ethMask"] = Ethernet.subnetMask().toString();
  doc["ethGw"] = Ethernet.gatewayIP().toString();
  doc["ethDns"] = Ethernet.dnsServerIP().toString();
  doc["ethIsStatic"] = g_ethConfig.valid;

  doc["cpu"] = ESP.getCpuFreqMHz();
  doc["ram"] = static_cast<int>(
      (1.0f - static_cast<float>(ESP.getFreeHeap()) / ESP.getHeapSize()) *
      100.0f);

  uint32_t usedFlash = ESP.getSketchSize();
  const esp_partition_t *running = esp_ota_get_running_partition();
  uint32_t totalFlash = (running != NULL)
                            ? running->size
                            : (usedFlash + ESP.getFreeSketchSpace());
  doc["flash"] =
      static_cast<int>(static_cast<float>(usedFlash) / totalFlash * 100.0f);
  doc["rssi"] = getActiveWifiRssi(wifiSta);
  doc["uptime"] = millis() / 1000;

  // 全局硬件与总线通信遥测字段 (所有工作模式共享 + 100% 兼容 AnyControl 平台)
  doc["gatewayName"] = g_dualMasterConfig.gatewayName;
  doc["firmware"] = FIRMWARE_VERSION;
  doc["heartbeatInterval"] = g_dualMasterConfig.heartbeatInterval;
  doc["busLatencyMs"] = g_telemetry.lastBusLatencyMs;
  doc["master1Frames"] = g_telemetry.master1FrameCount;
  doc["master2Frames"] = g_telemetry.master2FrameCount;
  doc["busCrcErrors"] = g_telemetry.busCrcErrorCount;
#if defined(ARDUINO_ARCH_ESP32)
  doc["chipTemp"] = static_cast<int>(temperatureRead() * 10) / 10.0f;
#endif

  String out;
  serializeJson(doc, out);
  apiSendJson(200, out);
}

static void handleBasicStatus() {
  StaticJsonDocument<256> doc;
  doc["mode"] = static_cast<int>(g_workMode);
  doc["uptime"] = millis() / 1000;

  String out;
  serializeJson(doc, out);
  apiSendJson(200, out);
}

static void handleSetMode() {
  if (!apiRequireJson()) return;
  StaticJsonDocument<64> doc;
  if (deserializeJson(doc, g_httpServer.arg("plain")) ||
      !doc["mode"].is<int>()) {
    apiError(400, "mode_required");
    return;
  }
  int mode = doc["mode"];
  if (mode < 0 || mode > 6) {
    apiError(400, "mode_out_of_range");
    return;
  }
  if (!configSaveWorkMode(static_cast<uint8_t>(mode))) {
    apiError(500, "nvs_error");
    return;
  }
  g_needRestart = true;
  apiOk(true);
}

// -----------------------
// 2. 全量配置读取与更新 (/api/config)
// -----------------------
static void handleApiConfigGet() {
  DynamicJsonDocument doc(3072);
  doc["gatewayName"] = g_dualMasterConfig.gatewayName;
  doc["workMode"] = static_cast<uint8_t>(g_workMode);
  doc["rs485Channel"] = g_rs485Channel;
  doc["otaApi"] = g_otaApiBase;

  JsonObject wifi = doc.createNestedObject("wifi");
  wifi["ssid"] = g_wifiStaConfig.ssid;
  wifi["password"] = g_wifiStaConfig.password.length() ? API_MASK : "";
  wifi["ip"] = g_wifiStaConfig.staticIp ? g_wifiStaConfig.ip.toString() : "";
  wifi["mask"] =
      g_wifiStaConfig.staticIp ? g_wifiStaConfig.subnet.toString() : "";
  wifi["gateway"] =
      g_wifiStaConfig.staticIp ? g_wifiStaConfig.gateway.toString() : "";
  wifi["dns"] = g_wifiStaConfig.staticIp ? g_wifiStaConfig.dns.toString() : "";

  JsonObject eth = doc.createNestedObject("eth");
  eth["ip"] = g_ethConfig.valid ? g_ethConfig.ip.toString() : "";
  eth["mask"] = g_ethConfig.valid ? g_ethConfig.subnet.toString() : "";
  eth["gateway"] = g_ethConfig.valid ? g_ethConfig.gateway.toString() : "";
  eth["dns"] = g_ethConfig.valid ? g_ethConfig.dns.toString() : "";

  JsonObject mqtt = doc.createNestedObject("mqtt");
  mqtt["host"] = g_mqttConfig.host;
  mqtt["port"] = g_mqttConfig.port;
  mqtt["path"] = g_mqttConfig.path;
  mqtt["username"] = g_mqttConfig.username;
  mqtt["password"] = g_mqttConfig.password.length() ? API_MASK : "";
  mqtt["topicPrefix"] = g_mqttConfig.topicPrefix;
  mqtt["siteId"] = g_mqttConfig.siteId;
  mqtt["gatewayId"] = g_mqttConfig.gatewayId;

  JsonObject rs1 = doc.createNestedObject("rs485_1");
  rs1["baud"] = g_rs485_1Config.baud;
  rs1["dataBits"] = g_rs485_1Config.dataBits;
  rs1["parity"] = g_rs485_1Config.parity;
  rs1["stopBits"] = g_rs485_1Config.stopBits;

  JsonObject rs2 = doc.createNestedObject("rs485_2");
  rs2["baud"] = g_rs485_2Config.baud;
  rs2["dataBits"] = g_rs485_2Config.dataBits;
  rs2["parity"] = g_rs485_2Config.parity;
  rs2["stopBits"] = g_rs485_2Config.stopBits;

  JsonObject tr = doc.createNestedObject("transparent");
  tr["baud"] = g_transBaud;
  tr["dataBits"] = g_transDataBits;
  tr["parity"] = g_transParity;
  tr["stopBits"] = g_transStopBits;

  JsonObject br = doc.createNestedObject("bridge");
  br["direction"] = g_bridgeConfig.direction;
  br["mode"] = g_bridgeConfig.bridgeMode;
  br["slaveId"] = g_bridgeConfig.slaveId;
  br["netInterface"] = g_bridgeConfig.netInterface;
  br["tcpPort"] = g_bridgeConfig.tcpPort;
  br["targetIp"] = g_bridgeConfig.targetIp;
  br["targetPort"] = g_bridgeConfig.targetPort;
  br["baud"] = g_bridgeConfig.baud;
  br["dataBits"] = g_bridgeConfig.dataBits;
  br["parity"] = g_bridgeConfig.parity;
  br["stopBits"] = g_bridgeConfig.stopBits;

  JsonObject ew = doc.createNestedObject("ethWifi");
  ew["direction"] = g_ethWifiConfig.direction;
  ew["listenPort"] = g_ethWifiConfig.listenPort;
  ew["targetIp"] = g_ethWifiConfig.targetIp;
  ew["targetPort"] = g_ethWifiConfig.targetPort;
  ew["protocol"] = g_ethWifiConfig.protocol;

  JsonObject dm = doc.createNestedObject("dualMaster");
  dm["gatewayName"] = g_dualMasterConfig.gatewayName;
  dm["baud"] = g_dualMasterConfig.masterBaud;
  dm["dataBits"] = g_dualMasterConfig.masterData;
  dm["parity"] = g_dualMasterConfig.masterParity;
  dm["stopBits"] = g_dualMasterConfig.masterStop;
  dm["wifiPort"] = g_dualMasterConfig.wifiPort;
  dm["masterTimeout"] = g_dualMasterConfig.masterTimeout;
  dm["heartbeatInterval"] = g_dualMasterConfig.heartbeatInterval;

  String out;
  serializeJson(doc, out);
  apiSendJson(200, out);
}

static bool apiParseAndCommitConfig(JsonObject root, String &err) {
  uint8_t workMode = static_cast<uint8_t>(g_workMode);
  uint8_t rsCh = g_rs485Channel;
  String otaApi = g_otaApiBase;
  WifiStaConfig wifi = g_wifiStaConfig;
  EthStaticConfig eth = g_ethConfig;
  MqttRuntimeConfig mqtt = g_mqttConfig;
  Rs485PortConfig rs1 = g_rs485_1Config;
  Rs485PortConfig rs2 = g_rs485_2Config;
  uint32_t tBaud = g_transBaud;
  uint8_t tData = g_transDataBits, tParity = g_transParity,
          tStop = g_transStopBits;
  BridgeConfig br = g_bridgeConfig;
  EthWifiConfig ew = g_ethWifiConfig;
  DualMasterConfig dm = g_dualMasterConfig;

  if (!apiStr(root, "gatewayName", 64, dm.gatewayName, err)) return false;
  if (!apiNum(root, "workMode", 0, 6, workMode, err)) return false;
  if (!apiNum(root, "rs485Channel", 0, 1, rsCh, err)) return false;
  if (!apiStr(root, "otaApi", 128, otaApi, err)) return false;
  if (otaApi.length() && !otaApi.startsWith("http")) {
    err = "otaApi_invalid_url";
    return false;
  }

  if (root.containsKey("wifi")) {
    JsonObject o = root["wifi"];
    if (!apiStr(o, "ssid", 32, wifi.ssid, err)) return false;
    if (!apiSecret(o, "password", 64, wifi.password, err)) return false;
    wifi.valid = wifi.ssid.length() > 0;

    bool pIp, pMask, pGw, pDns;
    if (!apiIp(o, "ip", wifi.ip, pIp, err)) return false;
    if (!apiIp(o, "mask", wifi.subnet, pMask, err)) return false;
    if (!apiIp(o, "gateway", wifi.gateway, pGw, err)) return false;
    if (!apiIp(o, "dns", wifi.dns, pDns, err)) return false;
    wifi.staticIp = static_cast<uint32_t>(wifi.ip) != 0 &&
                    static_cast<uint32_t>(wifi.subnet) != 0;
  }

  if (root.containsKey("eth")) {
    JsonObject o = root["eth"];
    bool pIp, pMask, pGw, pDns;
    if (!apiIp(o, "ip", eth.ip, pIp, err)) return false;
    if (!apiIp(o, "mask", eth.subnet, pMask, err)) return false;
    if (!apiIp(o, "gateway", eth.gateway, pGw, err)) return false;
    if (!apiIp(o, "dns", eth.dns, pDns, err)) return false;
    eth.valid = static_cast<uint32_t>(eth.ip) != 0 &&
                static_cast<uint32_t>(eth.subnet) != 0;
  }

  if (root.containsKey("mqtt")) {
    JsonObject o = root["mqtt"];
    if (!apiStr(o, "host", 128, mqtt.host, err)) return false;
    if (!apiNum(o, "port", 1, 65535, mqtt.port, err)) return false;
    if (!apiStr(o, "path", 64, mqtt.path, err)) return false;
    if (!apiStr(o, "username", 64, mqtt.username, err)) return false;
    if (!apiSecret(o, "password", 128, mqtt.password, err)) return false;
    if (!apiStr(o, "topicPrefix", 64, mqtt.topicPrefix, err)) return false;
    if (!apiStr(o, "siteId", 64, mqtt.siteId, err)) return false;
    if (!apiStr(o, "gatewayId", 64, mqtt.gatewayId, err)) return false;
    mqtt.valid = true;
  }

  if (root.containsKey("rs485_1")) {
    JsonObject o = root["rs485_1"];
    if (!apiNum(o, "baud", 1200, 921600, rs1.baud, err)) return false;
    if (!apiNum(o, "dataBits", 7, 8, rs1.dataBits, err)) return false;
    if (!apiNum(o, "parity", 0, 2, rs1.parity, err)) return false;
    if (!apiNum(o, "stopBits", 1, 2, rs1.stopBits, err)) return false;
  }

  if (root.containsKey("rs485_2")) {
    JsonObject o = root["rs485_2"];
    if (!apiNum(o, "baud", 1200, 921600, rs2.baud, err)) return false;
    if (!apiNum(o, "dataBits", 7, 8, rs2.dataBits, err)) return false;
    if (!apiNum(o, "parity", 0, 2, rs2.parity, err)) return false;
    if (!apiNum(o, "stopBits", 1, 2, rs2.stopBits, err)) return false;
  }

  if (root.containsKey("transparent")) {
    JsonObject o = root["transparent"];
    if (!apiNum(o, "baud", 1200, 921600, tBaud, err)) return false;
    if (!apiNum(o, "dataBits", 7, 8, tData, err)) return false;
    if (!apiNum(o, "parity", 0, 2, tParity, err)) return false;
    if (!apiNum(o, "stopBits", 1, 2, tStop, err)) return false;
  }

  if (root.containsKey("bridge")) {
    JsonObject o = root["bridge"];
    if (!apiNum(o, "direction", 0, 1, br.direction, err)) return false;
    if (!apiNum(o, "mode", 0, 1, br.bridgeMode, err)) return false;
    if (!apiNum(o, "slaveId", 1, 247, br.slaveId, err)) return false;
    if (!apiNum(o, "netInterface", 0, 2, br.netInterface, err)) return false;
    if (!apiNum(o, "tcpPort", 1, 65535, br.tcpPort, err)) return false;
    if (!apiStr(o, "targetIp", 15, br.targetIp, err)) return false;
    if (!apiNum(o, "targetPort", 1, 65535, br.targetPort, err)) return false;
    if (!apiNum(o, "baud", 1200, 921600, br.baud, err)) return false;
    if (!apiNum(o, "dataBits", 7, 8, br.dataBits, err)) return false;
    if (!apiNum(o, "parity", 0, 2, br.parity, err)) return false;
    if (!apiNum(o, "stopBits", 1, 2, br.stopBits, err)) return false;
  }

  if (root.containsKey("ethWifi")) {
    JsonObject o = root["ethWifi"];
    if (!apiNum(o, "direction", 0, 1, ew.direction, err)) return false;
    if (!apiNum(o, "listenPort", 1, 65535, ew.listenPort, err)) return false;
    if (!apiStr(o, "targetIp", 15, ew.targetIp, err)) return false;
    if (!apiNum(o, "targetPort", 1, 65535, ew.targetPort, err)) return false;
    if (!apiNum(o, "protocol", 0, 1, ew.protocol, err)) return false;
  }

  if (root.containsKey("dualMaster")) {
    JsonObject o = root["dualMaster"];
    if (!apiStr(o, "gatewayName", 64, dm.gatewayName, err)) return false;
    if (!apiNum(o, "baud", 1200, 921600, dm.masterBaud, err)) return false;
    if (!apiNum(o, "dataBits", 7, 8, dm.masterData, err)) return false;
    if (!apiNum(o, "parity", 0, 2, dm.masterParity, err)) return false;
    if (!apiNum(o, "stopBits", 1, 2, dm.masterStop, err)) return false;
    if (!apiNum(o, "wifiPort", 1, 65535, dm.wifiPort, err)) return false;
    if (!apiNum(o, "masterTimeout", 100, 3000, dm.masterTimeout, err))
      return false;
    if (!apiNum(o, "heartbeatInterval", 5, 86400, dm.heartbeatInterval, err))
      return false;
  }

  if (!configSaveSystem(workMode, rsCh, otaApi, wifi, eth, mqtt, rs1, rs2,
                        tBaud, tData, tParity, tStop, br, ew, dm)) {
    err = "nvs_error";
    return false;
  }
  return true;
}

static void handleApiConfigPost() {
  if (!apiRequireJson()) return;

  DynamicJsonDocument doc(3072);
  DeserializationError e = deserializeJson(doc, g_httpServer.arg("plain"));
  if (e || !doc.is<JsonObject>()) {
    apiError(400, "invalid_json");
    return;
  }

  String err;
  if (!apiParseAndCommitConfig(doc.as<JsonObject>(), err)) {
    apiError(400, err);
    return;
  }
  g_needRestart = true;
  apiOk(true);
}

// -----------------------
// 3. OTA 固件维护路由
// -----------------------
static void handleOtaCheck() {
  int result = checkOtaUpdate();
  if (result == -3) {
    apiError(400, "请先配置并保存有效的 OTA 更新服务地址");
    return;
  }
  if (result == -2) {
    apiError(503, "网关当前未连接网络，无法访问 OTA 服务");
    return;
  }
  if (result < 0) {
    apiError(502, "无法连接 OTA 服务器或响应无效，请检查服务地址");
    return;
  }

  DynamicJsonDocument doc(512);
  doc["found"] = (result == 1);
  const String &remoteVer = otaGetRemoteVersion();
  doc["version"] = remoteVer != "" ? remoteVer : FIRMWARE_VERSION;
  doc["changelog"] = otaGetChangelog();
  String out;
  serializeJson(doc, out);
  apiSendJson(200, out);
}

static void handleOtaUpdate() {
  if (!apiRequireJson()) return;
  if (validateFirmwareExistence()) {
    apiOk(true);
    delay(500);
    executeFirmwareUpdate();
  } else {
    apiError(404, "firmware_not_found");
  }
}

static void handleOtaStatus() {
  String backupVer;
  bool canRollback = false;
  getBackupPartitionInfo(backupVer, canRollback);
  StaticJsonDocument<256> doc;
  doc["found"] = otaIsUpdateFound();
  doc["version"] = otaGetRemoteVersion();
  doc["canRollback"] = canRollback;
  doc["backupVersion"] = backupVer;
  String out;
  serializeJson(doc, out);
  apiSendJson(200, out);
}

static void handleOtaRollback() {
  if (!apiRequireJson()) return;
  if (executeRollback()) {
    apiOk(true);
    delay(1000);
    ESP.restart();
  } else {
    apiError(400, "no_valid_backup_partition");
  }
}

// -----------------------
// 3.5 双主站模式 (Mode 6 / AnyControl 平台) 在线参数同步与热重载接口
// -----------------------
static void handleApiGatewayConfig() {
  DualMasterConfig dm = g_dualMasterConfig;
  String body = g_httpServer.arg("plain");

  if (body.length() > 0) {
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, body);
    if (!err && doc.is<JsonObject>()) {
      if (doc.containsKey("gatewayName")) {
        String name = doc["gatewayName"].as<String>();
        if (name.length() > 0) dm.gatewayName = name;
      }
      if (doc.containsKey("baud")) {
        uint32_t b = doc["baud"].as<uint32_t>();
        if (b >= 1200 && b <= 921600) dm.masterBaud = b;
      }
      if (doc.containsKey("dataBits")) {
        uint8_t d = doc["dataBits"].as<uint8_t>();
        if (d == 7 || d == 8) dm.masterData = d;
      }
      if (doc.containsKey("parity")) {
        uint8_t p = doc["parity"].as<uint8_t>();
        if (p <= 2) dm.masterParity = p;
      }
      if (doc.containsKey("stopBits")) {
        uint8_t s = doc["stopBits"].as<uint8_t>();
        if (s == 1 || s == 2) dm.masterStop = s;
      }
      if (doc.containsKey("wifiPort")) {
        uint16_t wp = doc["wifiPort"].as<uint16_t>();
        if (wp > 0) dm.wifiPort = wp;
      }
      uint16_t to = 0;
      if (doc.containsKey("masterTimeout"))
        to = doc["masterTimeout"].as<uint16_t>();
      else if (doc.containsKey("timeout"))
        to = doc["timeout"].as<uint16_t>();
      if (to >= 100 && to <= 3000) dm.masterTimeout = to;

      if (doc.containsKey("heartbeatInterval")) {
        uint32_t hb = doc["heartbeatInterval"].as<uint32_t>();
        if (hb > 0) dm.heartbeatInterval = hb;
      }
    }
  }

  if (!configSaveDualMasterHotReload(dm)) {
    apiSendJson(500, "{\"success\":false,\"message\":\"NVS写入失败\"}");
    return;
  }

  // 严格保护：仅当设备当前处于模式 6 (双主站模式) 时才热重载双路串口，绝不干扰其他模式
  if (g_workMode == WorkMode::DUAL_MASTER) {
    DualMasterMode::reconfigSerial();
  }

  DynamicJsonDocument resp(384);
  resp["success"] = true;
  resp["message"] = "双主站通信串口参数、超时与网关名称已更新生效";
  JsonObject data = resp.createNestedObject("data");
  data["gatewayName"] = g_dualMasterConfig.gatewayName;
  data["baud"] = g_dualMasterConfig.masterBaud;
  data["dataBits"] = g_dualMasterConfig.masterData;
  data["parity"] = g_dualMasterConfig.masterParity;
  data["stopBits"] = g_dualMasterConfig.masterStop;
  data["wifiPort"] = g_dualMasterConfig.wifiPort;
  data["masterTimeout"] = g_dualMasterConfig.masterTimeout;
  data["heartbeatInterval"] = g_dualMasterConfig.heartbeatInterval;

  String out;
  serializeJson(resp, out);
  apiSendJson(200, out);
}

// -----------------------
// 4. 路由挂载入口
// -----------------------
void registerSystemRoutes(WebServer &server) {
  server.on("/", HTTP_GET, handleHttpRoot);
  server.on("/api/sys/status", HTTP_GET, handleSystemStatus);
  server.on("/api/status", HTTP_GET, handleBasicStatus);
  server.on("/api/mode", HTTP_POST, handleSetMode);
  server.on("/api/config", HTTP_GET, handleApiConfigGet);
  server.on("/api/config", HTTP_POST, handleApiConfigPost);
  server.on("/api/gateway/config", HTTP_POST, handleApiGatewayConfig);
  server.on("/api/ota/check", HTTP_GET, handleOtaCheck);
  server.on("/api/ota/do", HTTP_POST, handleOtaUpdate);
  server.on("/api/ota/status", HTTP_GET, handleOtaStatus);
  server.on("/api/ota/rollback", HTTP_POST, handleOtaRollback);
}
