#include "SimulatorDomain.h"
#include <string.h>

// -----------------------
// 1. 模块私有静态状态池 (BSS 段确定性分配，避免堆内存碎片)
// -----------------------
static SimulatorVariable s_simVariables[MAX_SIM_VARIABLES];
static size_t s_simVarCount = 0;
static SimulatorGlobalConfig s_simConfig = {
    true, 9600, 1, 0, 8, 1, 1, true, 502, 0};

static uint16_t s_holdingRegisters[20000];
static uint8_t s_coils[2500];          // 2500 * 8 = 20000 bits
static uint8_t s_discreteInputs[2500]; // 2500 * 8 = 20000 bits

// -----------------------
// 2. 状态访问器实现
// -----------------------
SimulatorGlobalConfig &simGetConfig() { return s_simConfig; }
SimulatorVariable *simGetVariables() { return s_simVariables; }
size_t &simGetVarCount() { return s_simVarCount; }

// -----------------------
// 4. 地址映射与字节序转换内部辅助
// -----------------------
static uint16_t getMappedRegisterIndex(uint32_t addr) {
  if (addr >= 400001) return static_cast<uint16_t>(addr - 400001);
  if (addr >= 300001) return static_cast<uint16_t>(addr - 300001);
  if (addr >= 100001) return static_cast<uint16_t>(addr - 100001);

  if (addr >= 40001 && addr <= 49999) return static_cast<uint16_t>(addr - 40001);
  if (addr >= 30001 && addr <= 39999) return static_cast<uint16_t>(addr - 30001);
  if (addr >= 10001 && addr <= 19999) return static_cast<uint16_t>(addr - 10001);

  if (addr >= 1 && addr <= 9999) return static_cast<uint16_t>(addr - 1);
  return static_cast<uint16_t>(addr);
}

static uint16_t getDataTypeRegCount(DataType t) {
  switch (t) {
  case DataType::INT32:
  case DataType::UINT32:
  case DataType::FLOAT32:
    return 2;
  case DataType::STRING:
    return 16;
  default:
    return 1;
  }
}

static bool isInternalIndexDefined(uint16_t idx, bool isCoil) {
  for (size_t i = 0; i < s_simVarCount; i++) {
    uint16_t vIdx = getMappedRegisterIndex(s_simVariables[i].address);
    bool vIsCoil = (s_simVariables[i].type == DataType::BIT);
    if (vIsCoil != isCoil) continue;

    uint16_t count = getDataTypeRegCount(s_simVariables[i].type);
    if (idx >= vIdx && idx < vIdx + count) return true;
  }
  return false;
}

static uint8_t checkAddressRange(uint16_t start, uint16_t count, bool isCoil) {
  uint16_t limit = 20000;
  if (count == 0 || start + count > limit) return 0x02;
  for (uint16_t i = 0; i < count; i++) {
    if (!isInternalIndexDefined(start + i, isCoil)) return 0x02;
  }
  return 0;
}

static int makeExceptionResponse(uint8_t unitId, uint8_t funcCode,
                                 uint8_t exceptionCode, uint8_t *out) {
  out[0] = unitId;
  out[1] = funcCode | 0x80;
  out[2] = exceptionCode;
  return 3;
}

static bool getBit(const uint8_t *buf, uint16_t idx) {
  if (idx >= 20000) return false;
  return (buf[idx / 8] >> (idx % 8)) & 0x01;
}

static void setBit(uint8_t *buf, uint16_t idx, bool val) {
  if (idx >= 20000) return;
  if (val) {
    buf[idx / 8] |= (1 << (idx % 8));
  } else {
    buf[idx / 8] &= ~(1 << (idx % 8));
  }
}

static void swapBytes(uint8_t *p, Endianness endian, size_t size) {
  if (size == 2) {
    if (endian == Endianness::DCBA || endian == Endianness::BADC) {
      uint8_t tmp = p[0];
      p[0] = p[1];
      p[1] = tmp;
    }
  } else if (size == 4) {
    uint8_t b0 = p[0], b1 = p[1], b2 = p[2], b3 = p[3];
    switch (endian) {
    case Endianness::ABCD:
      break;
    case Endianness::DCBA:
      p[0] = b3;
      p[1] = b2;
      p[2] = b1;
      p[3] = b0;
      break;
    case Endianness::BADC:
      p[0] = b1;
      p[1] = b0;
      p[2] = b3;
      p[3] = b2;
      break;
    case Endianness::CDAB:
      p[0] = b2;
      p[1] = b3;
      p[2] = b0;
      p[3] = b1;
      break;
    }
  }
}

// -----------------------
// 5. 寄存器池同步与动态更新
// -----------------------
void simWriteValueToPool(const SimulatorVariable &var) {
  if (var.type == DataType::BIT) {
    uint16_t bitIdx = 0;
    uint8_t *pool = s_coils;
    if (var.address >= 100001 && var.address <= 120000) {
      bitIdx = var.address - 100001;
      pool = s_discreteInputs;
    } else if (var.address >= 10001 && var.address <= 30000) {
      bitIdx = var.address - 10001;
      pool = s_discreteInputs;
    } else {
      bitIdx = (var.address >= 1 && var.address <= 20000) ? (var.address - 1) : 0;
      pool = s_coils;
    }
    setBit(pool, bitIdx, var.currentValue > 0.5f);
    return;
  }

  uint16_t regIdx = getMappedRegisterIndex(var.address);
  if (regIdx >= 20000) return;

  if (var.type == DataType::INT16 || var.type == DataType::UINT16) {
    uint16_t val = static_cast<uint16_t>(var.currentValue);
    swapBytes(reinterpret_cast<uint8_t *>(&val), var.endian, 2);
    s_holdingRegisters[regIdx] = val;
  } else if (var.type == DataType::INT32 || var.type == DataType::UINT32 ||
             var.type == DataType::FLOAT32) {
    if (regIdx + 1 >= 20000) return;
    uint32_t val32;
    if (var.type == DataType::FLOAT32) {
      memcpy(&val32, &var.currentValue, 4);
    } else {
      val32 = static_cast<uint32_t>(var.currentValue);
    }
    swapBytes(reinterpret_cast<uint8_t *>(&val32), var.endian, 4);
    s_holdingRegisters[regIdx] = static_cast<uint16_t>(val32 >> 16);
    s_holdingRegisters[regIdx + 1] = static_cast<uint16_t>(val32 & 0xFFFF);
  } else if (var.type == DataType::STRING) {
    size_t len = strlen(var.currentStr);
    size_t regCount = (len + 1) / 2;
    for (size_t i = 0; i < regCount && (regIdx + i) < 20000; i++) {
      uint16_t val =
          (var.currentStr[i * 2] << 8) | (var.currentStr[i * 2 + 1] & 0xFF);
      s_holdingRegisters[regIdx + i] = val;
    }
  }
}

static void syncSingleValueFromPool(SimulatorVariable &var) {
  if (var.type == DataType::BIT) {
    if (var.address >= 100001 && var.address <= 120000)
      var.currentValue =
          getBit(s_discreteInputs, var.address - 100001) ? 1.0f : 0.0f;
    else if (var.address >= 10001 && var.address <= 30000)
      var.currentValue =
          getBit(s_discreteInputs, var.address - 10001) ? 1.0f : 0.0f;
    else
      var.currentValue =
          getBit(s_coils, (var.address >= 1 && var.address <= 20000)
                              ? (var.address - 1)
                              : 0)
              ? 1.0f
              : 0.0f;
    return;
  }

  uint16_t regIdx = getMappedRegisterIndex(var.address);
  if (regIdx >= 20000) return;

  if (var.type == DataType::INT16 || var.type == DataType::UINT16) {
    uint16_t val = s_holdingRegisters[regIdx];
    swapBytes(reinterpret_cast<uint8_t *>(&val), var.endian, 2);
    if (var.type == DataType::INT16)
      var.currentValue = static_cast<float>(static_cast<int16_t>(val));
    else
      var.currentValue = static_cast<float>(val);
  } else if (var.type == DataType::INT32 || var.type == DataType::UINT32 ||
             var.type == DataType::FLOAT32) {
    if (regIdx + 1 >= 20000) return;
    uint32_t val32 = (static_cast<uint32_t>(s_holdingRegisters[regIdx]) << 16) |
                     s_holdingRegisters[regIdx + 1];
    swapBytes(reinterpret_cast<uint8_t *>(&val32), var.endian, 4);
    if (var.type == DataType::FLOAT32)
      memcpy(&var.currentValue, &val32, 4);
    else if (var.type == DataType::INT32)
      var.currentValue = static_cast<float>(static_cast<int32_t>(val32));
    else
      var.currentValue = static_cast<float>(val32);
  }
}

void simSyncValuesFromPool() {
  for (size_t i = 0; i < s_simVarCount; i++) {
    syncSingleValueFromPool(s_simVariables[i]);
  }
}

void simUpdateDynamicValues() {
  unsigned long now = millis();
  for (size_t i = 0; i < s_simVarCount; i++) {
    SimulatorVariable &var = s_simVariables[i];
    syncSingleValueFromPool(var);

    if (!var.isDynamic) continue;

    unsigned long intervalMs =
        (var.dynInterval > 0 ? var.dynInterval : 1) * 1000UL;
    if (now - var._lastDynUpdate < intervalMs) continue;
    var._lastDynUpdate = now;

    float range = var.dynamicMax - var.dynamicMin;
    if (var.dynMode == 0) {
      float randomVal = static_cast<float>(random(1001)) / 1000.0f;
      var.currentValue = var.dynamicMin + (range * randomVal);
    } else if (var.dynMode == 1) {
      if (var.currentValue < var.dynamicMin ||
          var.currentValue >= var.dynamicMax) {
        var.currentValue = var.dynamicMin;
      } else {
        var.currentValue += 1.0f;
      }
    } else if (var.dynMode == 2) {
      if (var.currentValue <= var.dynamicMin ||
          var.currentValue > var.dynamicMax) {
        var.currentValue = var.dynamicMax;
      } else {
        var.currentValue -= 1.0f;
      }
    }

    simWriteValueToPool(var);
  }
}

// -----------------------
// 6. Modbus 功能码处理
// -----------------------
int simHandleModbusFrame(const uint8_t *frame, int len, uint8_t *outResponse,
                         uint8_t expectedUnitId) {
  if (len < 4) return 0;

  uint8_t unitId = frame[0];
  if (unitId != expectedUnitId) return 0;

  uint8_t funcCode = frame[1];
  uint16_t startAddr = (frame[2] << 8) | frame[3];
  uint16_t count = (frame[4] << 8) | frame[5];

  outResponse[0] = unitId;
  outResponse[1] = funcCode;

  if (funcCode == 0x01 || funcCode == 0x02) {
    uint8_t exc = checkAddressRange(startAddr, count, true);
    if (exc != 0) return makeExceptionResponse(unitId, funcCode, exc, outResponse);

    uint8_t *pool = (funcCode == 0x01) ? s_coils : s_discreteInputs;
    uint8_t byteCount = (count + 7) / 8;
    outResponse[2] = byteCount;
    memset(&outResponse[3], 0, byteCount);
    for (int i = 0; i < count; i++) {
      if (getBit(pool, startAddr + i)) {
        outResponse[3 + i / 8] |= (1 << (i % 8));
      }
    }
    return 3 + byteCount;
  } else if (funcCode == 0x03 || funcCode == 0x04) {
    uint8_t exc = checkAddressRange(startAddr, count, false);
    if (exc != 0) return makeExceptionResponse(unitId, funcCode, exc, outResponse);

    outResponse[2] = count * 2;
    for (int i = 0; i < count; i++) {
      uint16_t val = s_holdingRegisters[startAddr + i];
      outResponse[3 + i * 2] = val >> 8;
      outResponse[4 + i * 2] = val & 0xFF;
    }
    return 3 + count * 2;
  } else if (funcCode == 0x05) {
    if (checkAddressRange(startAddr, 1, true) != 0)
      return makeExceptionResponse(unitId, funcCode, 0x02, outResponse);

    bool val = (frame[4] == 0xFF);
    setBit(s_coils, startAddr, val);
    memcpy(&outResponse[2], &frame[2], 4);
    return 6;
  } else if (funcCode == 0x0F) {
    uint8_t exc = checkAddressRange(startAddr, count, true);
    if (exc != 0) return makeExceptionResponse(unitId, funcCode, exc, outResponse);

    for (int i = 0; i < count; i++) {
      bool val = (frame[7 + i / 8] >> (i % 8)) & 0x01;
      setBit(s_coils, startAddr + i, val);
    }
    memcpy(&outResponse[2], &frame[2], 4);
    return 6;
  } else if (funcCode == 0x06) {
    if (checkAddressRange(startAddr, 1, false) != 0)
      return makeExceptionResponse(unitId, funcCode, 0x02, outResponse);

    s_holdingRegisters[startAddr] = (frame[4] << 8) | frame[5];
    memcpy(&outResponse[2], &frame[2], 4);
    return 6;
  } else if (funcCode == 0x10) {
    uint8_t exc = checkAddressRange(startAddr, count, false);
    if (exc != 0) return makeExceptionResponse(unitId, funcCode, exc, outResponse);

    for (int i = 0; i < count; i++) {
      s_holdingRegisters[startAddr + i] =
          (frame[7 + i * 2] << 8) | frame[8 + i * 2];
    }
    memcpy(&outResponse[2], &frame[2], 4);
    return 6;
  }

  return 0;
}
