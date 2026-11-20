#pragma once

#include "IWorkMode.h"
#include <stdint.h>

/**
 * @brief 工作模式 6：双主站防碰撞中继网关模式 (专属适配 AnyControl 平台与 Dual485-Controller-V1 硬件)
 *
 * 物理引脚映射 (以本项目 Dual485-Controller-V1 硬件为准):
 * - 485-1 (原物理主站侧, UART0 / MasterSerial): RX = GPIO 2, TX = GPIO 3
 * - 485-2 (现场从站总线侧, UART1 / RS485Serial): RX = GPIO 5, TX = GPIO 4
 * - WiFi TCP Server (第 2 主站 / AnyControl 上位机): 默认监听 9502 端口
 */

class DualMasterMode : public IWorkMode {
public:
  void begin() override;
  void loop() override;

  // 动态热重载双路 RS485 串口参数 (仅在当前处于模式 6 时生效)
  static void reconfigSerial();
};
