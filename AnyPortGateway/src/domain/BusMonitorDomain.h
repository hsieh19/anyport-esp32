#pragma once

#include "AppTypes.h"

/**
 * @brief 总线监听与通讯质量诊断领域模型 (纯被动嗅探、主从配对状态机、RTT 与丢包率统计)
 */

enum class BusFrameMeta : uint8_t {
  UNKNOWN = 0,
  REQ = 1,       // 主站请求帧
  RESP = 2,      // 从站正常响应帧
  EXCEPTION = 3, // 从站异常响应帧 (功能码 0x80+)
  CRC_ERR = 4    // 物理层 CRC 校验失败或残帧 (< 4 字节)
};

struct BusMonitorConfig {
  uint8_t filterSlaveId;  // 0 表示不过滤 (全总线监听)
  uint8_t filterFuncCode; // 0 表示不过滤
  uint16_t timeoutMs;     // 主从应答超时判定阈值 (ms, 默认 1000, 范围 100~5000)
};

struct BusQualityStats {
  uint32_t totalFrames;    // 捕获总物理帧数
  uint32_t reqCount;       // 主站请求计数 (需应答的非广播请求)
  uint32_t respCount;      // 从站成功配对应答计数 (含正常与异常响应)
  uint32_t timeoutCount;   // 从站超时无应答计数
  uint32_t crcErrorCount;  // 物理层 CRC 错误或残帧计数
  uint32_t exceptionCount; // Modbus 异常响应 (0x80+) 计数
  uint32_t lastRttMs;      // 最近一次从站应答耗时 (ms)
  uint32_t minRttMs;       // 最小应答耗时 (ms)
  uint32_t maxRttMs;       // 最大峰值应答耗时 (ms)
  uint64_t totalRttMs;     // 累计应答总耗时 (用于计算平均耗时)
};

struct BusMonitorLog {
  unsigned long timestamp;
  uint8_t meta;     // BusFrameMeta 枚举值
  uint8_t slaveId;  // 从站地址
  uint8_t funcCode; // 功能码
  uint16_t rttMs;   // 配对响应耗时 (ms，仅 RESP/EXCEPTION 有效)
  uint8_t data[260];
  size_t length;
};

static const size_t MAX_MONITOR_LOGS = 100;

// 配置与统计只读/读写访问器
BusMonitorConfig &busMonitorGetConfig();
const BusQualityStats &busMonitorGetStats();

// 接收并分析一帧 RS485 物理报文 (驱动主从配对状态机与环形日志池)
void busMonitorProcessFrame(const uint8_t *data, size_t len);

// 主循环静默超时巡检 (当挂起请求超过 timeoutMs 未收到响应时结算为超时)
void busMonitorCheckTimeout();

// 清空报文环形缓冲区并重置全部质量统计指标
void busMonitorClear();

// 环形日志缓冲区读取
size_t busMonitorGetLogCount();
const BusMonitorLog &busMonitorGetLogAt(size_t logicalIndex);
