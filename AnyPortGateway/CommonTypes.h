#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * @brief AnyPort 项目通用类型定义
 */

// 工作模式定义
enum class WorkMode : uint8_t {
  GATEWAY = 0,   // MQTT 网关模式
  SIMULATOR = 1, // 从站模拟器模式
  TRANSPARENT = 2 // USB 转 RS485 透传模式
};
