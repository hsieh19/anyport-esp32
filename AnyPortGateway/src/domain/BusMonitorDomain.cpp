#include "BusMonitorDomain.h"
#include "ProtocolUtils.h"
#include <string.h>

// -----------------------
// 1. 模块私有静态状态池 (BSS 段确定性分配，零堆内存碎片)
// -----------------------
static BusMonitorConfig s_monitorConfig = {0, 0, 1000};
static BusQualityStats s_qualityStats = {};

static BusMonitorLog s_monitorLogs[MAX_MONITOR_LOGS];
static size_t s_monitorHead = 0;
static size_t s_monitorCount = 0;

struct PendingMasterRequest {
  bool active;
  uint8_t slaveId;
  uint8_t funcCode;
  uint16_t regCount;
  uint8_t reqParams[4]; // 请求帧 bytes[2..5] (起始地址与数量/设定值)
  unsigned long timestampMs;
};

static PendingMasterRequest s_pendingReq = {false, 0, 0, 0, {0, 0, 0, 0}, 0};

// -----------------------
// 2. 访问器与缓冲区管理
// -----------------------
BusMonitorConfig &busMonitorGetConfig() { return s_monitorConfig; }
const BusQualityStats &busMonitorGetStats() { return s_qualityStats; }
size_t busMonitorGetLogCount() { return s_monitorCount; }

const BusMonitorLog &busMonitorGetLogAt(size_t logicalIndex) {
  return s_monitorLogs[(s_monitorHead + logicalIndex) % MAX_MONITOR_LOGS];
}

void busMonitorClear() {
  s_monitorHead = 0;
  s_monitorCount = 0;
  memset(&s_qualityStats, 0, sizeof(s_qualityStats));
  s_pendingReq.active = false;

  g_telemetry.lastBusLatencyMs = 0;
  g_telemetry.master1FrameCount = 0;
  g_telemetry.master2FrameCount = 0;
  g_telemetry.busCrcErrorCount = 0;
}

static void appendMonitorLog(unsigned long now, BusFrameMeta meta,
                             uint8_t slaveId, uint8_t funcCode, uint16_t rttMs,
                             const uint8_t *data, size_t len) {
  // 1. 从站 ID 过滤 (0 表示全总线监听)
  if (s_monitorConfig.filterSlaveId != 0 &&
      slaveId != s_monitorConfig.filterSlaveId) {
    return;
  }

  // 2. 功能码过滤 (屏蔽异常最高位 0x80)
  if (s_monitorConfig.filterFuncCode != 0) {
    uint8_t baseFc = funcCode & 0x7F;
    if (baseFc != s_monitorConfig.filterFuncCode) {
      return;
    }
  }

  size_t index = (s_monitorHead + s_monitorCount) % MAX_MONITOR_LOGS;
  s_monitorLogs[index].timestamp = now;
  s_monitorLogs[index].meta = static_cast<uint8_t>(meta);
  s_monitorLogs[index].slaveId = slaveId;
  s_monitorLogs[index].funcCode = funcCode;
  s_monitorLogs[index].rttMs = rttMs;
  s_monitorLogs[index].length = (len > 260) ? 260 : len;
  memcpy(s_monitorLogs[index].data, data, s_monitorLogs[index].length);

  if (s_monitorCount < MAX_MONITOR_LOGS) {
    s_monitorCount++;
  } else {
    s_monitorHead = (s_monitorHead + 1) % MAX_MONITOR_LOGS;
  }
}

// -----------------------
// 3. 主从请求-应答配对与通讯质量分析核心算法
// -----------------------
static uint16_t recordMatchedRtt(unsigned long now) {
  unsigned long elapsed =
      (now >= s_pendingReq.timestampMs) ? (now - s_pendingReq.timestampMs) : 1;
  if (elapsed == 0) elapsed = 1;
  uint16_t rtt = (elapsed > 60000) ? 60000 : static_cast<uint16_t>(elapsed);

  s_qualityStats.respCount++;
  s_qualityStats.lastRttMs = rtt;
  if (s_qualityStats.minRttMs == 0 || rtt < s_qualityStats.minRttMs) {
    s_qualityStats.minRttMs = rtt;
  }
  if (rtt > s_qualityStats.maxRttMs) {
    s_qualityStats.maxRttMs = rtt;
  }
  s_qualityStats.totalRttMs += rtt;
  g_telemetry.lastBusLatencyMs = rtt;

  s_pendingReq.active = false;
  return rtt;
}

// 判断当前有效 CRC 帧是否为挂起主站请求的配对从站响应
static bool isPairedSlaveResponse(const uint8_t *data, size_t len) {
  if (!s_pendingReq.active || len < 5) return false;

  uint8_t uid = data[0];
  uint8_t fc = data[1];
  uint8_t baseFc = fc & 0x7F;

  if (uid != s_pendingReq.slaveId || baseFc != s_pendingReq.funcCode) {
    return false;
  }

  // 异常响应帧: [uid][fc|0x80][excCode][crcLo][crcHi] 固定 5 字节
  if ((fc & 0x80) != 0 && len == 5) {
    return true;
  }

  // 标准功能码结构精确匹配，杜绝把主站超时重发帧误判为从站响应
  if (baseFc == 0x01 || baseFc == 0x02) {
    uint16_t expectedBytes = (s_pendingReq.regCount + 7) / 8;
    return (expectedBytes > 0 && data[2] == expectedBytes &&
            len == static_cast<size_t>(5 + expectedBytes));
  }
  if (baseFc == 0x03 || baseFc == 0x04) {
    uint16_t expectedBytes = s_pendingReq.regCount * 2;
    return (expectedBytes > 0 && data[2] == expectedBytes &&
            len == static_cast<size_t>(5 + expectedBytes));
  }
  if (baseFc == 0x05 || baseFc == 0x06 || baseFc == 0x0F || baseFc == 0x10) {
    return (len == 8 && memcmp(&data[2], s_pendingReq.reqParams, 4) == 0);
  }

  return true;
}

// 判断无挂起匹配时，一帧报文是否符合主站请求结构
static bool isMasterRequestStructure(const uint8_t *data, size_t len) {
  if (len < 6) return false;
  uint8_t fc = data[1];
  if (fc & 0x80) return false;

  if (fc >= 0x01 && fc <= 0x06) {
    return (len == 8);
  }
  if (fc == 0x0F || fc == 0x10) {
    return (len >= 10 && len == static_cast<size_t>(9 + data[6]));
  }
  return (len == 8);
}

void busMonitorCheckTimeout() {
  if (!s_pendingReq.active) return;
  uint16_t limitMs =
      (s_monitorConfig.timeoutMs >= 100 && s_monitorConfig.timeoutMs <= 5000)
          ? s_monitorConfig.timeoutMs
          : 1000;
  if (millis() - s_pendingReq.timestampMs > limitMs) {
    s_qualityStats.timeoutCount++;
    s_pendingReq.active = false;
  }
}

void busMonitorProcessFrame(const uint8_t *data, size_t len) {
  if (len == 0) return;
  unsigned long now = millis();

  // 先检查上一笔挂起请求是否已经超过了静默超时阈值
  busMonitorCheckTimeout();

  s_qualityStats.totalFrames++;

  uint8_t uid = data[0];
  uint8_t fc = (len > 1) ? data[1] : 0;

  // 1. 校验物理帧长度与 CRC16
  if (len < 4) {
    s_qualityStats.crcErrorCount++;
    g_telemetry.busCrcErrorCount++;
    appendMonitorLog(now, BusFrameMeta::CRC_ERR, uid, fc, 0, data, len);
    return;
  }

  uint16_t rxCrc = (data[len - 1] << 8) | data[len - 2];
  uint16_t calcCrc = calculateCRC16(data, len - 2);
  if (rxCrc != calcCrc) {
    s_qualityStats.crcErrorCount++;
    g_telemetry.busCrcErrorCount++;
    appendMonitorLog(now, BusFrameMeta::CRC_ERR, uid, fc, 0, data, len);
    return;
  }

  g_telemetry.master1FrameCount++;

  // 2. 检查是否与当前挂起的主站请求配对成功
  if (isPairedSlaveResponse(data, len)) {
    uint16_t rtt = recordMatchedRtt(now);
    bool isExc = (fc & 0x80) != 0;
    if (isExc) {
      s_qualityStats.exceptionCount++;
    }
    appendMonitorLog(now, isExc ? BusFrameMeta::EXCEPTION : BusFrameMeta::RESP,
                     uid, fc, rtt, data, len);
    return;
  }

  // 3. 若未配对成功，但之前仍有未获应答的挂起请求，说明主站已放弃上一轮并发送了新请求
  if (s_pendingReq.active) {
    s_qualityStats.timeoutCount++;
    s_pendingReq.active = false;
  }

  // 4. 判定当前帧是否为主站请求帧或独立响应帧
  if ((fc & 0x80) != 0 && len == 5) {
    s_qualityStats.exceptionCount++;
    appendMonitorLog(now, BusFrameMeta::EXCEPTION, uid, fc, 0, data, len);
  } else if (isMasterRequestStructure(data, len)) {
    // 广播地址 (0) 无需从站应答，不计入需配对的挂起请求
    if (uid != 0) {
      s_qualityStats.reqCount++;
      s_pendingReq.active = true;
      s_pendingReq.slaveId = uid;
      s_pendingReq.funcCode = fc;
      s_pendingReq.regCount = (data[4] << 8) | data[5];
      memcpy(s_pendingReq.reqParams, &data[2], 4);
      s_pendingReq.timestampMs = now;
    }
    appendMonitorLog(now, BusFrameMeta::REQ, uid, fc, 0, data, len);
  } else {
    appendMonitorLog(now, BusFrameMeta::RESP, uid, fc, 0, data, len);
  }
}
