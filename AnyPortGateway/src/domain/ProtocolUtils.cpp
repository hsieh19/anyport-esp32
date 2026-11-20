#include "ProtocolUtils.h"
#include <stdio.h>
#include <string.h>

uint16_t calculateCRC16(const uint8_t *buf, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t pos = 0; pos < len; pos++) {
    crc ^= static_cast<uint16_t>(buf[pos]);
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

bool hexStringToBytes(const char *hex, uint8_t *buffer, size_t bufferSize,
                      size_t &outLength) {
  size_t len = strlen(hex);
  if ((len % 2) != 0) return false;
  size_t byteCount = len / 2;
  if (byteCount > bufferSize) return false;
  for (size_t i = 0; i < byteCount; ++i) {
    char high = hex[i * 2];
    char low = hex[i * 2 + 1];
    uint8_t h = (high >= '0' && high <= '9')   ? high - '0'
                : (high >= 'A' && high <= 'F') ? high - 'A' + 10
                : (high >= 'a' && high <= 'f') ? high - 'a' + 10
                                               : 0xFF;
    uint8_t l = (low >= '0' && low <= '9')   ? low - '0'
                : (low >= 'A' && low <= 'F') ? low - 'A' + 10
                : (low >= 'a' && low <= 'f') ? low - 'a' + 10
                                             : 0xFF;
    if (h == 0xFF || l == 0xFF) return false;
    buffer[i] = (h << 4) | l;
  }
  outLength = byteCount;
  return true;
}

String bytesToHexString(const uint8_t *data, size_t length) {
  String result;
  static const char *hexDigits = "0123456789ABCDEF";
  result.reserve(length * 2);
  for (size_t i = 0; i < length; ++i) {
    uint8_t b = data[i];
    result += hexDigits[b >> 4];
    result += hexDigits[b & 0x0F];
  }
  return result;
}

bool parseIpAddress(const char *str, IPAddress &outIp) {
  if (!str) return false;
  int parts[4] = {0, 0, 0, 0};
  int count =
      sscanf(str, "%d.%d.%d.%d", &parts[0], &parts[1], &parts[2], &parts[3]);
  if (count != 4) return false;
  for (int i = 0; i < 4; ++i) {
    if (parts[i] < 0 || parts[i] > 255) return false;
  }
  outIp = IPAddress(parts[0], parts[1], parts[2], parts[3]);
  return true;
}

uint32_t getSerialConfig(uint8_t dataBits, uint8_t parity, uint8_t stopBits) {
  if (dataBits == 7) {
    if (parity == 1) return (stopBits == 2) ? SERIAL_7E2 : SERIAL_7E1;
    if (parity == 2) return (stopBits == 2) ? SERIAL_7O2 : SERIAL_7O1;
    return (stopBits == 2) ? SERIAL_7N2 : SERIAL_7N1;
  } else {
    if (parity == 1) return (stopBits == 2) ? SERIAL_8E2 : SERIAL_8E1;
    if (parity == 2) return (stopBits == 2) ? SERIAL_8O2 : SERIAL_8O1;
    return (stopBits == 2) ? SERIAL_8N2 : SERIAL_8N1;
  }
}

bool buildRs485Config(uint32_t requestedBaud, uint8_t stopBits,
                      const char *parityStr, uint32_t &baudOut,
                      uint32_t &configOut) {
  if (requestedBaud < 1200 || requestedBaud > 1152000) {
    return false;
  }
  if (stopBits != 1 && stopBits != 2) {
    return false;
  }

  uint32_t config = 0;
  if (strcmp(parityStr, "none") == 0) {
    config = (stopBits == 1) ? SERIAL_8N1 : SERIAL_8N2;
  } else if (strcmp(parityStr, "even") == 0) {
    config = (stopBits == 1) ? SERIAL_8E1 : SERIAL_8E2;
  } else if (strcmp(parityStr, "odd") == 0) {
    config = (stopBits == 1) ? SERIAL_8O1 : SERIAL_8O2;
  } else {
    return false;
  }

  baudOut = requestedBaud;
  configOut = config;
  return true;
}
