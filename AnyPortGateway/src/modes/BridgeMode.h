#pragma once

#include "IWorkMode.h"

/**
 * @brief 工作模式 3：Modbus TCP <-> RTU 双向标准协议互转模式
 */
class BridgeMode : public IWorkMode {
public:
  void begin() override;
  void loop() override;
};
