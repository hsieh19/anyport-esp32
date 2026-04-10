#pragma once

#include <stdint.h>
#include <string.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Ethernet.h>

#include "CommonTypes.h"
#include "Globals.h"

// 引用不再在本地定义，而是通过 Globals.h 统一管理
extern WorkMode g_workMode;
extern volatile bool g_needRestart;

/**
 * @brief 从站模拟器核心逻辑
 */

/**
 * @brief 从站模拟器核心逻辑
 * 
 * 包含：
 * 1. 寄存器结构体与寄存器池 (Holding Registers)
 * 2. 字节序转换处理 (Endianness)
 * 3. 动态模拟逻辑
 */

// 数据类型枚举
enum class DataType : uint8_t {
    INT16 = 0,
    UINT16 = 1,
    INT32 = 2,
    UINT32 = 3,
    FLOAT32 = 4,
    STRING = 5,
    BIT = 6      // 与 Web UI 的 value='6' 保持一致
};

// 字节序模式枚举
enum class Endianness : uint8_t {
    ABCD = 0, // Big-endian (Standard)
    DCBA = 1, // Little-endian
    BADC = 2, // Byte swap
    CDAB = 3  // Word swap
};

// 寄存器变量定义结构体
struct SimulatorVariable {
    uint32_t address;      // PLC 地址 (Base 1, 如 40001 或 400001)
    char name[32];         // 变量名称
    DataType type;         // 数据类型
    Endianness endian;     // 字节序
    float targetValue;     // 设定值 (用户手动修改的值)
    float currentValue;    // 状态值 (当前实际读取的值)
    char currentStr[32];   // 文本型
    bool isDynamic;        // 是否启用动态模拟
    float dynamicMin;      // 模拟最小值
    float dynamicMax;      // 模拟最大值
    uint16_t dynInterval;  // 动态频率 (秒)
    uint8_t dynMode;       // 动态模式: 0:随机, 1:加1, 2:减1
    unsigned long _lastDynUpdate; // 内部计时
};

// 模拟器全局运行参数
struct SimulatorGlobalConfig {
    bool rtuEnabled;
    uint32_t baud;
    uint8_t stopBits;    // 1 或 2
    uint8_t parity;      // 0:None, 1:Even, 2:Odd
    uint8_t dataBits;    // 7 或 8
    uint8_t unitId;      // 从站地址码
    bool tcpEnabled;
    uint16_t tcpPort;
    uint8_t netInterface; // 0: Auto, 1: W5500, 2: WiFi
    bool monitorEnabled; // 是否开启报文监听
    uint8_t monitorFilter; // 0:全线报文, 1:仅本站报文
};

// 共享寄存器池
static const size_t MAX_SIM_VARIABLES = 32;
static SimulatorVariable g_simVariables[MAX_SIM_VARIABLES];
static size_t g_simVarCount = 0;
static SimulatorGlobalConfig g_simConfig = {true, 9600, 1, 0, 8, 1, true, 502, 0, false, 0};

// 报文监听循环缓冲区
struct MonitorLog {
    unsigned long timestamp;
    bool isOutgoing; // TX: true, RX: false
    uint8_t type;    // 0: RTU, 1: TCP
    uint8_t data[260];
    size_t length;
};
static const size_t MAX_MONITOR_LOGS = 20;
static MonitorLog g_monitorLogs[MAX_MONITOR_LOGS];
static size_t g_monitorHead = 0;
static size_t g_monitorCount = 0;

static void addMonitorLog(bool isOutgoing, uint8_t type, const uint8_t* data, size_t len) {
    if (!g_simConfig.monitorEnabled) return;
    
    // 过滤逻辑：本站报文过滤（针对本站地址或响应）
    if (g_simConfig.monitorFilter == 1 && len > 0) {
        if (data[0] != g_simConfig.unitId) return;
    }

    size_t index = (g_monitorHead + g_monitorCount) % MAX_MONITOR_LOGS;
    g_monitorLogs[index].timestamp = millis();
    g_monitorLogs[index].isOutgoing = isOutgoing;
    g_monitorLogs[index].type = type;
    g_monitorLogs[index].length = (len > 260) ? 260 : len;
    memcpy(g_monitorLogs[index].data, data, g_monitorLogs[index].length);

    if (g_monitorCount < MAX_MONITOR_LOGS) {
        g_monitorCount++;
    } else {
        g_monitorHead = (g_monitorHead + 1) % MAX_MONITOR_LOGS;
    }
}

// 清除监听日志
static void clearMonitorLogs() {
    g_monitorHead = 0;
    g_monitorCount = 0;
}

// Holding / Input Register 缓冲区 (3xxxx / 4xxxx)
static uint16_t g_holdingRegisters[20000]; 

// Coils / Discrete Inputs 缓冲区 (0xxxx / 1xxxx)
static uint8_t g_coils[2500];           // 2500 * 8 = 20000 bits
static uint8_t g_discreteInputs[2500];  // 2500 * 8 = 20000 bits

/**
 * @brief 位操作与地址转换辅助函数
 */
// 仅用于将用户配置的 PLC 地址 (Base 1) 转换为内部 0-based 偏移量
static uint16_t getMappedRegisterIndex(uint32_t addr) {
    // 6 位地址段 (100001-465535)
    if (addr >= 400001) return (uint16_t)(addr - 400001);
    if (addr >= 300001) return (uint16_t)(addr - 300001);
    if (addr >= 100001) return (uint16_t)(addr - 100001);

    // 5 位地址段 (10001-49999)
    if (addr >= 40001 && addr <= 49999)  return (uint16_t)(addr - 40001);
    if (addr >= 30001 && addr <= 39999)  return (uint16_t)(addr - 30001);
    if (addr >= 10001 && addr <= 19999)  return (uint16_t)(addr - 10001);

    // 标准 0xxxx 线圈地址处理 (00001 -> 0)
    if (addr >= 1 && addr <= 9999)      return (uint16_t)(addr - 1); 

    return (uint16_t)addr; 
}

/**
 * @brief 根据数据类型获取占用的寄存器数量 (16-bit counts)
 */
static uint16_t getDataTypeRegCount(DataType t) {
    switch (t) {
        case DataType::INT32:
        case DataType::UINT32:
        case DataType::FLOAT32:
            return 2;
        case DataType::STRING:
            return 16; // 32 bytes
        default:
            return 1;
    }
}

/**
 * @brief 检查某个内部池索引是否对应已定义的变量
 */
static bool isInternalIndexDefined(uint16_t idx, bool isCoil) {
    for (size_t i = 0; i < g_simVarCount; i++) {
        uint16_t vIdx = getMappedRegisterIndex(g_simVariables[i].address);
        bool vIsCoil = (g_simVariables[i].type == DataType::BIT);
        if (vIsCoil != isCoil) continue;
        
        uint16_t count = getDataTypeRegCount(g_simVariables[i].type);
        if (idx >= vIdx && idx < vIdx + count) return true;
    }
    return false;
}

/**
 * @brief 检查请求的寄存器范围是否全部属于已定义变量
 * @return 0: 全部定义, 2: 包含未定义地址 (Illegal Data Address)
 */
static uint8_t checkAddressRange(uint16_t start, uint16_t count, bool isCoil) {
    uint16_t limit = isCoil ? 20000 : 20000;
    if (count == 0 || start + count > limit) return 02; // 异常码 02: 非法地址
    for (uint16_t i = 0; i < count; i++) {
        if (!isInternalIndexDefined(start + i, isCoil)) return 02;
    }
    return 0; // OK
}

/**
 * @brief 生成 Modbus 异常响应
 */
static int makeExceptionResponse(uint8_t unitId, uint8_t funcCode, uint8_t exceptionCode, uint8_t* out) {
    out[0] = unitId;
    out[1] = funcCode | 0x80;
    out[2] = exceptionCode;
    return 3;
}

static bool getBit(const uint8_t* buf, uint16_t idx) {
    if (idx >= 20000) return false;
    return (buf[idx / 8] >> (idx % 8)) & 0x01;
}
static void setBit(uint8_t* buf, uint16_t idx, bool val) {
    if (idx >= 20000) return;
    if (val) {
        buf[idx / 8] |= (1 << (idx % 8));
    } else {
        buf[idx / 8] &= ~(1 << (idx % 8));
    }
}

/**
 * @brief 字节交换辅助函数
 */
static void swapBytes(uint8_t* p, Endianness endian, size_t size) {
    if (size == 2) {
        if (endian == Endianness::DCBA || endian == Endianness::BADC) {
            uint8_t tmp = p[0]; p[0] = p[1]; p[1] = tmp;
        }
    } else if (size == 4) {
        uint8_t b0 = p[0], b1 = p[1], b2 = p[2], b3 = p[3];
        switch (endian) {
            case Endianness::ABCD: // No swap (1234)
                break;
            case Endianness::DCBA: // Total swap (4321)
                p[0] = b3; p[1] = b2; p[2] = b1; p[3] = b0;
                break;
            case Endianness::BADC: // Byte swap (2143)
                p[0] = b1; p[1] = b0; p[2] = b3; p[3] = b2;
                break;
            case Endianness::CDAB: // Word swap (3412)
                p[0] = b2; p[1] = b3; p[2] = b0; p[3] = b1;
                break;
        }
    }
}

/**
 * @brief 将浮点数值写入寄存器池（带类型和字节序处理）
 */
static void writeValueToPool(const SimulatorVariable& var) {
    if (var.type == DataType::BIT) {
        uint16_t bitIdx = 0;
        uint8_t* pool = g_coils;
        if (var.address >= 100001 && var.address <= 120000) {
            bitIdx = var.address - 100001;
            pool = g_discreteInputs;
        } else if (var.address >= 10001 && var.address <= 30000) {
            bitIdx = var.address - 10001;
            pool = g_discreteInputs;
        } else {
            bitIdx = (var.address >= 1 && var.address <= 20000) ? (var.address - 1) : 0;
            pool = g_coils;
        }
        setBit(pool, bitIdx, var.currentValue > 0.5f);
        return;
    }

    uint16_t regIdx = getMappedRegisterIndex(var.address);
    
    if (regIdx >= 20000) return;

    if (var.type == DataType::INT16 || var.type == DataType::UINT16) {
        uint16_t val = static_cast<uint16_t>(var.currentValue);
        swapBytes((uint8_t*)&val, var.endian, 2);
        g_holdingRegisters[regIdx] = val;
    } else if (var.type == DataType::INT32 || var.type == DataType::UINT32 || var.type == DataType::FLOAT32) {
        if (regIdx + 1 >= 20000) return;
        uint32_t val32;
        if (var.type == DataType::FLOAT32) {
            memcpy(&val32, &var.currentValue, 4);
        } else {
            val32 = static_cast<uint32_t>(var.currentValue);
        }
        swapBytes((uint8_t*)&val32, var.endian, 4);
        g_holdingRegisters[regIdx] = (uint16_t)(val32 >> 16);     // High Word
        g_holdingRegisters[regIdx + 1] = (uint16_t)(val32 & 0xFFFF); // Low Word
    } else if (var.type == DataType::STRING) {
        size_t len = strlen(var.currentStr);
        size_t regCount = (len + 1) / 2;
        for (size_t i = 0; i < regCount && (regIdx + i) < 20000; i++) {
            uint16_t val = (var.currentStr[i * 2] << 8) | (var.currentStr[i * 2 + 1] & 0xFF);
            // 字符串通常不应用 ABCD 以外的复杂交换，保持标准序
            g_holdingRegisters[regIdx + i] = val;
        }
    }
}

/**
 * @brief 从存储池同步单个变量的值 (用于 Web 显示真实值)
 */
static void syncSingleValueFromPool(SimulatorVariable& var) {
    if (var.type == DataType::BIT) {
        if (var.address >= 100001 && var.address <= 120000)
            var.currentValue = getBit(g_discreteInputs, var.address - 100001) ? 1.0f : 0.0f;
        else if (var.address >= 10001 && var.address <= 30000)
            var.currentValue = getBit(g_discreteInputs, var.address - 10001) ? 1.0f : 0.0f;
        else
            var.currentValue = getBit(g_coils, (var.address >= 1 && var.address <= 20000) ? (var.address - 1) : 0) ? 1.0f : 0.0f;
        return;
    }

    uint16_t regIdx = getMappedRegisterIndex(var.address);

    if (regIdx >= 20000) return;

    if (var.type == DataType::INT16 || var.type == DataType::UINT16) {
        uint16_t val = g_holdingRegisters[regIdx];
        swapBytes((uint8_t*)&val, var.endian, 2);
        if (var.type == DataType::INT16) var.currentValue = (float)(int16_t)val;
        else var.currentValue = (float)val;
    } else if (var.type == DataType::INT32 || var.type == DataType::UINT32 || var.type == DataType::FLOAT32) {
        if (regIdx + 1 >= 20000) return;
        uint32_t val32 = ((uint32_t)g_holdingRegisters[regIdx] << 16) | g_holdingRegisters[regIdx + 1];
        swapBytes((uint8_t*)&val32, var.endian, 4);
        if (var.type == DataType::FLOAT32) memcpy(&var.currentValue, &val32, 4);
        else if (var.type == DataType::INT32) var.currentValue = (float)(int32_t)val32;
        else var.currentValue = (float)val32;
    }
}

/**
 * @brief 批量同步所有值
 */
static void syncValuesFromPool() {
    for (size_t i = 0; i < g_simVarCount; i++) {
        syncSingleValueFromPool(g_simVariables[i]);
    }
}


/**
 * @brief 更新所有处于动态模式的变量
 */
static void updateDynamicValues() {
    unsigned long now = millis();
    for (size_t i = 0; i < g_simVarCount; i++) {
        SimulatorVariable& var = g_simVariables[i];
        
        // 1. 无论是否开启动态，先从寄存器读回当前最真实的“状态值”
        // 这确保证了 Master 的写入能即时反映在 Web 的“状态值”上
        syncSingleValueFromPool(var);

        if (!var.isDynamic) continue;

        // 2. 检查独立间隔
        unsigned long intervalMs = (var.dynInterval > 0 ? var.dynInterval : 1) * 1000;
        if (now - var._lastDynUpdate < intervalMs) continue;
        var._lastDynUpdate = now;

        float range = var.dynamicMax - var.dynamicMin;
        if (var.dynMode == 0) { // 随机模式
            float randomVal = (float)random(1001) / 1000.0f;
            var.currentValue = var.dynamicMin + (range * randomVal);
        } else if (var.dynMode == 1) { // 加 1 模式
            if (var.currentValue < var.dynamicMin || var.currentValue >= var.dynamicMax) {
                var.currentValue = var.dynamicMin;
            } else {
                var.currentValue += 1.0f;
            }
        } else if (var.dynMode == 2) { // 减 1 模式
            if (var.currentValue <= var.dynamicMin || var.currentValue > var.dynamicMax) {
                var.currentValue = var.dynamicMax;
            } else {
                var.currentValue -= 1.0f;
            }
        }
        
        // 3. 将变化后的动态值写回寄存器池
        writeValueToPool(var);
    }
}

/**
 * @brief 计算 Modbus CRC16
 */
static uint16_t calculateCRC(uint8_t* buf, int len) {
    uint16_t crc = 0xFFFF;
    for (int pos = 0; pos < len; pos++) {
        crc ^= (uint16_t)buf[pos];
        for (int i = 8; i != 0; i--) {
            if ((crc & 0x0001) != 0) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/**
 * @brief 处理 Modbus 功能码并生成响应
 * @return 响应长度，0 表示不响应或地址错误
 */
static int handleModbusFrame(uint8_t* frame, int len, uint8_t* outResponse) {
    if (len < 4) return 0;
    
    uint8_t unitId = frame[0];
    
    // 关键修复：仅当 Unit ID 匹配模拟器配置的地址时才响应
    if (unitId != g_simConfig.unitId) return 0;

    uint8_t funcCode = frame[1];
    uint16_t ra = (frame[2] << 8) | frame[3]; // ra = raw offset from master
    uint16_t startAddr = ra;                  // Directly use offset
    uint16_t count = (frame[4] << 8) | frame[5];

    outResponse[0] = unitId;
    outResponse[1] = funcCode;

    if (funcCode == 0x01 || funcCode == 0x02) { // Read Coils / Discrete Inputs
        uint8_t exc = checkAddressRange(startAddr, count, true);
        if (exc != 0) return makeExceptionResponse(unitId, funcCode, exc, outResponse);

        uint8_t* pool = (funcCode == 0x01) ? g_coils : g_discreteInputs;
        uint8_t byteCount = (count + 7) / 8;
        outResponse[2] = byteCount;
        memset(&outResponse[3], 0, byteCount);
        for (int i = 0; i < count; i++) {
            if (getBit(pool, startAddr + i)) {
                outResponse[3 + i / 8] |= (1 << (i % 8));
            }
        }
        return 3 + byteCount;
    }
    else if (funcCode == 0x03 || funcCode == 0x04) { // Read Holding/Input Registers
        uint8_t exc = checkAddressRange(startAddr, count, false);
        if (exc != 0) return makeExceptionResponse(unitId, funcCode, exc, outResponse);

        outResponse[2] = count * 2;
        for (int i = 0; i < count; i++) {
            uint16_t val = g_holdingRegisters[startAddr + i];
            outResponse[3 + i * 2] = val >> 8;
            outResponse[4 + i * 2] = val & 0xFF;
        }
        return 3 + count * 2;
    } 
    else if (funcCode == 0x05) { // Write Single Coil
        if (checkAddressRange(startAddr, 1, true) != 0) 
            return makeExceptionResponse(unitId, funcCode, 02, outResponse);
            
        bool val = (frame[4] == 0xFF); // 0xFF00 is ON, 0x0000 is OFF
        setBit(g_coils, startAddr, val);
        memcpy(&outResponse[2], &frame[2], 4);
        return 6;
    }
    else if (funcCode == 0x0F) { // Write Multiple Coils
        uint8_t exc = checkAddressRange(startAddr, count, true);
        if (exc != 0) return makeExceptionResponse(unitId, funcCode, exc, outResponse);

        uint8_t byteCount = frame[6];
        for (int i = 0; i < count; i++) {
            bool val = (frame[7 + i / 8] >> (i % 8)) & 0x01;
            setBit(g_coils, startAddr + i, val);
        }
        memcpy(&outResponse[2], &frame[2], 4);
        return 6;
    }
    else if (funcCode == 0x06) { // Write Single Register
        if (checkAddressRange(startAddr, 1, false) != 0)
            return makeExceptionResponse(unitId, funcCode, 02, outResponse);

        g_holdingRegisters[startAddr] = (frame[4] << 8) | frame[5];
        memcpy(&outResponse[2], &frame[2], 4);
        return 6;
    }
    else if (funcCode == 0x10) { // Write Multiple Registers
        uint8_t exc = checkAddressRange(startAddr, count, false);
        if (exc != 0) return makeExceptionResponse(unitId, funcCode, exc, outResponse);

        for (int i = 0; i < count; i++) {
            g_holdingRegisters[startAddr + i] = (frame[7 + i * 2] << 8) | frame[8 + i * 2];
        }
        memcpy(&outResponse[2], &frame[2], 4);
        return 6;
    }

    return 0; 
}

/**
 * @brief 处理 RTU 串口流量
 */
static void handleRtuSerial() {
    if (RS485Serial.available() > 0) {
        uint8_t buf[256];
        int len = 0;
        unsigned long lastByte = millis();
        while (millis() - lastByte < 5 && len < 256) { // 3.5 字符时间超时判断
            if (RS485Serial.available()) {
                buf[len++] = RS485Serial.read();
                lastByte = millis();
            }
        }

        if (len < 4) return;
        
        // 记录入向原始报文（包括可能的其他地址报文）
        addMonitorLog(false, 0, buf, len);

        uint16_t receivedCrc = (buf[len - 1] << 8) | buf[len - 2];
        if (calculateCRC(buf, len - 2) != receivedCrc) return;

        uint8_t response[256];
        int respLen = handleModbusFrame(buf, len - 2, response);
        if (respLen > 0) {
            uint16_t crc = calculateCRC(response, respLen);
            response[respLen++] = crc & 0xFF;
            response[respLen++] = crc >> 8;

            // 记录出向报文
            addMonitorLog(true, 0, response, respLen);

            RS485Serial.write(response, respLen);
            RS485Serial.flush();
        }
    }
}

// 使用 Globals.h 中定义的 EspEthernetServer

static EspEthernetServer* g_simTcpEthServer = nullptr;
static EthernetClient g_simTcpEthClients[2]; 
static WiFiServer* g_simTcpWifiServer = nullptr;
static WiFiClient g_simTcpWifiClients[2];

static void processGenericTcpClient(Client& client, uint8_t type) {
    if (!client || !client.connected()) return;
    
    if (client.available() >= 7) { 
        uint8_t mbap[7];
        client.read(mbap, 7);
        
        uint16_t len = (mbap[4] << 8) | mbap[5];
        if (len > 0 && len < 260) {
            uint8_t pdu[256];
            pdu[0] = mbap[6]; // Unit ID
            
            int pduGot = 1;
            unsigned long startWait = millis();
            while (pduGot < len && millis() - startWait < 50) {
                if (client.available()) {
                    pdu[pduGot++] = client.read();
                }
            }

            if (pduGot == len) {
                uint8_t fullReq[263];
                memcpy(fullReq, mbap, 7);
                if (len > 1) memcpy(&fullReq[7], &pdu[1], len - 1);
                addMonitorLog(false, type, fullReq, 6 + len);

                uint8_t responsePdu[256];
                int respPduLen = handleModbusFrame(pdu, len, responsePdu);
                
                if (respPduLen > 0) {
                    uint8_t respFrame[263];
                    memcpy(respFrame, mbap, 4); 
                    respFrame[4] = (respPduLen >> 8);
                    respFrame[5] = (respPduLen & 0xFF);
                    memcpy(&respFrame[6], responsePdu, respPduLen);

                    client.write(respFrame, 6 + respPduLen);
                    client.flush();
                    addMonitorLog(true, type, respFrame, 6 + respPduLen);
                }
            }
        }
    }
}

/**
 * @brief 处理 Modbus TCP 流量
 */
static void handleTcpServer() {
    // 1. 处理以太网连接
    if (g_simTcpEthServer) {
        EthernetClient newEth = g_simTcpEthServer->accept();
        if (newEth) {
            bool ok = false;
            for (int i = 0; i < 2; i++) {
                if (!g_simTcpEthClients[i] || !g_simTcpEthClients[i].connected()) {
                    g_simTcpEthClients[i] = newEth; ok = true; break;
                }
            }
            if (!ok) newEth.stop();
        }
        for (int i = 0; i < 2; i++) processGenericTcpClient(g_simTcpEthClients[i], 1);
    }

    // 2. 处理 WiFi 连接
    if (g_simTcpWifiServer) {
        WiFiClient newWifi = g_simTcpWifiServer->accept();
        if (newWifi) {
            bool ok = false;
            for (int i = 0; i < 2; i++) {
                if (!g_simTcpWifiClients[i] || !g_simTcpWifiClients[i].connected()) {
                    g_simTcpWifiClients[i] = newWifi; ok = true; break;
                }
            }
            if (!ok) newWifi.stop();
        }
        for (int i = 0; i < 2; i++) processGenericTcpClient(g_simTcpWifiClients[i], 1);
    }
}

/**
 * @brief 从 NVS 加载寄存器与全局配置
 */
static void loadSimConfig() {
    g_prefs.begin("anyport", true);
    
    // 加载全局配置
    if (g_prefs.isKey("simGlobal")) {
        g_prefs.getBytes("simGlobal", &g_simConfig, sizeof(g_simConfig));
    }

    // 加载寄存器变量
    size_t size = g_prefs.getBytesLength("simVars");
    if (size > 0 && (size % sizeof(SimulatorVariable) == 0) && size <= sizeof(g_simVariables)) {
        g_prefs.getBytes("simVars", g_simVariables, size);
        g_simVarCount = size / sizeof(SimulatorVariable);
        
        // 初始化寄存器池中已有的值
        for (size_t i = 0; i < g_simVarCount; i++) {
            // 启动时确保持续设定值优先，状态值等于设定值
            g_simVariables[i].currentValue = g_simVariables[i].targetValue; 
            writeValueToPool(g_simVariables[i]);
        }
    } else {
        if (size > 0) {
            APP_LOG("[Simulator] Config size mismatch (%d vs %d), clearing old settings", size, sizeof(SimulatorVariable));
            g_prefs.remove("simVars");
        }
        g_simVarCount = 0;
    }
    g_prefs.end();
}

/**
 * @brief 将当前寄存器与全局配置保存到 NVS
 */
static void saveSimConfig() {
    if (!g_prefs.begin("anyport", false)) return;
    g_prefs.putBytes("simGlobal", &g_simConfig, sizeof(g_simConfig));
    if (g_simVarCount > 0) {
        g_prefs.putBytes("simVars", g_simVariables, g_simVarCount * sizeof(SimulatorVariable));
    } else {
        g_prefs.remove("simVars"); // 显式删除，防止 0 长度写入无效
    }
    g_prefs.end();
    delay(100); // 增加少量延迟确保 Flash 物理写入完成
}

// 模拟器主循环挂载点
void simulatorLoop() {
    static bool netStarted = false;
    if (!netStarted) {
        loadSimConfig(); // 首先加载配置

        // 如果使能了串口，需按配置初始化串口
        if (g_simConfig.rtuEnabled) {
            uint32_t config = SERIAL_8N1;
            uint8_t db = g_simConfig.dataBits;
            if (db != 7 && db != 8) db = 8; // 强制合法值
            
            if (db == 8) {
                if (g_simConfig.parity == 0) config = (g_simConfig.stopBits == 1) ? SERIAL_8N1 : SERIAL_8N2;
                else if (g_simConfig.parity == 1) config = (g_simConfig.stopBits == 1) ? SERIAL_8E1 : SERIAL_8E2;
                else config = (g_simConfig.stopBits == 1) ? SERIAL_8O1 : SERIAL_8O2;
            } else {
                if (g_simConfig.parity == 0) config = (g_simConfig.stopBits == 1) ? SERIAL_7N1 : SERIAL_7N2;
                else if (g_simConfig.parity == 1) config = (g_simConfig.stopBits == 1) ? SERIAL_7E1 : SERIAL_7E2;
                else config = (g_simConfig.stopBits == 1) ? SERIAL_7O1 : SERIAL_7O2;
            }
            RS485Serial.begin(g_simConfig.baud, config, PIN_RS485_RX, PIN_RS485_TX);
        }

        if (g_simConfig.tcpEnabled) {
            uint8_t net = g_simConfig.netInterface; 
            if (net == 0 || net == 1) { // W5500
                if (g_simTcpEthServer) delete g_simTcpEthServer;
                g_simTcpEthServer = new EspEthernetServer(g_simConfig.tcpPort);
                g_simTcpEthServer->begin();
                APP_LOG("[Simulator] TCP Server (Ethernet) started on port %d", g_simConfig.tcpPort);
            }
            if (net == 0 || net == 2) { // WiFi
                if (g_simTcpWifiServer) delete g_simTcpWifiServer;
                g_simTcpWifiServer = new WiFiServer(g_simConfig.tcpPort);
                g_simTcpWifiServer->begin();
                APP_LOG("[Simulator] TCP Server (WiFi) started on port %d", g_simConfig.tcpPort);
            }
        }
        netStarted = true;
    }

    updateDynamicValues();
    if (g_simConfig.rtuEnabled) handleRtuSerial();
    if (g_simConfig.tcpEnabled) handleTcpServer();
}
