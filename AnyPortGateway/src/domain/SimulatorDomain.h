#pragma once

#include "AppTypes.h"

/**
 * @brief 从站模拟器领域模型与核心算法 (寄存器池、字节序、动态生成、报文监听)
 */

enum class DataType : uint8_t {
  INT16 = 0,
  UINT16 = 1,
  INT32 = 2,
  UINT32 = 3,
  FLOAT32 = 4,
  STRING = 5,
  BIT = 6
};

enum class Endianness : uint8_t {
  ABCD = 0, // Big-endian (Standard)
  DCBA = 1, // Little-endian
  BADC = 2, // Byte swap
  CDAB = 3  // Word swap
};

struct SimulatorVariable {
  uint32_t address;             // PLC 地址 (Base 1, 如 40001 或 400001)
  char name[32];                // 变量名称
  DataType type;                // 数据类型
  Endianness endian;            // 字节序
  float targetValue;            // 设定值
  float currentValue;           // 状态值
  char currentStr[32];          // 文本型
  bool isDynamic;               // 是否启用动态模拟
  float dynamicMin;             // 模拟最小值
  float dynamicMax;             // 模拟最大值
  uint16_t dynInterval;         // 动态频率 (秒)
  uint8_t dynMode;              // 动态模式: 0:随机, 1:加1, 2:减1
  unsigned long _lastDynUpdate; // 内部计时
};

struct SimulatorGlobalConfig {
  bool rtuEnabled;
  uint32_t baud;
  uint8_t stopBits;     // 1 或 2
  uint8_t parity;       // 0:None, 1:Even, 2:Odd
  uint8_t dataBits;     // 7 或 8
  uint8_t unitId;       // RTU 从站地址码
  uint8_t tcpUnitId;    // TCP 从站地址码
  bool tcpEnabled;
  uint16_t tcpPort;
  uint8_t netInterface; // 0: Auto, 1: W5500, 2: WiFi
};

static const size_t MAX_SIM_VARIABLES = 32;

// 状态访问器
SimulatorGlobalConfig &simGetConfig();
SimulatorVariable *simGetVariables();
size_t &simGetVarCount();

// 寄存器池与动态变量操作
void simWriteValueToPool(const SimulatorVariable &var);
void simSyncValuesFromPool();
void simUpdateDynamicValues();

// Modbus 从站 PDU/帧处理 (不含 RTU CRC 尾部，返回响应长度，0 表示不响应)
int simHandleModbusFrame(const uint8_t *frame, int len, uint8_t *outResponse,
                         uint8_t expectedUnitId);
