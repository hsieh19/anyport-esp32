#include "AppKernel.h"
#include "../api/HttpApiServer.h"
#include "../domain/AppTypes.h"
#include "../infra/ConfigStore.h"
#include "../infra/NetworkManager.h"
#include "../infra/OtaService.h"
#include "../infra/Rs485Driver.h"
#include "../modes/BridgeMode.h"
#include "../modes/BusMonitorMode.h"
#include "../modes/DualMasterMode.h"
#include "../modes/EthWifiBridgeMode.h"
#include "../modes/GatewayMode.h"
#include "../modes/IWorkMode.h"
#include "../modes/SimulatorMode.h"
#include "../modes/TransparentMode.h"

// 编译期静态实例池（零动态堆分配）
static GatewayMode s_gatewayMode;
static SimulatorMode s_simulatorMode;
static TransparentMode s_transparentMode;
static BridgeMode s_bridgeMode;
static BusMonitorMode s_busMonitorMode;
static EthWifiBridgeMode s_ethWifiBridgeMode;
static DualMasterMode s_dualMasterMode;

static IWorkMode *s_activeMode = nullptr;

static IWorkMode *resolveWorkMode(WorkMode mode) {
  switch (mode) {
  case WorkMode::GATEWAY:
    return &s_gatewayMode;
  case WorkMode::SIMULATOR:
    return &s_simulatorMode;
  case WorkMode::TRANSPARENT:
    return &s_transparentMode;
  case WorkMode::BRIDGE:
    return &s_bridgeMode;
  case WorkMode::BUS_MONITOR:
    return &s_busMonitorMode;
  case WorkMode::ETH_WIFI_BRIDGE:
    return &s_ethWifiBridgeMode;
  case WorkMode::DUAL_MASTER:
    return &s_dualMasterMode;
  default:
    return nullptr;
  }
}

void anyportHardwareInit() {
  Serial.begin(115200);
  delay(500);

  // 1. 首先加载持久化配置（确定工作模式及是否静默透传）
  configLoadAll();

  APP_PRINTLN("\n=== AnyPort Gateway v" FIRMWARE_VERSION " Starting ===");

  // 2. 初始化基础设施与网络栈 (模式 6 双主站由 DualMasterMode 独占初始化双路 UART0/UART1，避免单串口先绑定 GPIO 2/3 造成矩阵卸载冲突)
  if (g_workMode != WorkMode::DUAL_MASTER) {
    initRs485Port();
  }
  initWifi();
  initSpiAndEthernet();
  httpServerInit();

  // 3. 解析并启动当前激活的业务工作模式
  s_activeMode = resolveWorkMode(g_workMode);
  if (s_activeMode) {
    s_activeMode->begin();
  }

  APP_PRINTLN("=== Initialization Complete ===");
}

void anyportGatewayLoop() {
  if (s_activeMode) {
    s_activeMode->loop();
  }

  otaAutoCheckLoop();
  networkLoop();
  httpServerLoop();

  if (g_needRestart) {
    delay(1000);
    ESP.restart();
  }
}
