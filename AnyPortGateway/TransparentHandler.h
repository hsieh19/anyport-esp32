#pragma once

#include "Globals.h"

/**
 * @brief ESP32-C3 简约版高性能手动透传模式实现
 * 此模式下，AnyPort 充当 USB 与 RS485 之间的透明网桥。
 * 注意：由于 C3 内部 USB 硬件限制，RS485 的波特率、校验位等参数需在 Web
 * 页面手动设置。
 */

static void initTransparentMode() {
  // 强制关闭系统调试输出
  Serial.setDebugOutput(false);

  // 构建串口配置
  uint32_t config = SERIAL_8N1;
  if (g_transDataBits == 8) {
    if (g_transParity == 1)
      config = (g_transStopBits == 1) ? SERIAL_8E1 : SERIAL_8E2;
    else if (g_transParity == 2)
      config = (g_transStopBits == 1) ? SERIAL_8O1 : SERIAL_8O2;
    else
      config = (g_transStopBits == 1) ? SERIAL_8N1 : SERIAL_8N2;
  } else if (g_transDataBits == 7) {
    if (g_transParity == 1)
      config = (g_transStopBits == 1) ? SERIAL_7E1 : SERIAL_7E2;
    else if (g_transParity == 2)
      config = (g_transStopBits == 1) ? SERIAL_7O1 : SERIAL_7O2;
    else
      config = (g_transStopBits == 1) ? SERIAL_7N1 : SERIAL_7N2;
  }

  RS485Serial.flush();
  RS485Serial.end();
  delay(10);
  RS485Serial.begin(g_transBaud, config, PIN_RS485_RX, PIN_RS485_TX);
}

static void loopTransparentMode() {
  // 使用 while 循环配合 read/write 实现最高吞吐量转发

  // 1. USB -> RS485 (电脑发给设备)
  while (Serial.available()) {
    RS485Serial.write(Serial.read());
  }

  // 2. RS485 -> USB (设备回给电脑)
  while (RS485Serial.available()) {
    Serial.write(RS485Serial.read());
  }
}
