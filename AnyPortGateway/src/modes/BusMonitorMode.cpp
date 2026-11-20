#include "BusMonitorMode.h"
#include "../domain/AppTypes.h"
#include "../domain/BusMonitorDomain.h"
#include "../infra/Rs485Driver.h"

static uint8_t s_rxBuf[260];
static size_t s_rxLen = 0;
static unsigned long s_lastByteMs = 0;

// 根据当前串口波特率计算 Modbus RTU 帧间隔静默阈值 (3.5T + 安全裕量)
static unsigned long getInterFrameGapMs() {
  uint32_t baud = g_rs485CurrentBaud > 0 ? g_rs485CurrentBaud : 9600;
  if (baud <= 2400) return 20;
  if (baud <= 4800) return 10;
  if (baud <= 9600) return 6;
  return 4;
}

void BusMonitorMode::begin() {
  RS485Serial.flush();
  RS485Serial.end();
  delay(10);

  rs485BeginCurrent();
  const Rs485PortConfig &cfg = getCurrentRs485Config();
  g_rs485CurrentBaud = cfg.baud;
  s_rxLen = 0;

  while (RS485Serial.available() > 0) {
    RS485Serial.read();
  }

  APP_LOG("[BusMonitor] Passive sniffer started on RS485-%d (%u %u%s%u)",
          g_rs485Channel + 1, cfg.baud, cfg.dataBits,
          (cfg.parity == 0 ? "N" : (cfg.parity == 1 ? "E" : "O")), cfg.stopBits);
}

void BusMonitorMode::loop() {
  // 1. 非阻塞被动读取串口字节流 (纯只读，绝不向总线写任何数据)
  while (RS485Serial.available() > 0 && s_rxLen < sizeof(s_rxBuf)) {
    int b = RS485Serial.read();
    if (b < 0) break;
    s_rxBuf[s_rxLen++] = static_cast<uint8_t>(b);
    s_lastByteMs = millis();
  }

  // 2. 达到帧间静默阈值或缓冲区满时，切分完整帧并送入质量诊断状态机
  if (s_rxLen > 0) {
    unsigned long gapMs = getInterFrameGapMs();
    if ((millis() - s_lastByteMs >= gapMs) || s_rxLen >= sizeof(s_rxBuf)) {
      busMonitorProcessFrame(s_rxBuf, s_rxLen);
      s_rxLen = 0;
    }
  } else {
    // 3. 空闲时巡检挂起的主站请求是否已超时无应答
    busMonitorCheckTimeout();
  }
}
