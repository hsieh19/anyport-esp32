#pragma once

#include "IWorkMode.h"

/**
 * @brief 工作模式 4：RS485 总线被动监听与通讯质量诊断模式 (纯只听不发，零总线干扰)
 */
class BusMonitorMode : public IWorkMode {
public:
  void begin() override;
  void loop() override;
};
