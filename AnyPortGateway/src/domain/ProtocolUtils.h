#pragma once

#include "AppTypes.h"

/**
 * @brief 领域层通用协议与编码转换工具 (无状态纯函数)
 */

// 计算标准 Modbus RTU CRC16 (多项式 0xA001)
uint16_t calculateCRC16(const uint8_t *buf, size_t len);

// 十六进制字符串转字节数组
bool hexStringToBytes(const char *hex, uint8_t *buffer, size_t bufferSize,
                      size_t &outLength);

// 字节数组转大写十六进制紧凑字符串
String bytesToHexString(const uint8_t *data, size_t length);

// 解析点分十进制 IPv4 地址字符串
bool parseIpAddress(const char *str, IPAddress &outIp);

// 将数据位(7/8)、校验位(0:None,1:Even,2:Odd)、停止位(1/2) 转换为 Arduino Serial 配置掩码
uint32_t getSerialConfig(uint8_t dataBits, uint8_t parity, uint8_t stopBits);

// 校验并构建 MQTT 动态串口请求配置 ("none" / "even" / "odd")
bool buildRs485Config(uint32_t requestedBaud, uint8_t stopBits,
                      const char *parityStr, uint32_t &baudOut,
                      uint32_t &configOut);
