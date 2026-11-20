#pragma once

#include "../domain/AppTypes.h"
#include "../domain/ProtocolUtils.h"
#include <HardwareSerial.h>

/**
 * @brief 基础设施层：RS485 硬件串口驱动与双通道抽象
 */

extern HardwareSerial RS485Serial;
extern uint32_t g_rs485CurrentBaud;
extern uint32_t g_rs485CurrentConfig;
extern volatile bool g_rs485Busy;

// 获取当前单串口模式选定通道的全局配置
const Rs485PortConfig &getCurrentRs485Config();

// 单串口模式按指定波特率与配置掩码绑定当前选定通道 (485-1 或 485-2)
void rs485Begin(uint32_t baud, uint32_t config);

// 单串口模式统一按当前生效通道及全局参数启动
void rs485BeginCurrent();

// 系统启动时初始化默认 RS485 通道
void initRs485Port();
