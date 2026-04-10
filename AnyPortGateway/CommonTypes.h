#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * @brief AnyPort 项目通用类型定义
 */

// 工作模式定义
enum class WorkMode : uint8_t {
  GATEWAY = 0,     // MQTT 网关模式
  SIMULATOR = 1,   // 从站模拟器模式
  TRANSPARENT = 2, // USB 转 RS485 透传模式
  BRIDGE = 3,      // 协议互转模式 (Modbus TCP <-> RTU)
  FIRMWARE_UPDATE = 4, // 系统固件更新模式
  ETH_WIFI_BRIDGE = 5  // 以太网转 WiFi 桥接模式
};

// 网络接口选择
enum class NetInterface : uint8_t {
  AUTO = 0,   // 同时启用或自动切换
  W5500 = 1,  // 仅以太网
  WIFI = 2    // 仅 WiFi
};
