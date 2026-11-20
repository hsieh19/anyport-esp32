#include "ConfigStore.h"
#include "../domain/SimulatorDomain.h"
#include <Preferences.h>

// -----------------------
// 1. 全局配置上下文定义
// -----------------------
WorkMode g_workMode = WorkMode::GATEWAY;
volatile bool g_needRestart = false;
uint8_t g_rs485Channel = 0;
String g_otaApiBase = "";

Rs485PortConfig g_rs485_1Config = {9600, 8, 0, 1};
Rs485PortConfig g_rs485_2Config = {9600, 8, 0, 1};
EthStaticConfig g_ethConfig = {};
WifiStaConfig g_wifiStaConfig = {};
MqttRuntimeConfig g_mqttConfig = {};

uint32_t g_transBaud = 9600;
uint8_t g_transDataBits = 8;
uint8_t g_transParity = 0;
uint8_t g_transStopBits = 1;

BridgeConfig g_bridgeConfig = {0, 0, 1, 0, 502, "", 502, 9600, 1, 0, 8};
EthWifiConfig g_ethWifiConfig = {0, 502, "192.168.1.100", 502, 0};
DualMasterConfig g_dualMasterConfig = {"AnyPort 网关", 9600, 8, 0, 1, 9502, 1000, 30};
NetInterface g_netInterface = NetInterface::AUTO;
SystemTelemetry g_telemetry = {0, 0, 0, 0};

static Preferences s_prefs;
static constexpr const char *NVS_NS = "anyport";

// -----------------------
// 2. 系统配置加载与保存
// -----------------------
void configLoadAll() {
  s_prefs.begin(NVS_NS, true);
  g_workMode = static_cast<WorkMode>(s_prefs.getUChar("workMode", 0));
  g_otaApiBase = s_prefs.getString("otaApi", "");
  if (g_otaApiBase == "https://your-firmware-worker.workers.dev") {
    g_otaApiBase = "";
  }
  g_rs485Channel = s_prefs.getUChar("rsCh", 0) == 1 ? 1 : 0;

  if (s_prefs.isKey("ethIp") && s_prefs.isKey("ethMask")) {
    uint32_t ipRaw = s_prefs.getUInt("ethIp", 0);
    uint32_t maskRaw = s_prefs.getUInt("ethMask", 0);
    if (ipRaw != 0 && maskRaw != 0) {
      g_ethConfig.ip = IPAddress(ipRaw);
      g_ethConfig.subnet = IPAddress(maskRaw);
      g_ethConfig.gateway = IPAddress(s_prefs.getUInt("ethGw", 0));
      g_ethConfig.dns = IPAddress(s_prefs.getUInt("ethDns", 0));
      g_ethConfig.valid = true;
    }
  }

  if (s_prefs.isKey("wifiSsid")) {
    g_wifiStaConfig.ssid = s_prefs.getString("wifiSsid", "");
    g_wifiStaConfig.password = s_prefs.getString("wifiPwd", "");
    g_wifiStaConfig.valid = true;
  }

  if (s_prefs.isKey("wifiIp") && s_prefs.isKey("wifiMask")) {
    uint32_t ipRaw = s_prefs.getUInt("wifiIp", 0);
    uint32_t maskRaw = s_prefs.getUInt("wifiMask", 0);
    if (ipRaw != 0 && maskRaw != 0) {
      g_wifiStaConfig.ip = IPAddress(ipRaw);
      g_wifiStaConfig.subnet = IPAddress(maskRaw);
      g_wifiStaConfig.gateway = IPAddress(s_prefs.getUInt("wifiGw", 0));
      g_wifiStaConfig.dns = IPAddress(s_prefs.getUInt("wifiDns", 0));
      g_wifiStaConfig.staticIp = true;
    }
  }

  if (s_prefs.isKey("mqttHost")) {
    g_mqttConfig.host = s_prefs.getString("mqttHost", MQTT_BROKER_HOST);
    g_mqttConfig.port = s_prefs.getUShort("mqttPort", MQTT_BROKER_PORT);
    g_mqttConfig.path = s_prefs.getString("mqttPath", MQTT_BROKER_PATH);
    g_mqttConfig.username = s_prefs.getString("mqttUser", "");
    g_mqttConfig.password = s_prefs.getString("mqttPwd", "");
    g_mqttConfig.topicPrefix =
        s_prefs.getString("mqttPrefix", MQTT_TOPIC_PREFIX);
    g_mqttConfig.siteId = s_prefs.getString("mqttSite", MQTT_SITE_ID);
    g_mqttConfig.gatewayId =
        s_prefs.getString("mqttGateway", MQTT_GATEWAY_ID);
    g_mqttConfig.valid = true;
  } else {
    g_mqttConfig.host = MQTT_BROKER_HOST;
    g_mqttConfig.port = MQTT_BROKER_PORT;
    g_mqttConfig.path = MQTT_BROKER_PATH;
    g_mqttConfig.username = MQTT_USERNAME;
    g_mqttConfig.password = MQTT_PASSWORD;
    g_mqttConfig.topicPrefix = MQTT_TOPIC_PREFIX;
    g_mqttConfig.siteId = MQTT_SITE_ID;
    g_mqttConfig.gatewayId = MQTT_GATEWAY_ID;
    g_mqttConfig.valid = true;
  }

  // RS485-1 (GPIO 2/3) 全局端口配置加载
  g_rs485_1Config.baud =
      s_prefs.getUInt("rs1Baud", s_prefs.getUInt("tBaud", 9600));
  g_rs485_1Config.dataBits =
      s_prefs.getUChar("rs1Data", s_prefs.getUChar("tData", 8));
  g_rs485_1Config.parity =
      s_prefs.getUChar("rs1Parity", s_prefs.getUChar("tParity", 0));
  g_rs485_1Config.stopBits =
      s_prefs.getUChar("rs1Stop", s_prefs.getUChar("tStop", 1));

  // RS485-2 (GPIO 5/4) 全局端口配置加载
  g_rs485_2Config.baud = s_prefs.getUInt("rs2Baud", 9600);
  g_rs485_2Config.dataBits = s_prefs.getUChar("rs2Data", 8);
  g_rs485_2Config.parity = s_prefs.getUChar("rs2Parity", 0);
  g_rs485_2Config.stopBits = s_prefs.getUChar("rs2Stop", 1);

  g_transBaud = s_prefs.getUInt("tBaud", 9600);
  g_transDataBits = s_prefs.getUChar("tData", 8);
  g_transParity = s_prefs.getUChar("tParity", 0);
  g_transStopBits = s_prefs.getUChar("tStop", 1);

  // Bridge 加载
  g_bridgeConfig.direction = s_prefs.getUChar("bDir", 0);
  g_bridgeConfig.bridgeMode = s_prefs.getUChar("bMode", 0);
  g_bridgeConfig.slaveId = s_prefs.getUChar("bSlaveId", 1);
  g_bridgeConfig.netInterface = s_prefs.getUChar("bNetInt", 0);
  g_bridgeConfig.tcpPort = s_prefs.getUShort("bTcpPort", 502);
  g_bridgeConfig.targetIp = s_prefs.getString("bTargetIp", "192.168.1.100");
  g_bridgeConfig.targetPort = s_prefs.getUShort("bTargetPort", 502);
  g_bridgeConfig.baud = s_prefs.getUInt("bBaud", 9600);
  g_bridgeConfig.dataBits = s_prefs.getUChar("bData", 8);
  g_bridgeConfig.parity = s_prefs.getUChar("bParity", 0);
  g_bridgeConfig.stopBits = s_prefs.getUChar("bStop", 1);

  // Eth-WiFi Bridge 加载
  g_ethWifiConfig.direction = s_prefs.getUChar("ewDir", 0);
  g_ethWifiConfig.listenPort = s_prefs.getUShort("ewLPort", 502);
  g_ethWifiConfig.targetIp = s_prefs.getString("ewTIp", "192.168.1.100");
  g_ethWifiConfig.targetPort = s_prefs.getUShort("ewTPort", 502);
  g_ethWifiConfig.protocol = s_prefs.getUChar("ewProto", 0);

  // 全局网关名称与双主站模式参数加载
  g_dualMasterConfig.gatewayName =
      s_prefs.getString("gwName", "AnyPort 网关");
  g_dualMasterConfig.masterBaud =
      s_prefs.getUInt("dmBaud", g_rs485_1Config.baud);
  g_dualMasterConfig.masterData =
      s_prefs.getUChar("dmData", g_rs485_1Config.dataBits);
  g_dualMasterConfig.masterParity =
      s_prefs.getUChar("dmParity", g_rs485_1Config.parity);
  g_dualMasterConfig.masterStop =
      s_prefs.getUChar("dmStop", g_rs485_1Config.stopBits);
  g_dualMasterConfig.wifiPort = s_prefs.getUShort("dmWPort", 9502);
  g_dualMasterConfig.masterTimeout = s_prefs.getUShort("dmTimeout", 1000);
  g_dualMasterConfig.heartbeatInterval = s_prefs.getUInt("hbInt", 30);

  s_prefs.end();
}

bool configSaveSystem(uint8_t workMode, uint8_t rsCh, const String &otaApi,
                      const WifiStaConfig &wifi, const EthStaticConfig &eth,
                      const MqttRuntimeConfig &mqtt,
                      const Rs485PortConfig &rs1,
                      const Rs485PortConfig &rs2, uint32_t tBaud,
                      uint8_t tData, uint8_t tParity, uint8_t tStop,
                      BridgeConfig &br, const EthWifiConfig &ew,
                      DualMasterConfig &dm) {
  if (!s_prefs.begin(NVS_NS, false)) return false;

  s_prefs.putUChar("workMode", workMode);
  s_prefs.putUChar("rsCh", rsCh);
  s_prefs.putString("otaApi", otaApi);

  if (wifi.valid) {
    s_prefs.putString("wifiSsid", wifi.ssid);
    s_prefs.putString("wifiPwd", wifi.password);
    if (wifi.staticIp) {
      s_prefs.putUInt("wifiIp", static_cast<uint32_t>(wifi.ip));
      s_prefs.putUInt("wifiMask", static_cast<uint32_t>(wifi.subnet));
      s_prefs.putUInt("wifiGw", static_cast<uint32_t>(wifi.gateway));
      s_prefs.putUInt("wifiDns", static_cast<uint32_t>(wifi.dns));
    } else {
      s_prefs.remove("wifiIp");
      s_prefs.remove("wifiMask");
      s_prefs.remove("wifiGw");
      s_prefs.remove("wifiDns");
    }
  } else {
    s_prefs.remove("wifiSsid");
    s_prefs.remove("wifiPwd");
    s_prefs.remove("wifiIp");
    s_prefs.remove("wifiMask");
    s_prefs.remove("wifiGw");
    s_prefs.remove("wifiDns");
  }

  if (eth.valid) {
    s_prefs.putUInt("ethIp", static_cast<uint32_t>(eth.ip));
    s_prefs.putUInt("ethMask", static_cast<uint32_t>(eth.subnet));
    s_prefs.putUInt("ethGw", static_cast<uint32_t>(eth.gateway));
    s_prefs.putUInt("ethDns", static_cast<uint32_t>(eth.dns));
  } else {
    s_prefs.remove("ethIp");
    s_prefs.remove("ethMask");
    s_prefs.remove("ethGw");
    s_prefs.remove("ethDns");
  }

  if (mqtt.host.length()) {
    s_prefs.putString("mqttHost", mqtt.host);
    s_prefs.putUShort("mqttPort", mqtt.port);
    s_prefs.putString("mqttPath", mqtt.path);
    s_prefs.putString("mqttUser", mqtt.username);
    s_prefs.putString("mqttPwd", mqtt.password);
    s_prefs.putString("mqttPrefix", mqtt.topicPrefix);
    s_prefs.putString("mqttSite", mqtt.siteId);
    s_prefs.putString("mqttGateway", mqtt.gatewayId);
  }

  s_prefs.putUInt("rs1Baud", rs1.baud);
  s_prefs.putUChar("rs1Data", rs1.dataBits);
  s_prefs.putUChar("rs1Parity", rs1.parity);
  s_prefs.putUChar("rs1Stop", rs1.stopBits);

  s_prefs.putUInt("rs2Baud", rs2.baud);
  s_prefs.putUChar("rs2Data", rs2.dataBits);
  s_prefs.putUChar("rs2Parity", rs2.parity);
  s_prefs.putUChar("rs2Stop", rs2.stopBits);

  // 保证单串口模式与双主站模式各参数与全局 485 对齐
  const Rs485PortConfig &actRs = (rsCh == 1) ? rs2 : rs1;
  tBaud = actRs.baud;
  tData = actRs.dataBits;
  tParity = actRs.parity;
  tStop = actRs.stopBits;
  br.baud = actRs.baud;
  br.dataBits = actRs.dataBits;
  br.parity = actRs.parity;
  br.stopBits = actRs.stopBits;
  dm.masterBaud = rs1.baud;
  dm.masterData = rs1.dataBits;
  dm.masterParity = rs1.parity;
  dm.masterStop = rs1.stopBits;

  s_prefs.putUInt("tBaud", tBaud);
  s_prefs.putUChar("tData", tData);
  s_prefs.putUChar("tParity", tParity);
  s_prefs.putUChar("tStop", tStop);

  s_prefs.putUChar("bDir", br.direction);
  s_prefs.putUChar("bMode", br.bridgeMode);
  s_prefs.putUChar("bSlaveId", br.slaveId);
  s_prefs.putUChar("bNetInt", br.netInterface);
  s_prefs.putUShort("bTcpPort", br.tcpPort);
  s_prefs.putString("bTargetIp", br.targetIp);
  s_prefs.putUShort("bTargetPort", br.targetPort);
  s_prefs.putUInt("bBaud", br.baud);
  s_prefs.putUChar("bData", br.dataBits);
  s_prefs.putUChar("bParity", br.parity);
  s_prefs.putUChar("bStop", br.stopBits);

  s_prefs.putUChar("ewDir", ew.direction);
  s_prefs.putUShort("ewLPort", ew.listenPort);
  s_prefs.putString("ewTIp", ew.targetIp);
  s_prefs.putUShort("ewTPort", ew.targetPort);
  s_prefs.putUChar("ewProto", ew.protocol);

  if (dm.gatewayName.length() > 0) {
    s_prefs.putString("gwName", dm.gatewayName);
  }
  s_prefs.putUInt("dmBaud", dm.masterBaud);
  s_prefs.putUChar("dmData", dm.masterData);
  s_prefs.putUChar("dmParity", dm.masterParity);
  s_prefs.putUChar("dmStop", dm.masterStop);
  s_prefs.putUShort("dmWPort", dm.wifiPort);
  s_prefs.putUShort("dmTimeout",
                    dm.masterTimeout > 0 ? dm.masterTimeout : 1000);
  s_prefs.putUInt("hbInt",
                  dm.heartbeatInterval > 0 ? dm.heartbeatInterval : 30);
  s_prefs.end();

  // 同步运行时全局
  g_otaApiBase = otaApi;
  g_wifiStaConfig = wifi;
  g_ethConfig = eth;
  g_mqttConfig = mqtt;
  g_rs485_1Config = rs1;
  g_rs485_2Config = rs2;
  g_transBaud = tBaud;
  g_transDataBits = tData;
  g_transParity = tParity;
  g_transStopBits = tStop;
  g_bridgeConfig = br;
  g_ethWifiConfig = ew;
  g_dualMasterConfig = dm;
  return true;
}

bool configSaveWorkMode(uint8_t mode) {
  if (!s_prefs.begin(NVS_NS, false)) return false;
  s_prefs.putUChar("workMode", mode);
  s_prefs.end();
  return true;
}

bool configSaveDualMasterHotReload(const DualMasterConfig &dm) {
  if (!s_prefs.begin(NVS_NS, false)) return false;

  if (dm.gatewayName.length() > 0) {
    s_prefs.putString("gwName", dm.gatewayName);
  }
  s_prefs.putUInt("dmBaud", dm.masterBaud);
  s_prefs.putUChar("dmData", dm.masterData);
  s_prefs.putUChar("dmParity", dm.masterParity);
  s_prefs.putUChar("dmStop", dm.masterStop);
  s_prefs.putUShort("dmWPort", dm.wifiPort);
  s_prefs.putUShort("dmTimeout", dm.masterTimeout);
  s_prefs.putUInt("hbInt", dm.heartbeatInterval);

  // 同步全局 485-1 与 485-2 参数，确保双主站两路串口参数一致
  s_prefs.putUInt("rs1Baud", dm.masterBaud);
  s_prefs.putUChar("rs1Data", dm.masterData);
  s_prefs.putUChar("rs1Parity", dm.masterParity);
  s_prefs.putUChar("rs1Stop", dm.masterStop);

  s_prefs.putUInt("rs2Baud", dm.masterBaud);
  s_prefs.putUChar("rs2Data", dm.masterData);
  s_prefs.putUChar("rs2Parity", dm.masterParity);
  s_prefs.putUChar("rs2Stop", dm.masterStop);
  s_prefs.end();

  g_dualMasterConfig = dm;
  g_rs485_1Config = {dm.masterBaud, dm.masterData, dm.masterParity,
                     dm.masterStop};
  g_rs485_2Config = {dm.masterBaud, dm.masterData, dm.masterParity,
                     dm.masterStop};
  return true;
}

// -----------------------
// 3. 模拟器持久化
// -----------------------
void configLoadSimulator() {
  s_prefs.begin(NVS_NS, true);
  SimulatorGlobalConfig &simCfg = simGetConfig();
  SimulatorVariable *simVars = simGetVariables();
  size_t &simVarCount = simGetVarCount();

  if (s_prefs.isKey("simGlobal") &&
      s_prefs.getBytesLength("simGlobal") == sizeof(simCfg)) {
    s_prefs.getBytes("simGlobal", &simCfg, sizeof(simCfg));
  }

  size_t size = s_prefs.getBytesLength("simVars");
  if (size > 0 && (size % sizeof(SimulatorVariable) == 0) &&
      size <= (MAX_SIM_VARIABLES * sizeof(SimulatorVariable))) {
    s_prefs.getBytes("simVars", simVars, size);
    simVarCount = size / sizeof(SimulatorVariable);
    for (size_t i = 0; i < simVarCount; i++) {
      simVars[i].currentValue = simVars[i].targetValue;
      simWriteValueToPool(simVars[i]);
    }
  } else {
    if (size > 0) {
      APP_LOG("[Simulator] Config size mismatch (%d vs %d), clearing old settings",
              size, sizeof(SimulatorVariable));
      s_prefs.remove("simVars");
    }
    simVarCount = 0;
  }
  s_prefs.end();
}

void configSaveSimulator() {
  if (!s_prefs.begin(NVS_NS, false)) return;
  const SimulatorGlobalConfig &simCfg = simGetConfig();
  const SimulatorVariable *simVars = simGetVariables();
  size_t simVarCount = simGetVarCount();

  s_prefs.putBytes("simGlobal", &simCfg, sizeof(simCfg));
  if (simVarCount > 0) {
    s_prefs.putBytes("simVars", simVars,
                     simVarCount * sizeof(SimulatorVariable));
  } else {
    s_prefs.remove("simVars");
  }
  s_prefs.end();
  delay(100);
}

// -----------------------
// 4. OTA 版本快照持久化
// -----------------------
String configGetPrevFirmwareVersion() {
  s_prefs.begin(NVS_NS, true);
  String ver = s_prefs.getString("prevFwVer", "");
  s_prefs.end();
  return ver;
}

void configSavePrevFirmwareVersion(const String &ver) {
  if (!s_prefs.begin(NVS_NS, false)) return;
  s_prefs.putString("prevFwVer", ver);
  s_prefs.end();
}
