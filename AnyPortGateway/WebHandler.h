#pragma once

#include "Globals.h"
#include "SimulatorCore.h"
#include "SimulatorWeb.h"
#include "OtaHandler.h"

// -----------------------
// 1. MQTT 消息路由
// -----------------------
void handleMqttRequestMessage(const String &topic, const uint8_t *payload,
                              unsigned int length) {
  String sessionId;
  int idx = topic.lastIndexOf('/');
  if (idx >= 0 && idx + 1 < static_cast<int>(topic.length()))
    sessionId = topic.substring(idx + 1);

  DynamicJsonDocument doc(MQTT_JSON_DOC_SIZE);
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err != DeserializationError::Ok) {
    StaticJsonDocument<256> resp;
    resp["sessionId"] = sessionId;
    resp["success"] = false;
    resp["error"] = String("json_error:") + err.c_str();
    String out;
    serializeJson(resp, out);
    extern String buildMqttResponseTopic(const String &sessionId);
    g_mqttClient.publish(buildMqttResponseTopic(sessionId).c_str(), out.c_str(),
                         false, 1);
    return;
  }

  const char *transport = doc["transport"] | "";

  if (strcmp(transport, "ping") == 0) {
    extern void pingForward(JsonDocument & doc, const String &sessionId);
    pingForward(doc, sessionId);
    return;
  }

  const char *payloadHex = doc["payloadHex"] | "";

  if (!transport[0] || !payloadHex[0]) {
    StaticJsonDocument<256> resp;
    resp["sessionId"] = sessionId;
    resp["success"] = false;
    resp["error"] = "missing_transport_or_payloadHex";
    String out;
    serializeJson(resp, out);
    extern String buildMqttResponseTopic(const String &sessionId);
    g_mqttClient.publish(buildMqttResponseTopic(sessionId).c_str(), out.c_str(),
                         false, 1);
    return;
  }

  doc["hex"] = payloadHex;

  DynamicJsonDocument modbusResp(512);
  bool ok = false;
  extern bool modbusTcpForward(JsonDocument & doc, JsonDocument & resp);
  extern bool modbusRtuForward(JsonDocument & doc, JsonDocument & resp);

  if (strcmp(transport, "tcp") == 0)
    ok = modbusTcpForward(doc, modbusResp);
  else if (strcmp(transport, "rtu") == 0)
    ok = modbusRtuForward(doc, modbusResp);
  else {
    StaticJsonDocument<256> resp;
    resp["sessionId"] = sessionId;
    resp["success"] = false;
    resp["error"] = "unsupported_transport";
    String out;
    serializeJson(resp, out);
    extern String buildMqttResponseTopic(const String &sessionId);
    g_mqttClient.publish(buildMqttResponseTopic(sessionId).c_str(), out.c_str(),
                         false, 1);
    return;
  }

  StaticJsonDocument<512> resp;
  resp["sessionId"] = sessionId;
  resp["success"] = ok;

  if (ok) {
    const char *outHex = modbusResp["hex"] | "";
    resp["payloadHex"] = outHex;
  } else {
    const char *msg = modbusResp["message"] | "modbus_forward_failed";
    resp["error"] = msg;
  }

  String out;
  serializeJson(resp, out);
  extern String buildMqttResponseTopic(const String &sessionId);
  g_mqttClient.publish(buildMqttResponseTopic(sessionId).c_str(), out.c_str(),
                       false, 1);
}

// -----------------------
// 2. Web 路由逻辑
// -----------------------

static void handleHttpRoot() {
  String html;
  html.reserve(8192);
  html +=
      "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' "
      "content='width=device-width,initial-scale=1'><title>AnyPort 配置 "
      "v" FIRMWARE_VERSION "</title>";
  html +=
      "<style>body{font-family:sans-serif;margin:20px;background:#f4f4f4} "
      "h2{border-bottom:2px solid #336699;padding-bottom:5px;color:#336699} "
      ".card{background:white;padding:15px;border-radius:8px;box-shadow:0 2px "
      "5px rgba(0,0,0,0.1);margin-bottom:20px} "
      "label{display:inline-block;width:160px;margin-bottom:8px;white-space:"
      "nowrap} input,select{padding:6px;border:1px solid "
      "#ccc;border-radius:4px;margin-bottom:10px;width:200px} "
      "table{width:100%;border-collapse:collapse} th,td{padding:8px;border:1px "
      "solid #ddd;text-align:center} button{padding:10px "
      "20px;background:#336699;color:white;border:none;border-radius:4px;"
      "cursor:pointer} "
      ".save-btn{display:block;width:100%;font-size:18px;margin-top:20px;"
      "background:#28a745} .badge{display:inline-block;padding:2px "
      "10px;border-radius:15px;font-size:13px;margin-left:10px;background:#"
      "d1ecf1;color:#0c5460;border:1px solid "
      "#bee5eb;vertical-align:middle;font-weight:bold}</style>";
  html += "</head><body>";
  html += "<h1 id='mainTitle'>AnyPort 智能网关控制面板 v" FIRMWARE_VERSION;
  if (g_otaUpdateFound) {
      html += "<span id='otaBadge' class='badge' style='background:#dc3545;color:white;border-color:#bd2130'>有新版本</span>";
  }
  html += "<span style='font-size:14px;color:#666;font-weight:normal;margin-left:15px'>© 2026 Hotwon-CD2-Hsieh</span></h1>";
  html += "<form id='mainForm' method='POST' action='/config'>";

  // 1. 顶部：WIFI 与 模式切换
  html += "<div class='card'>";
  html += "<h2>基础配置</h2>";
  html +=
      "<label>WiFi SSID:</label><input name='wifiSsid' value='" +
      g_wifiStaConfig.ssid +
      "'><span style='color:#666;font-size:13px;margin-left:5px'>当前 IP: " +
      (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString()
                                     : WiFi.softAPIP().toString()) +
      "</span><br>";
  html += "<label>WiFi 密码:</label><input name='wifiPwd' type='password' "
          "placeholder='********'><br>";
  html += "<label>Domain Name:</label><input name='mdnsName' value='" +
          g_mdnsName +
          "' maxlength='16' placeholder='anyport'><span "
          "style='color:#666;font-size:13px;margin-left:5px'>可使用 " +
          g_mdnsName + ".local 进行访问</span><br>";
  html += "<label>工作模式:</label><select name='workMode' "
          "onchange='uiToggleMode(this.value)'>";
  html += "<option value='4'" +
          String(g_workMode == WorkMode::FIRMWARE_UPDATE ? " selected" : "") +
          ">系统固件更新模式</option>";
  html += "<option value='0'" +
          String(g_workMode == WorkMode::GATEWAY ? " selected" : "") +
          ">MQTT 网关模式</option>";
  html += "<option value='1'" +
          String(g_workMode == WorkMode::SIMULATOR ? " selected" : "") +
          ">从站模拟器模式</option>";
  html += "<option value='2'" +
          String(g_workMode == WorkMode::TRANSPARENT ? " selected" : "") +
          ">USB 转 RS485 透传模式</option>";
  html += "<option value='3'" +
          String(g_workMode == WorkMode::BRIDGE ? " selected" : "") +
          ">RTU-TCP 协议互转模式</option>";
  html += "<option value='5'" +
          String(g_workMode == WorkMode::ETH_WIFI_BRIDGE ? " selected" : "") +
          ">WiFi 转以太网透传模式</option>";
  html += "</select>";
  html +=
      "<span class='badge'>当前运行模式: " +
      String(g_workMode == WorkMode::GATEWAY
                 ? "MQTT 网关"
                 : (g_workMode == WorkMode::SIMULATOR
                        ? "从站模拟器"
                        : (g_workMode == WorkMode::TRANSPARENT ? "USB 透传"
                                                               : (g_workMode == WorkMode::BRIDGE ? "RTU-TCP 协议互转" : (g_workMode == WorkMode::FIRMWARE_UPDATE ? "系统固件更新" : "WiFi 转以太网透传"))))) +
      "</span>";
  html += "</div>";

  // 2. 模式 A: 网关配置
  html += "<div id='secGateway' class='card' style='display:" +
          String(g_workMode == WorkMode::GATEWAY ? "block" : "none") + "'>";
  html += "<h2>MQTT 网关参数</h2>";
  html += "<label>Broker Host:</label><input name='mqttHost' value='" +
          g_mqttConfig.host + "'><br>";
  html += "<label>Broker Port:</label><input name='mqttPort' type='number' "
          "value='" +
          String(g_mqttConfig.port) + "'><br>";
  html += "<label>WebSocket Path:</label><input name='mqttPath' value='" +
          g_mqttConfig.path + "'><br>";
  html += "<label>用户名:</label><input name='mqttUser' value='" +
          g_mqttConfig.username + "'><br>";
  html += "<label>密码:</label><input name='mqttPwd' type='password' "
          "placeholder='********'><br>";
  html += "<label>Topic 前缀:</label><input name='mqttPrefix' value='" +
          g_mqttConfig.topicPrefix + "'><br>";
  html += "<label>Site ID:</label><input name='mqttSite' value='" +
          (g_mqttConfig.siteId.length() > 0 ? g_mqttConfig.siteId
                                            : String(MQTT_SITE_ID)) +
          "'><br>";
  html += "<label>Gateway ID:</label><input name='mqttGateway' value='" +
          g_mqttConfig.gatewayId + "'><br>";
  html += "<h3>NTP 时间同步</h3>";
  html += "<label>NTP Server:</label><input name='ntpServer' value='" +
          g_ntpConfig.server + "'><br>";
  html += "<label>同步间隔 (s):</label><input name='ntpInterval' type='number' "
          "value='" +
          String(g_ntpConfig.interval) + "'><br>";
  html += "</div>";

  // 2.5 以太网配置 (W5500) - 在网关、模拟器、协议互转模式下显示，透传与升级模式隐藏
  html += "<div id='secEthConfig' class='card' style='display:" +
          String((g_workMode != WorkMode::TRANSPARENT && g_workMode != WorkMode::FIRMWARE_UPDATE) ? "block" : "none") + "'>";
  html += "<h2>以太网配置 (W5500)</h2>";
  html += "<label>IP 地址:</label><input name='ethIp' "
          "placeholder='192.168.1.100' value='" +
          (g_ethConfig.valid ? g_ethConfig.ip.toString() : "") + "'><br>";
  html += "<label>子网掩码:</label><input name='ethMask' "
          "placeholder='255.255.255.0' value='" +
          (g_ethConfig.valid ? g_ethConfig.subnet.toString() : "") + "'><br>";
  html += "<label>默认网关:</label><input name='ethGw' "
          "placeholder='192.168.1.1' value='" +
          (g_ethConfig.valid ? g_ethConfig.gateway.toString() : "") + "'><br>";
  html += "<label>DNS 地址:</label><input name='ethDns' placeholder='8.8.8.8' "
          "value='" +
          (g_ethConfig.valid ? g_ethConfig.dns.toString() : "") + "'><br>";
  html += "</div>";

  // 3. 模式 B: 从站模拟配置
  html += "<div id='secSimulator' class='card' style='display:" +
          String(g_workMode == WorkMode::SIMULATOR ? "block" : "none") + "'>";
  html += "<h2>从站模拟器配置</h2>";
  html += "<h3>Modbus RTU (RS485)</h3>";
  html += "<label>状态:</label><select name='simRtuEnabled' "
          "onchange='document.getElementById(\"simRtuExt\").style.display=("
          "this.value==\"1\"?\"block\":\"none\")'>";
  html += "<option value='1' " +
          String(g_simConfig.rtuEnabled ? "selected" : "") + ">启用</option>";
  html += "<option value='0' " +
          String(!g_simConfig.rtuEnabled ? "selected" : "") +
          ">禁用</option></select>";
  html += "<div id='simRtuExt' style='display:" +
          String(g_simConfig.rtuEnabled ? "block" : "none") + "'>";
  html += "<label>从站地址码:</label><input name='simUnitId' type='number' "
          "value='" +
          String(g_simConfig.unitId) + "' style='width:60px'><br>";
  html += "<label>波特率:</label><select name='simBaud'>";
  uint32_t bauds[] = {4800, 9600, 19200, 38400, 115200};
  for (int i = 0; i < 5; i++)
    html += "<option value='" + String(bauds[i]) + "'" +
            (g_simConfig.baud == bauds[i] ? " selected" : "") + ">" +
            String(bauds[i]) + "</option>";
  html += "</select><br>";
  html += "<label>数据位:</label><select name='simDataBits'>";
  html += "<option value='8' " +
          String(g_simConfig.dataBits == 8 ? "selected" : "") + ">8位</option>";
  html += "<option value='7' " +
          String(g_simConfig.dataBits == 7 ? "selected" : "") +
          ">7位</option></select><br>";
  html += "<label>检验位:</label><select name='simParity'>";
  html += "<option value='0' " +
          String(g_simConfig.parity == 0 ? "selected" : "") + ">None</option>";
  html += "<option value='1' " +
          String(g_simConfig.parity == 1 ? "selected" : "") + ">Even</option>";
  html += "<option value='2' " +
          String(g_simConfig.parity == 2 ? "selected" : "") +
          ">Odd</option></select><br>";
  html += "<label>停止位:</label><select name='simStop'>";
  html += "<option value='1' " +
          String(g_simConfig.stopBits == 1 ? "selected" : "") + ">1位</option>";
  html += "<option value='2' " +
          String(g_simConfig.stopBits == 2 ? "selected" : "") +
          ">2位</option></select>";
  html += "</div>";
  html += "<h3>Modbus TCP (以太网)</h3>";
  html += "<label>状态:</label><select name='simTcpEnabled' "
          "onchange='document.getElementById(\"simTcpExt\").style.display=("
          "this.value==\"1\"?\"block\":\"none\")'>";
  html += "<option value='1' " +
          String(g_simConfig.tcpEnabled ? "selected" : "") + ">启用</option>";
  html += "<option value='0' " +
          String(!g_simConfig.tcpEnabled ? "selected" : "") +
          ">禁用</option></select>";
  html += "<div id='simTcpExt' style='display:" +
          String(g_simConfig.tcpEnabled ? "block" : "none") + "'>";
  html +=
      "<label>监听端口:</label><input name='simTcpPort' type='number' value='" +
      String(g_simConfig.tcpPort) + "' style='width:60px'><br>";
  html += "<label>网络接口:</label><select name='simNetInt'>";
  html += "<option value='0' " + String(g_simConfig.netInterface == 0 ? "selected" : "") + ">自动 (WiFi + W5500)</option>";
  html += "<option value='1' " + String(g_simConfig.netInterface == 1 ? "selected" : "") + ">仅以太网 (W5500)</option>";
  html += "<option value='2' " + String(g_simConfig.netInterface == 2 ? "selected" : "") + ">仅 WiFi</option></select>";
  html += "</div>";
  html += "<h3>报文监听 (Sniffer)</h3>";
  html += "<label>启用监听:</label><input type='checkbox' "
          "name='simMonitorEnabled' " +
          String(g_simConfig.monitorEnabled ? "checked" : "") +
          " style='width:auto'><br>";
  html += "<label>过滤模式:</label><select name='simMonitorFilter'>";
  html += "<option value='0' " +
          String(g_simConfig.monitorFilter == 0 ? "selected" : "") +
          ">全线报文</option>";
  html += "<option value='1' " +
          String(g_simConfig.monitorFilter == 1 ? "selected" : "") +
          ">仅本站报文</option>";
  html += "</select>";
  html += "</div>";

  // 4. 模式 C: USB 透传配置
  html += "<div id='secTransparent' class='card' style='display:" +
          String(g_workMode == WorkMode::TRANSPARENT ? "block" : "none") + "'>";
  html += "<h2>USB 转 RS485 透传配置</h2>";
  html +=
      "<p "
      "style='color:#856404;font-size:14px;background:#fff3cd;padding:10px;"
      "border-radius:4px;border:1px solid "
      "#ffeeba'><strong>提示：</"
      "strong>"
      "当前硬件不支持自动跟随设置。上位调试软件配置的波特率、数据位、校验位、停"
      "止位参数将无效，请确保在此页面配置的参数与您的 RS485 设备一致。</p>";
  html += "<h3>RS485 串口参数</h3>";

  html += "<label>波特率:</label><select name='tBaud'>";
  uint32_t bauds_t[] = {4800, 9600, 19200, 38400, 115200};
  for (int i = 0; i < 5; i++)
    html += "<option value='" + String(bauds_t[i]) + "'" +
            (g_transBaud == bauds_t[i] ? " selected" : "") + ">" +
            String(bauds_t[i]) + "</option>";
  html += "</select><br>";

  html += "<label>数据位:</label><select name='tData'>";
  html += "<option value='8' " +
          String(g_transDataBits == 8 ? "selected" : "") + ">8位</option>";
  html += "<option value='7' " +
          String(g_transDataBits == 7 ? "selected" : "") +
          ">7位</option></select><br>";

  html += "<label>检验位:</label><select name='tParity'>";
  html += "<option value='0' " + String(g_transParity == 0 ? "selected" : "") +
          ">None</option>";
  html += "<option value='1' " + String(g_transParity == 1 ? "selected" : "") +
          ">Even</option>";
  html += "<option value='2' " + String(g_transParity == 2 ? "selected" : "") +
          ">Odd</option></select><br>";

  html += "<label>停止位:</label><select name='tStop'>";
  html += "<option value='1' " +
          String(g_transStopBits == 1 ? "selected" : "") + ">1位</option>";
  html += "<option value='2' " +
          String(g_transStopBits == 2 ? "selected" : "") +
          ">2位</option></select><br>";
  html += "</div>";

  // 4.5 模式 D: 协议互转配置
  html += "<div id='secBridge' class='card' style='display:" +
          String(g_workMode == WorkMode::BRIDGE ? "block" : "none") + "'>";
  html += "<h2>RTU-TCP 协议互转配置</h2>";
  html += "<h3>转换方向</h3>";
  html += "<label>转换方向:</label><select name='bDir' "
          "onchange='uiToggleBridge(this.value)'>";
  html += "<option value='0' " +
          String(g_bridgeConfig.direction == 0 ? "selected" : "") +
          ">以太网主站 -> 访问串口从站 (TCP 服务模式)</option>";
  html += "<option value='1' " +
          String(g_bridgeConfig.direction == 1 ? "selected" : "") +
          ">串口主站 -> 访问网络从站 (RTU 监听模式)</option>";
  html += "</select><br>";

  html += "<h3>转换模式</h3>";
  html += "<label>转换方式:</label><select name='bMode' "
          "onchange='uiToggleBridgeMode(this.value)'>";
  html += "<option value='0' " +
          String(g_bridgeConfig.bridgeMode == 0 ? "selected" : "") +
          ">总线级透明传输 (Bus-level)</option>";
  html += "<option value='1' " +
          String(g_bridgeConfig.bridgeMode == 1 ? "selected" : "") +
          ">单个设备地址映射 (Single Device)</option>";
  html += "</select>";
  html += "<span "
          "style='color:#666;font-size:13px;margin-left:5px'>*"
          "单个设备地址映射会忽略主站下发的 ID 并强制改写为下方 ID</span><br>";

  html += "<div id='bridgeSlaveId' style='display:" +
          String(g_bridgeConfig.bridgeMode == 1 ? "block" : "none") + "'>";
  html += "<label>从站 ID (1-247):</label><input name='bSlaveId' type='number' "
          "value='" +
          String(g_bridgeConfig.slaveId) +
          "'><span style='color:#666;font-size:13px;margin-left:5px'>*此 ID "
          "必须与 RS485 总线上串口设备的真实地址一致</span><br>";
  html += "</div>";

  html += "<h3>网络参数</h3>";

  html += "<div id='bridgeTcpServer' style='display:" +
          String(g_bridgeConfig.direction == 0 ? "block" : "none") + "'>";
  html +=
      "<label>监听端口:</label><input name='bTcpPort' type='number' value='" +
      String(g_bridgeConfig.tcpPort) + "'><br>";
  html += "<label>网络接口:</label><select name='bNetInt'>";
  html += "<option value='0' " + String(g_bridgeConfig.netInterface == 0 ? "selected" : "") + ">自动 (WiFi + W5500)</option>";
  html += "<option value='1' " + String(g_bridgeConfig.netInterface == 1 ? "selected" : "") + ">仅以太网 (W5500)</option>";
  html += "<option value='2' " + String(g_bridgeConfig.netInterface == 2 ? "selected" : "") + ">仅 WiFi</option></select>";
  html += "</div>";

  html += "<div id='bridgeTcpClient' style='display:" +
          String(g_bridgeConfig.direction == 1 ? "block" : "none") + "'>";
  html += "<label>目标 IP:</label><input name='bTargetIp' value='" +
          g_bridgeConfig.targetIp + "'><br>";
  html += "<label>目标端口:</label><input name='bTargetPort' type='number' "
          "value='" +
          String(g_bridgeConfig.targetPort) + "'><br>";
  html += "</div>";

  html += "<h3>RS485 串口参数</h3>";
  html += "<label>波特率:</label><select name='bBaud'>";
  for (int i = 0; i < 5; i++)
    html += "<option value='" + String(bauds_t[i]) + "'" +
            (g_bridgeConfig.baud == bauds_t[i] ? " selected" : "") + ">" +
            String(bauds_t[i]) + "</option>";
  html += "</select><br>";
  html += "<label>数据位:</label><select name='bData'>";
  html += "<option value='8' " +
          String(g_bridgeConfig.dataBits == 8 ? "selected" : "") +
          ">8位</option>";
  html += "<option value='7' " +
          String(g_bridgeConfig.dataBits == 7 ? "selected" : "") +
          ">7位</option></select><br>";
  html += "<label>校验位:</label><select name='bParity'>";
  html += "<option value='0' " +
          String(g_bridgeConfig.parity == 0 ? "selected" : "") +
          ">None</option>";
  html += "<option value='1' " +
          String(g_bridgeConfig.parity == 1 ? "selected" : "") +
          ">Even</option>";
  html += "<option value='2' " +
          String(g_bridgeConfig.parity == 2 ? "selected" : "") +
          ">Odd</option></select><br>";
  html += "<label>停止位:</label><select name='bStop'>";
  html += "<option value='1' " +
          String(g_bridgeConfig.stopBits == 1 ? "selected" : "") +
          ">1位</option>";
  html += "<option value='2' " +
          String(g_bridgeConfig.stopBits == 2 ? "selected" : "") +
          ">2位</option></select><br>";
  html += "</div>";

  // 5. 寄存器配置区
  html += "<div id='secRegisters' class='card' style='display:" +
          String(g_workMode == WorkMode::SIMULATOR ? "block" : "none") + "'>";
  html += "<h2>寄存器与协议对象定义 (支持 0x/1x/3x/4x)</h2>";
  html += "<p style='color:#336699;font-size:13px;background:#e7f3ff;padding:8px;border-radius:4px;border:1px solid #b8daff'><b>💡 配置指南：</b>上位机 (Master) 推荐设置为 <b>Base-1</b> 模式，以直接映射左侧的 <b>PLC 地址</b>；若您的上位机软件固定为 <b>Base-0</b> 模式，请对照右侧的 <b>数据地址</b> 进行访问。</p>";
  html +=
      "<table "
      "id='regTable'><tr><th>PLC 地址<br>(Base-1)</th><th>数据地址<br>(Base-0)</th><th>名称</th><th>设定值</th><th>状态值</"
      "th><th>类型</th><th>字节序</th><th>动态</th><th>动态时间(s)</"
      "th><th>动态属性</th><th>模拟范围</th><th>操作</th></tr></table>";
  html += "<button type='button' style='margin-top:10px' "
          "onclick='addRegRow()'>+ 添加寄存器</button>";
  html += "</div>";

  // 6. 报文监听区
  html += "<div id='secMonitor' class='card' style='display:" +
          String(g_workMode == WorkMode::SIMULATOR ? "block" : "none") + "'>";
  html += "<h2>实时报文监听</h2>";
  html +=
      "<div id='monitorLog' style='background:#1e1e1e; color:#d4d4d4; "
      "padding:10px; font-family:monospace; height:300px; overflow-y:auto; "
      "font-size:12px; border-radius:4px; line-height:1.5'>等待数据...</div>";
  html += "<button type='button' style='margin-top:10px; background:#6c757d' "
          "onclick='uiClearMonitor()'>清空屏幕</button>";
  html += "<button type='button' id='btnPause' style='margin-top:10px; "
          "margin-left:10px; background:#336699' "
          "onclick='uiTogglePause()'>暂停接收</button>";
  html += "</div>";  // 关闭 secMonitor

  // 7. 固件维护模式
  html += "<div id='secOta' class='card' style='display:" +
          String(g_workMode == WorkMode::FIRMWARE_UPDATE ? "block" : "none") + "'>";
  html += "<h2>系统固件在线更新 (OTA)</h2>";
    if (g_otaUpdateFound) {
      html += "      <div style='margin-bottom:10px'><b style='color:#28a745;font-size:16px'>发现新版本: v" + g_otaRemoteVersion + "</b></div>";
      html += "      <div style='font-size:13px;color:#444;background:#f8f9fa;padding:12px;border-radius:6px;border-left:4px solid #17a2b8;white-space:pre-wrap;line-height:1.5;text-align:left'>";
      html += g_otaChangelog;
      html += "      </div>";
      html += "      <button type='button' id='btnDoUpdate' onclick='uiDoUpdate()' style='margin-top:15px;background:#dc3545'>立即升级</button>";
    } else {
      html += "      <div id='otaInfo' style='font-size:14px;color:#666'>点击下方按钮检查云端是否有新版本...</div>";
      html += "      <button type='button' onclick='uiCheckOta()' style='margin-top:10px'>立即检查更新</button>";
      html += "      <button type='button' id='btnDoUpdate' style='margin-top:10px;margin-left:10px;background:#dc3545;display:none' onclick='uiDoUpdate()'>立即升级</button>";
    }

  // 回滚功能区
  {
    String backupVer;
    bool canRollback;
    getBackupPartitionInfo(backupVer, canRollback);
    html += "<hr style='margin:20px 0;border-color:#eee'>";
    html += "<h3>固件回滚</h3>";
    if (canRollback) {
      html += "<p style='font-size:13px;color:#666'>备用分区检测到历史版本: <b style='color:#17a2b8'>v" + backupVer + "</b></p>";
      html += "<button type='button' onclick='uiRollback()' style='background:#e67e22;margin-top:5px'>⏪ 回滚到 v" + backupVer + "</button>";
    } else {
      html += "<p style='font-size:13px;color:#999'>备用分区暂无可用的历史固件（首次烧录或分区已被覆盖）。</p>";
    }
  }
  html += "</div>";
  
  // 8. 模式 E: WiFi 转以太网透传模式配置
  html += "<div id='secEthWifi' class='card' style='display:" +
          String(g_workMode == WorkMode::ETH_WIFI_BRIDGE ? "block" : "none") + "'>";
  html += "<h2>WiFi 转以太网透传模式配置</h2>";
  html += "<p style='color:#336699;font-size:13px;background:#e7f3ff;padding:8px;border-radius:4px;border:1px solid #b8daff'><b>💡 说明：</b>AnyPort 将在 WiFi 接口上开启监听，并将接收到的数据转发给以太网引脚连接的特定 IP 设备。</p>";
  html += "<label>监听端口 (WiFi):</label><input name='ewLPort' type='number' value='" + String(g_ethWifiConfig.listenPort) + "'><br>";
  html += "<label>目标 IP (Ethernet):</label><input name='ewTIp' value='" + g_ethWifiConfig.targetIp + "'><br>";
  html += "<label>目标端口 (Ethernet):</label><input name='ewTPort' type='number' value='" + String(g_ethWifiConfig.targetPort) + "'><br>";
  html += "</div>";

  html += "<button type='submit' class='save-btn'>保存并重启设备</button>";
  html += "</form>";

  html += "<script>";
  html +=
      "function uiToggleMode(m){ "
      "document.getElementById('secGateway').style.display=(m=='0'?'block':'none'); "
      "document.getElementById('secEthConfig').style.display=(m!='2'&&m!='4'?'block':'none'); "
      "document.getElementById('secSimulator').style.display=(m=='1'?'block':'none'); "
      "document.getElementById('secRegisters').style.display=(m=='1'?'block':'none'); "
      "document.getElementById('secMonitor').style.display=(m=='1'?'block':'none'); "
      "document.getElementById('secTransparent').style.display=(m=='2'?'block':'none'); "
      "document.getElementById('secBridge').style.display=(m=='3'?'block':'none'); "
      "document.getElementById('secOta').style.display=(m=='4'?'block':'none'); "
      "document.getElementById('secEthWifi').style.display=(m=='5'?'block':'none'); }";
  html += "function uiToggleBridge(d){ "
          "document.getElementById('bridgeTcpServer').style.display=(d=='0'?'"
          "block':'none'); "
          "document.getElementById('bridgeTcpClient').style.display=(d=='1'?'"
          "block':'none'); }";
  html += "function uiToggleBridgeMode(m){ "
          "document.getElementById('bridgeSlaveId').style.display=(m=='1'?'"
          "block':'none'); }";
  html += "function "
          "addRegRow(d={addr:40001,name:'',targetVal:0,val:0,type:4,endian:0,"
          "isDyn:false,dynInterval:1,dynMode:0,min:0,max:100}){";
  html += " const padAddr=(a)=>(a+'').padStart(6,'0');";
  html += " const getOffset=(a)=>{ let v=parseInt(a); let o=0; "
          "if(v>=400001)o=v-400001; else if(v>=300001)o=v-300001; "
          "else if(v>=100001)o=v-100001; "
          "else if(v>=40001&&v<=49999)o=v-40001; "
          "else if(v>=30001&&v<=39999)o=v-30001; "
          "else if(v>=10001&&v<=19999)o=v-10001; "
          "else if(v>=1&&v<=9999)o=v-1; else o=v; "
          "if(isNaN(o))return '--'; "
          "return o.toString(16).toUpperCase().padStart(4,'0')+'H ('+o+')'; };";
  html += " let t=document.getElementById('regTable'); let r=t.insertRow(-1); "
          "r.id='reg-'+d.addr; ";
  html +=
      " r.innerHTML=`<td><input class='addr' value='${padAddr(d.addr)}' "
      "style='width:75px' oninput='this.parentElement.nextElementSibling.innerText=getOffset(this.value)' "
      "onblur='this.value=padAddr(this.value)'></td>` + ";
  html += " `<td class='offset' style='color:#666;font-size:12px;width:100px;white-space:nowrap'>${getOffset(d.addr)}</td>` + ";
  html += " `<td><input class='name' value='${d.name}' style='width:90%; "
          "min-width:65px; max-width:100px; box-sizing:border-box'></td>` + ";
  html += " `<td><input class='targetVal' value='${d.targetVal}' "
          "style='width:95%; min-width:80px; box-sizing:border-box; "
          "color:#e67e22; font-weight:bold'></td>` + ";
  html += " `<td><input class='val status-val' value='${d.val}' "
          "style='width:95%; min-width:80px; box-sizing:border-box; "
          "color:#2980b9; font-weight:bold' readonly></td>` + ";
  html += " `<td><select class='type' style='width:85px'><option value='0' "
          "${d.type==0?'selected':''}>Int16</option><option value='1' "
          "${d.type==1?'selected':''}>UInt16</option><option value='2' "
          "${d.type==2?'selected':''}>Int32</option><option value='3' "
          "${d.type==3?'selected':''}>UInt32</option><option value='4' "
          "${d.type==4?'selected':''}>Float32</option><option value='5' "
          "${d.type==5?'selected':''}>String</option><option value='6' "
          "${d.type==6?'selected':''}>Bit</option></select></td>` + ";
  html += " `<td><select class='endian' style='width:75px'><option value='0' "
          "${d.endian==0?'selected':''}>ABCD</option><option value='1' "
          "${d.endian==1?'selected':''}>DCBA</option><option value='2' "
          "${d.endian==2?'selected':''}>BADC</option><option value='3' "
          "${d.endian==3?'selected':''}>CDAB</option></select></td>` + ";
  html +=
      " `<td style='width:40px'><input type='checkbox' class='isDyn' "
      "${d.isDyn?'checked':''} style='width:auto; cursor:pointer'></td>` + ";
  html += " `<td><input class='dynInterval' value='${d.dynInterval}' "
          "style='width:40px'></td>` + ";
  html += " `<td><select class='dynMode' style='width:70px'><option value='0' "
          "${d.dynMode==0?'selected':''}>随机</option><option value='1' "
          "${d.dynMode==1?'selected':''}>加1</option><option value='2' "
          "${d.dynMode==2?'selected':''}>减1</option></select></td>` + ";
  html += " `<td><input class='range' value='${d.min}-${d.max}' "
          "style='width:70px'></td>` + ";
  html +=
      " `<td><button type='button' "
      "onclick='this.parentElement.parentElement.remove()'>x</button></td>`; ";
  html += "}";
  html += "async function refreshRegValues(){ if(document.activeElement && "
          "document.activeElement.classList.contains('targetVal'))return; try{ "
          "let r=await fetch('/api/registers'); let data=await r.json(); "
          "data.forEach(d=>{ let row=document.getElementById('reg-'+d.addr); "
          "if(row){ let sv=row.querySelector('.status-val'); if(sv) "
          "sv.value=d.val; } }); }catch(e){} }";
  html += "let monitorPaused=false; function uiTogglePause(){ "
          "monitorPaused=!monitorPaused; "
          "document.getElementById('btnPause').innerText=monitorPaused?'"
          "继续接收':'暂停接收'; "
          "document.getElementById('btnPause').style.background=monitorPaused?'"
          "#28a745':'#336699'; }";
  html += "async function uiClearMonitor(){ "
          "document.getElementById('monitorLog').innerHTML=''; await "
          "fetch('/api/monitor/clear',{method:'POST'}); }";
  html += "async function refreshMonitor(){ "
          "if(document.getElementById('secMonitor').style.display=='none' || "
          "monitorPaused)return; try{ "
          "let r=await fetch('/api/monitor'); let logs=await r.json(); let "
          "c=document.getElementById('monitorLog'); "
          "if(logs.length>0){ let h=''; logs.forEach(l=>{ let "
          "color=l.dir=='TX'?'#ce9178':'#9cdcfe'; "
          "let ms=l.t%1000; let s=Math.floor(l.t/1000)%60; let "
          "m=Math.floor(l.t/60000); "
          "let "
          "ts=`${(m+'').padStart(2,'0')}:${(s+'').padStart(2,'0')}.${(ms+'')."
          "padStart(3,'0')}`; "
          "h+=`<div><span style='color:#808080'>[${ts}]</span> <span "
          "style='color:${color};font-weight:bold'>${l.dir}</span> <span "
          "style='color:#4ec9b0'>[${l.type}]</span> ${l.hex}</div>`; }); "
          "c.innerHTML=h; c.scrollTop=c.scrollHeight; } else "
          "if(logs.length==0) { c.innerHTML='等待数据...'; } }catch(e){} }";
  html += "setInterval(refreshRegValues, 1000);";
  html += "setInterval(refreshMonitor, 1000);";
  html += "document.getElementById('mainForm').onsubmit=async function(e){";
  html += " if(this.submitting) return; "
          "if(document.getElementsByName('workMode')[0].value=='1'){";
  html +=
      "  e.preventDefault(); this.submitting=true; let regs=[]; "
      "document.querySelectorAll('#regTable tr').forEach((tr,i)=>{ "
      "if(i==0)return; let rng=tr.querySelector('.range').value.split('-'); "
      "regs.push({addr:parseInt(tr.querySelector('.addr').value),name:tr."
      "querySelector('.name').value,targetVal:parseFloat(tr.querySelector('."
      "targetVal').value),val:parseFloat(tr.querySelector('.status-val').value)"
      ",type:parseInt(tr.querySelector('.type').value),endian:parseInt(tr."
      "querySelector('.endian').value),isDyn:tr.querySelector('.isDyn')."
      "checked,dynInterval:parseInt(tr.querySelector('.dynInterval').value),"
      "dynMode:parseInt(tr.querySelector('.dynMode').value),min:parseFloat(rng["
      "0]||0),max:parseFloat(rng[1]||100)}); });";
  html += "  try {";
  html += "    let r1 = await "
          "fetch('/api/registers',{method:'POST',body:JSON.stringify(regs)});";
  html += "    if(!r1.ok) throw new Error('保存寄存器失败');";
  html += "    let "
          "cfg={rtuEnabled:document.getElementsByName('simRtuEnabled')[0]."
          "value=='1',baud:parseInt(document.getElementsByName('simBaud')[0]."
          "value),parity:parseInt(document.getElementsByName('simParity')[0]."
          "value),stopBits:parseInt(document.getElementsByName('simStop')[0]."
          "value),dataBits:parseInt(document.getElementsByName('simDataBits')["
          "0].value),unitId:parseInt(document.getElementsByName('simUnitId')[0]"
          ".value),tcpEnabled:document.getElementsByName('simTcpEnabled')[0]."
          "value=='1',tcpPort:parseInt(document.getElementsByName('simTcpPort')"
          "[0].value),netInterface:parseInt(document.getElementsByName('simNetInt')"
          "[0].value),monitorEnabled:document.getElementsByName('"
          "simMonitorEnabled')[0].checked,monitorFilter:parseInt(document."
          "getElementsByName('simMonitorFilter')[0].value)};";
  html += "    let r2 = await "
          "fetch('/api/simConfig',{method:'POST',body:JSON.stringify(cfg)});";
  html += "    if(!r2.ok) throw new Error('保存模拟器配置失败');";
  html +=
      "    await new Promise(r => setTimeout(r, 500));"; // 额外等待半秒确保
                                                         // Flash 写入完全结束
  html += "    this.submit();";
  html += "  } catch(e) { alert(e.message); this.submitting=false; }";
  html += " }";
  html += "};";  // 关闭 onsubmit 函数
  html += "async function uiCheckOta(){ "
          "let s=document.getElementById('otaInfo'); s.innerText='正在连接云端服务...'; "
          "try{ let r=await fetch('/api/ota/check'); let data=await r.json(); "
          "if(data.found){ "
          "let mdLog = data.changelog.replace(/</g, '&lt;').replace(/>/g, '&gt;')"
          ".replace(/^###\\s+(.*)/gm, '<span style=\"font-size:15px;font-weight:bold;color:#17a2b8;display:inline-block;margin-top:8px\">$1</span>')"
          ".replace(/\\*\\*(.*?)\\*\\*/g, '<b style=\"color:#222\">$1</b>')"
          ".replace(/`(.*?)`/g, '<code style=\"background:#e9ecef;padding:2px 5px;border-radius:4px;color:#d63384\">$1</code>'); "
          "s.innerHTML=`<div style='margin-bottom:10px'><b style='color:#28a745;font-size:16px'>发现新版本: v${data.version}</b></div>"
          "<div style='font-size:13px;color:#444;background:#f8f9fa;padding:12px;border-radius:6px;border-left:4px solid #17a2b8;white-space:pre-wrap;line-height:1.6;text-align:left'>${mdLog}</div>`; "
          "document.getElementById('btnDoUpdate').style.display='inline-block'; } "
          "else { s.innerText='当前已是最新版本 ('+data.version+')'; } "
          "}catch(e){ s.innerText='检测失败，请检查网络连接'; } }";
  html += "async function uiDoUpdate(){ "
          "if(!confirm('确定要开始固件升级吗？升级过程中请勿断电，设备将自动重启。'))return; "
          "let btn = document.getElementById('btnDoUpdate'); btn.disabled=true; btn.innerText='正在校验固件...'; "
          "try{ "
          "  let r = await fetch('/api/ota/do',{method:'POST'}); "
          "  if(!r.ok){ throw new Error('云端未找到固件文件或链接已失效'); } "
          "  document.body.innerHTML='<div style=\"text-align:center;margin-top:100px\"><h1>正在执行系统升级...</h1><p>请勿切断电源，预计需要 1-2 分钟。</p><progress id=\"p\" style=\"width:300px\"></progress></div>'; "
          "  setTimeout(()=>location.href='/', 80000); "
          "}catch(e){ alert('升级失败: ' + e.message); btn.disabled=false; btn.innerText='立即升级'; } }";
  html += "async function uiRollback(){ "
          "if(!confirm('确认要回滚到备用分区的历史固件吗？\\n回滚后设备将自动重启。'))return; "
          "try{ let r = await fetch('/api/ota/rollback',{method:'POST'}); "
          "let d = await r.json(); "
          "if(d.ok){ document.body.innerHTML='<div style=\"text-align:center;margin-top:100px\"><h1>正在回滚固件...</h1><p>设备将在数秒后自动重启，请稍候。</p></div>'; "
          "setTimeout(()=>location.href=\'/\', 10000); } "
          "else { alert('回滚失败: ' + (d.error || '未知错误')); } "
          "}catch(e){ alert('回滚请求失败: ' + e.message); } }";
  html += "fetch('/api/"
          "registers').then(r=>r.json()).then(data=>data.forEach(r=>addRegRow("
          "r)));";
  html += "(function pollOtaBadge(){"
          "setTimeout(async function(){"
          "try{ let r=await fetch('/api/ota/status'); let d=await r.json(); "
          "if(d.found && !document.getElementById('otaBadge')){"
          "let h=document.getElementById('mainTitle');"
          "let s=document.createElement('span'); s.id='otaBadge'; s.className='badge';"
          "s.style.cssText='background:#dc3545;color:white;border-color:#bd2130';"
          "s.innerText='\u6709\u65b0\u7248\u672c';"
          "h.insertBefore(s,h.querySelector('span'));"
          "} }catch(e){} pollOtaBadge(); }, 30000); })();";
  html += "</script></body></html>";

  g_httpServer.send(200, "text/html; charset=utf-8", html);
}

static void handleHttpConfig() {
  String wifiSsid = g_httpServer.arg("wifiSsid");
  String wifiPwd = g_httpServer.arg("wifiPwd");
  String mqttHost = g_httpServer.arg("mqttHost");
  String mqttPortStr = g_httpServer.arg("mqttPort");
  String mqttPath = g_httpServer.arg("mqttPath");
  String mqttUser = g_httpServer.arg("mqttUser");
  String mqttPwd = g_httpServer.arg("mqttPwd");
  String mqttPrefix = g_httpServer.arg("mqttPrefix");
  String mqttSite = g_httpServer.arg("mqttSite");
  String mqttGateway = g_httpServer.arg("mqttGateway");
  String ntpServer = g_httpServer.arg("ntpServer");
  String ntpIntervalStr = g_httpServer.arg("ntpInterval");

  String ethIpStr = g_httpServer.arg("ethIp");
  String ethMaskStr = g_httpServer.arg("ethMask");
  String ethGwStr = g_httpServer.arg("ethGw");
  String ethDnsStr = g_httpServer.arg("ethDns");

  if (!g_prefs.begin("anyport", false)) {
    g_httpServer.send(500, "text/plain", "NVS Error");
    return;
  }

  // 模式切换
  String workModeStr = g_httpServer.arg("workMode");
  if (workModeStr.length() > 0) {
    g_prefs.putUChar("workMode", (uint8_t)workModeStr.toInt());
  }

  // WiFi 保存
  if (wifiSsid.length() > 0 && wifiSsid.length() <= 32) {
    g_prefs.putString("wifiSsid", wifiSsid);
    if (wifiPwd.length() > 0 && wifiPwd != "********") {
      g_prefs.putString("wifiPwd", wifiPwd);
      g_wifiStaConfig.password = wifiPwd;
    }
    g_wifiStaConfig.ssid = wifiSsid;
    g_wifiStaConfig.valid = true;
  }

  // mDNS 保存
  String mdnsName = g_httpServer.arg("mdnsName");
  if (mdnsName.length() > 0 && mdnsName.length() <= 16) {
    g_prefs.putString("mdnsName", mdnsName);
    g_mdnsName = mdnsName;
  }

  // MQTT 保存
  if (mqttHost.length() > 0) {
    g_prefs.putString("mqttHost", mqttHost);
    int port = mqttPortStr.toInt();
    if (port > 0 && port <= 65535)
      g_prefs.putUShort("mqttPort", (uint16_t)port);
    g_prefs.putString("mqttPath", mqttPath);
    g_prefs.putString("mqttUser", mqttUser);
    if (mqttPwd.length() > 0 && mqttPwd != "********") {
      g_prefs.putString("mqttPwd", mqttPwd);
    }
    g_prefs.putString("mqttPrefix", mqttPrefix);
    g_prefs.putString("mqttSite", mqttSite);
    g_prefs.putString("mqttGateway", mqttGateway);

    g_mqttConfig.host = mqttHost;
    g_mqttConfig.port = (uint16_t)port;
    g_mqttConfig.path = mqttPath;
    g_mqttConfig.username = mqttUser;
    if (mqttPwd != "********")
      g_mqttConfig.password = mqttPwd;
    g_mqttConfig.topicPrefix = mqttPrefix;
    g_mqttConfig.siteId = mqttSite;
    g_mqttConfig.gatewayId = mqttGateway;
    g_mqttConfig.valid = true;
  }

  // NTP 保存
  if (ntpServer.length() > 0) {
    g_prefs.putString("ntpServer", ntpServer);
    uint32_t ntpInt = ntpIntervalStr.toInt();
    if (ntpInt == 0)
      ntpInt = 3600;
    g_prefs.putUInt("ntpInterval", ntpInt);
    g_ntpConfig.server = ntpServer;
    g_ntpConfig.interval = ntpInt;
    g_ntpConfig.valid = true;
  }

  // 以太网保存
  if (ethIpStr.length() > 0 && ethMaskStr.length() > 0) {
    IPAddress ip, mask, gw, dns;
    extern bool parseIpAddress(const char *str, IPAddress &outIp);
    if (parseIpAddress(ethIpStr.c_str(), ip) &&
        parseIpAddress(ethMaskStr.c_str(), mask)) {
      parseIpAddress(ethGwStr.c_str(), gw);
      parseIpAddress(ethDnsStr.c_str(), dns);
      g_prefs.putUInt("ethIp", (uint32_t)ip);
      g_prefs.putUInt("ethMask", (uint32_t)mask);
      g_prefs.putUInt("ethGw", (uint32_t)gw);
      g_prefs.putUInt("ethDns", (uint32_t)dns);

      g_ethConfig.ip = ip;
      g_ethConfig.subnet = mask;
      g_ethConfig.gateway = gw;
      g_ethConfig.dns = dns;
      g_ethConfig.valid = true;
    }
  }

  // 透传模式保存
  String tBaud = g_httpServer.arg("tBaud");
  if (tBaud.length() > 0) {
    g_transBaud = tBaud.toInt();
    g_transDataBits = g_httpServer.arg("tData").toInt();
    g_transParity = g_httpServer.arg("tParity").toInt();
    g_transStopBits = g_httpServer.arg("tStop").toInt();

    g_prefs.putUInt("tBaud", g_transBaud);
    g_prefs.putUChar("tData", g_transDataBits);
    g_prefs.putUChar("tParity", g_transParity);
    g_prefs.putUChar("tStop", g_transStopBits);
  }

  // Bridge 模式保存
  String bDirStr = g_httpServer.arg("bDir");
  if (bDirStr.length() > 0) {
    g_bridgeConfig.direction = bDirStr.toInt();
    g_bridgeConfig.bridgeMode = g_httpServer.arg("bMode").toInt();
    g_bridgeConfig.slaveId = (uint8_t)g_httpServer.arg("bSlaveId").toInt();
    g_bridgeConfig.netInterface = (uint8_t)g_httpServer.arg("bNetInt").toInt();
    g_bridgeConfig.tcpPort = g_httpServer.arg("bTcpPort").toInt();
    g_bridgeConfig.targetIp = g_httpServer.arg("bTargetIp");
    g_bridgeConfig.targetPort = g_httpServer.arg("bTargetPort").toInt();
    g_bridgeConfig.baud = g_httpServer.arg("bBaud").toInt();
    g_bridgeConfig.dataBits = g_httpServer.arg("bData").toInt();
    g_bridgeConfig.parity = g_httpServer.arg("bParity").toInt();
    g_bridgeConfig.stopBits = g_httpServer.arg("bStop").toInt();

    g_prefs.putUChar("bDir", g_bridgeConfig.direction);
    g_prefs.putUChar("bMode", g_bridgeConfig.bridgeMode);
    g_prefs.putUChar("bSlaveId", g_bridgeConfig.slaveId);
    g_prefs.putUChar("bNetInt", g_bridgeConfig.netInterface);
    g_prefs.putUShort("bTcpPort", g_bridgeConfig.tcpPort);
    g_prefs.putString("bTargetIp", g_bridgeConfig.targetIp);
    g_prefs.putUShort("bTargetPort", g_bridgeConfig.targetPort);
    g_prefs.putUInt("bBaud", g_bridgeConfig.baud);
    g_prefs.putUChar("bData", g_bridgeConfig.dataBits);
    g_prefs.putUChar("bParity", g_bridgeConfig.parity);
    g_prefs.putUChar("bStop", g_bridgeConfig.stopBits);
  }

  // Eth-WiFi Bridge 保存
  String ewLPort = g_httpServer.arg("ewLPort");
  if (ewLPort.length() > 0) {
      g_ethWifiConfig.listenPort = (uint16_t)ewLPort.toInt();
      g_ethWifiConfig.targetIp = g_httpServer.arg("ewTIp");
      g_ethWifiConfig.targetPort = (uint16_t)g_httpServer.arg("ewTPort").toInt();
      
      g_prefs.putUShort("ewLPort", g_ethWifiConfig.listenPort);
      g_prefs.putString("ewTIp", g_ethWifiConfig.targetIp);
      g_prefs.putUShort("ewTPort", g_ethWifiConfig.targetPort);
  }

  g_prefs.end();
  g_needRestart = true;
  g_httpServer.send(
      200, "text/html; charset=utf-8",
      "<html><head><meta "
      "charset='utf-8'></head><body><h1>配置已保存，设备即将重启...</"
      "h1><script>setTimeout(()=>location.href='/', "
      "3000);</script></body></html>");
}

static void handleOtaCheck() {
  int result = checkOtaUpdate();
  DynamicJsonDocument doc(512);
  doc["found"] = (result == 1);
  doc["version"] = g_otaRemoteVersion != "" ? g_otaRemoteVersion : FIRMWARE_VERSION;
  doc["changelog"] = g_otaChangelog;
  String out;
  serializeJson(doc, out);
  g_httpServer.send(200, "application/json", out);
}

static void handleOtaUpdate() {
  if (validateFirmwareExistence()) {
    g_httpServer.send(200, "text/plain", "OK");
    delay(500);
    executeFirmwareUpdate();
  } else {
    g_httpServer.send(404, "text/plain", "Firmware not found");
  }
}

static void handleOtaStatus() {
  String json = "{\"found\":" + String(g_otaUpdateFound ? "true" : "false") +
                ",\"version\":\"" + g_otaRemoteVersion + "\"}";
  g_httpServer.sendHeader("Cache-Control", "no-cache");
  g_httpServer.send(200, "application/json", json);
}

static void handleOtaRollback() {
  if (executeRollback()) {
    g_httpServer.send(200, "application/json", "{\"ok\":true}");
    delay(1000);
    ESP.restart();
  } else {
    g_httpServer.send(200, "application/json", "{\"ok\":false,\"error\":\"备用分区无有效固件\"}");
  }
}

static void initHttpServer() {
  g_httpServer.on("/", HTTP_GET, handleHttpRoot);
  g_httpServer.on("/config", HTTP_POST, handleHttpConfig);
  g_httpServer.on("/api/ota/check", HTTP_GET, handleOtaCheck);
  g_httpServer.on("/api/ota/do", HTTP_POST, handleOtaUpdate);
  g_httpServer.on("/api/ota/status", HTTP_GET, handleOtaStatus);
  g_httpServer.on("/api/ota/rollback", HTTP_POST, handleOtaRollback);
  g_httpServer.begin();
}
