/*
 * AnyPort ESP32-C3 网关主程序入口
 *
 * 说明：
 * - 本工程使用 Arduino 风格目录结构：
 *   /esp32
 *     /AnyPortGateway
 *       AnyPortGateway.ino   -> 主程序入口
 *       Config.h             -> 硬件引脚与外设主配置文件
 *
 * - 本文件只负责基础的 setup()/loop() 框架，并调用 Config.h 中定义的初始化函数。
 *   所有引脚定义、WiFi/W5500/RS485 初始化逻辑均在 Config.h 中给出。
 */

#include "Config.h"

void setup() {
    anyportHardwareInit();
}

void loop() {
    anyportGatewayLoop();
}

