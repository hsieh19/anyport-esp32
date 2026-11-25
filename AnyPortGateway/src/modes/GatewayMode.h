#pragma once

#include "IWorkMode.h"

/**
 * @brief 工作模式 0：MQTT 物联网云网关模式 (支持 Modbus TCP / RTU 远程指令转发与 Ping 探测)
 */
class GatewayMode : public IWorkMode {
public:
  void begin() override;
  void loop() override;
  static bool isMqttConnected();
};
