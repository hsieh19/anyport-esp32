#pragma once

#include "IWorkMode.h"

/**
 * @brief 工作模式 1：Modbus 从站模拟器模式 (支持 RTU 与双网口 TCP 从站仿真及报文监听)
 */
class SimulatorMode : public IWorkMode {
public:
  void begin() override;
  void loop() override;
};
