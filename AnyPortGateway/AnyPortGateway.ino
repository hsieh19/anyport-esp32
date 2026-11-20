/*
 * AnyPort ESP32-C3 网关主程序入口
 *
 * 架构分层 (Clean Architecture):
 * - src/domain/ : 领域模型、协议算法与模拟器核心 (AppTypes, ProtocolUtils, SimulatorDomain)
 * - src/infra/  : 基础设施与驱动层 (ConfigStore, Rs485Driver, NetworkManager, OtaService)
 * - src/modes/  : 业务工作模式层 (IWorkMode 接口及 6 大工作模式实现)
 * - src/api/    : 接口适配层 (HttpApiServer, SystemController, SimulatorController)
 * - src/app/    : 应用内核编排层 (AppKernel)
 */

#include "src/app/AppKernel.h"

void setup() {
  anyportHardwareInit();
}

void loop() {
  anyportGatewayLoop();
}
