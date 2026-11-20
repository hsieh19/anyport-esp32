#pragma once

#include "IWorkMode.h"

/**
 * @brief 工作模式 5：以太网 <-> WiFi 跨网段透明桥接模式
 */
class EthWifiBridgeMode : public IWorkMode {
public:
  void begin() override;
  void loop() override;
};
