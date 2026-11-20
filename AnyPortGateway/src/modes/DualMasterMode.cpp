#include "DualMasterMode.h"
#include "../domain/AppTypes.h"
#include "../domain/ProtocolUtils.h"
#include "../infra/Rs485Driver.h"
#include <HardwareSerial.h>
#include <WiFi.h>

// 专属 UART0 硬件串口实例 (严格隔离在模式 6 编译单元内，不污染其他模式)
static HardwareSerial s_masterSerial(0);

static WiFiServer *s_dmWifiServer = nullptr;
static WiFiClient s_dmWifiClients[2];
static volatile bool s_busBusy = false;

static uint8_t s_masterRxBuf[260];
static size_t s_masterRxLen = 0;
static unsigned long s_masterLastByteMs = 0;

void DualMasterMode::reconfigSerial() {
  // 关键保护：必须先同时关闭两个 UART 并释放旧的 GPIO 矩阵映射，再分别绑定新引脚
  // 彻底杜绝 UART1.end() 将 UART0 已绑定的 GPIO 2/3 从矩阵中误卸载
  s_masterSerial.flush();
  s_masterSerial.end();
  RS485Serial.flush();
  RS485Serial.end();
  delay(20);

  // 1. 初始化 UART0 连接 485-1 (原物理主站侧: RX=GPIO2, TX=GPIO3)
  uint32_t conf1 =
      getSerialConfig(g_rs485_1Config.dataBits, g_rs485_1Config.parity,
                      g_rs485_1Config.stopBits);
  s_masterSerial.begin(g_rs485_1Config.baud, conf1, PIN_RS485_1_RX,
                       PIN_RS485_1_TX);

  // 2. 初始化 UART1 连接 485-2 (现场从站侧: RX=GPIO5, TX=GPIO4)
  uint32_t conf2 =
      getSerialConfig(g_rs485_2Config.dataBits, g_rs485_2Config.parity,
                      g_rs485_2Config.stopBits);
  RS485Serial.begin(g_rs485_2Config.baud, conf2, PIN_RS485_2_RX,
                    PIN_RS485_2_TX);

  APP_LOG("[DM] Dual RS485 initialized: 485-1(UART0,GPIO2/3)=%u, "
          "485-2(UART1,GPIO5/4)=%u, Timeout=%ums",
          g_rs485_1Config.baud, g_rs485_2Config.baud,
          g_dualMasterConfig.masterTimeout);
}

static size_t forwardAndCollectResponse(const uint8_t *reqBuf, size_t reqLen,
                                        uint8_t *respBuf, size_t maxRespLen,
                                        unsigned long timeoutMs) {
  while (RS485Serial.available() > 0) {
    RS485Serial.read();
  }

  RS485Serial.write(reqBuf, reqLen);
  RS485Serial.flush();

  size_t respLen = 0;
  unsigned long startTime = millis();
  unsigned long lastByteTime = millis();
  const unsigned long frameTimeoutMs = 20;

  while (millis() - startTime < timeoutMs) {
    if (RS485Serial.available()) {
      respBuf[respLen++] = RS485Serial.read();
      lastByteTime = millis();
      if (respLen >= maxRespLen) break;
    } else if (respLen > 0 && (millis() - lastByteTime > frameTimeoutMs)) {
      break;
    }
    delay(1);
  }

  if (respLen > 0) {
    g_telemetry.lastBusLatencyMs = millis() - startTime;
  }
  return respLen;
}

static uint16_t getSafeSlaveTimeoutMs() {
  uint16_t timeoutMs = g_dualMasterConfig.masterTimeout > 0
                           ? g_dualMasterConfig.masterTimeout
                           : 1000;
  if (timeoutMs < 100) timeoutMs = 100;
  if (timeoutMs > 3000) timeoutMs = 3000;
  return timeoutMs;
}

static void processWifiMasterClient(WiFiClient &client) {
  if (!client || !client.connected()) return;

  if (client.available() >= 7) {
    uint8_t mbap[7];
    client.read(mbap, 7);

    uint16_t tid = (mbap[0] << 8) | mbap[1];
    uint16_t pid = (mbap[2] << 8) | mbap[3];
    uint16_t len = (mbap[4] << 8) | mbap[5];
    uint8_t uid = mbap[6];

    if (pid == 0 && len > 0 && len < 250) {
      uint8_t rtuFrame[260];
      rtuFrame[0] = uid;
      int pduLen = 1;
      unsigned long waitStart = millis();

      while (pduLen < len && (millis() - waitStart < 50)) {
        if (client.available()) {
          rtuFrame[pduLen++] = client.read();
        }
      }

      if (pduLen == len) {
        uint16_t crc = calculateCRC16(rtuFrame, len);
        rtuFrame[len] = crc & 0xFF;
        rtuFrame[len + 1] = crc >> 8;

        unsigned long startLockWait = millis();
        while (s_busBusy && (millis() - startLockWait < 1000)) {
          delay(1);
        }

        if (!s_busBusy) {
          s_busBusy = true;
          uint8_t respBuf[260];
          uint16_t timeoutMs = getSafeSlaveTimeoutMs();
          size_t respLen = forwardAndCollectResponse(
              rtuFrame, len + 2, respBuf, sizeof(respBuf), timeoutMs);
          s_busBusy = false;

          if (respLen >= 4) {
            uint16_t rxCrc = (respBuf[respLen - 1] << 8) | respBuf[respLen - 2];
            if (calculateCRC16(respBuf, respLen - 2) == rxCrc) {
              g_telemetry.master2FrameCount++;
              uint8_t tcpResp[260];
              tcpResp[0] = (tid >> 8);
              tcpResp[1] = (tid & 0xFF);
              tcpResp[2] = 0;
              tcpResp[3] = 0;
              uint16_t payloadLen = respLen - 2;
              tcpResp[4] = (payloadLen >> 8);
              tcpResp[5] = (payloadLen & 0xFF);
              memcpy(&tcpResp[6], respBuf, payloadLen);

              client.write(tcpResp, payloadLen + 6);
              client.flush();
            } else {
              g_telemetry.busCrcErrorCount++;
            }
          }
        }
      }
    }
  }
}

void DualMasterMode::begin() {
  reconfigSerial();

  if (s_dmWifiServer) {
    delete s_dmWifiServer;
    s_dmWifiServer = nullptr;
  }
  s_dmWifiServer = new WiFiServer(g_dualMasterConfig.wifiPort);
  s_dmWifiServer->begin();

  APP_LOG("[DM] Dual Master Relay initialized. WiFi Port: %u",
          g_dualMasterConfig.wifiPort);
}

void DualMasterMode::loop() {
  // 1. 拦截原物理主站 (485-1 / UART0 / s_masterSerial) 请求
  while (s_masterSerial.available() > 0 &&
         s_masterRxLen < sizeof(s_masterRxBuf)) {
    s_masterRxBuf[s_masterRxLen++] = s_masterSerial.read();
    s_masterLastByteMs = millis();
  }

  if (s_masterRxLen > 0 &&
      (millis() - s_masterLastByteMs > 15 ||
       s_masterRxLen >= sizeof(s_masterRxBuf))) {
    if (s_masterRxLen >= 4) {
      uint16_t rxCrc = (s_masterRxBuf[s_masterRxLen - 1] << 8) |
                       s_masterRxBuf[s_masterRxLen - 2];
      if (calculateCRC16(s_masterRxBuf, s_masterRxLen - 2) == rxCrc) {
        s_busBusy = true;
        uint8_t respBuf[260];
        uint16_t timeoutMs = getSafeSlaveTimeoutMs();
        size_t respLen = forwardAndCollectResponse(
            s_masterRxBuf, s_masterRxLen, respBuf, sizeof(respBuf), timeoutMs);
        s_busBusy = false;

        if (respLen >= 4) {
          uint16_t txCrc = (respBuf[respLen - 1] << 8) | respBuf[respLen - 2];
          if (calculateCRC16(respBuf, respLen - 2) == txCrc) {
            g_telemetry.master1FrameCount++;
            s_masterSerial.write(respBuf, respLen);
            s_masterSerial.flush();
          } else {
            g_telemetry.busCrcErrorCount++;
          }
        }
      } else {
        g_telemetry.busCrcErrorCount++;
      }
    }
    s_masterRxLen = 0;
  }

  // 2. 调度 WiFi 客户端 (Master 2 / AnyControl 上位机)
  if (s_dmWifiServer) {
    WiFiClient newClient = s_dmWifiServer->accept();
    if (newClient) {
      bool ok = false;
      for (int i = 0; i < 2; i++) {
        if (!s_dmWifiClients[i] || !s_dmWifiClients[i].connected()) {
          s_dmWifiClients[i] = newClient;
          ok = true;
          break;
        }
      }
      if (!ok) newClient.stop();
    }

    for (int i = 0; i < 2; i++) {
      processWifiMasterClient(s_dmWifiClients[i]);
    }
  }
}
