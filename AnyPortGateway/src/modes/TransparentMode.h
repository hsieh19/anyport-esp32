#pragma once

#include "IWorkMode.h"

/**
 * @brief 工作模式 2：USB 转 RS485 高速透明传输模式
 */
class TransparentMode : public IWorkMode {
public:
  void begin() override;
  void loop() override;
};
