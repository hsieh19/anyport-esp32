#include "BridgeMode.h"
#include "../api/HttpApiServer.h"
#include "../domain/AppTypes.h"
#include "../domain/ProtocolUtils.h"
#include "../infra/NetworkManager.h"
#include "../infra/Rs485Driver.h"

static EspEthernetServer *s_bridgeTcpEthServer = nullptr;
static EthernetClient s_bridgeTcpEthClients[2];
static WiFiServer *s_bridgeTcpWifiServer = nullptr;
static WiFiClient s_bridgeTcpWifiClients[2];

static EthernetClient s_bridgeTargetEthClient;
static WiFiClient s_bridgeTargetWifiClient;

static void processBridgeTcpClient(Client &client) {
  if (!client || !client.connected()) return;
  if (client.available() >= 7) {
    uint8_t mbap[7];
    client.read(mbap, 7);

    uint16_t tid = (mbap[0] << 8) | mbap[1];
    uint16_t pid = (mbap[2] << 8) | mbap[3];
    uint16_t len = (mbap[4] << 8) | mbap[5];
    uint8_t uid = mbap[6];

    if (pid == 0 && len > 0 && len < 256) {
      uint8_t rtuFrame[260];
      uint8_t targetUid =
          (g_bridgeConfig.bridgeMode == 1) ? g_bridgeConfig.slaveId : uid;

      rtuFrame[0] = targetUid;
      int pduLen = 1;
      unsigned long waitStart = millis();
      while (pduLen < len && millis() - waitStart < 50) {
        if (client.available()) {
          rtuFrame[pduLen++] = client.read();
        }
      }

      if (pduLen == len) {
        uint16_t crc = calculateCRC16(rtuFrame, len);
        rtuFrame[len] = crc & 0xFF;
        rtuFrame[len + 1] = crc >> 8;

        while (g_rs485Busy) delay(1);
        g_rs485Busy = true;
        while (RS485Serial.available()) RS485Serial.read();

        RS485Serial.write(rtuFrame, len + 2);
        RS485Serial.flush();

        uint8_t respBuf[260];
        size_t respLen = 0;
        unsigned long startTime = millis();
        unsigned long lastByteTime = millis();
        while (millis() - startTime < 1000) {
          if (RS485Serial.available()) {
            respBuf[respLen++] = RS485Serial.read();
            lastByteTime = millis();
            if (respLen >= 260) break;
          } else if (respLen > 0 && (millis() - lastByteTime > 50)) {
            break;
          }
          httpServerLoop();
          delay(1);
        }
        g_rs485Busy = false;

        if (respLen > 0) {
          g_telemetry.lastBusLatencyMs = millis() - startTime;
        }
        if (respLen >= 4) {
          uint16_t rxCrc = (respBuf[respLen - 1] << 8) | respBuf[respLen - 2];
          if (calculateCRC16(respBuf, respLen - 2) == rxCrc) {
            g_telemetry.master1FrameCount++;
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

static void handleTcpToRtu() {
  if (s_bridgeTcpEthServer) {
    EthernetClient newEth = s_bridgeTcpEthServer->accept();
    if (newEth) {
      bool ok = false;
      for (int i = 0; i < 2; i++) {
        if (!s_bridgeTcpEthClients[i] || !s_bridgeTcpEthClients[i].connected()) {
          s_bridgeTcpEthClients[i] = newEth;
          ok = true;
          break;
        }
      }
      if (!ok) newEth.stop();
    }
    for (int i = 0; i < 2; i++) {
      processBridgeTcpClient(s_bridgeTcpEthClients[i]);
    }
  }

  if (s_bridgeTcpWifiServer) {
    WiFiClient newWifi = s_bridgeTcpWifiServer->accept();
    if (newWifi) {
      bool ok = false;
      for (int i = 0; i < 2; i++) {
        if (!s_bridgeTcpWifiClients[i] ||
            !s_bridgeTcpWifiClients[i].connected()) {
          s_bridgeTcpWifiClients[i] = newWifi;
          ok = true;
          break;
        }
      }
      if (!ok) newWifi.stop();
    }
    for (int i = 0; i < 2; i++) {
      processBridgeTcpClient(s_bridgeTcpWifiClients[i]);
    }
  }
}

static void handleRtuToTcp() {
  if (RS485Serial.available() >= 4) {
    uint8_t rtuReq[260];
    size_t len = 0;
    unsigned long lastByte = millis();
    while (millis() - lastByte < 5 && len < 260) {
      if (RS485Serial.available()) {
        rtuReq[len++] = RS485Serial.read();
        lastByte = millis();
      }
    }

    if (len < 4) return;

    if (g_bridgeConfig.bridgeMode == 1 &&
        rtuReq[0] != g_bridgeConfig.slaveId) {
      return;
    }

    uint16_t rxCrc = (rtuReq[len - 1] << 8) | rtuReq[len - 2];
    if (calculateCRC16(rtuReq, len - 2) == rxCrc) {
      IPAddress targetIp;
      if (parseIpAddress(g_bridgeConfig.targetIp.c_str(), targetIp)) {
        bool useWifi = (g_bridgeConfig.netInterface == 2) ||
                       (g_bridgeConfig.netInterface == 0 &&
                        Ethernet.linkStatus() != LinkON);
        Client *targetClient =
            useWifi ? static_cast<Client *>(&s_bridgeTargetWifiClient)
                    : static_cast<Client *>(&s_bridgeTargetEthClient);

        if (!targetClient->connected()) {
          APP_LOG("[Bridge] Connecting to %s:%d...",
                  g_bridgeConfig.targetIp.c_str(), g_bridgeConfig.targetPort);
          targetClient->connect(targetIp, g_bridgeConfig.targetPort);
        }

        if (targetClient->connected()) {
          uint8_t tcpReq[260];
          tcpReq[0] = 0;
          tcpReq[1] = 1;
          tcpReq[2] = 0;
          tcpReq[3] = 0;
          uint16_t payloadLen = len - 2;
          tcpReq[4] = (payloadLen >> 8);
          tcpReq[5] = (payloadLen & 0xFF);
          memcpy(&tcpReq[6], rtuReq, payloadLen);

          targetClient->write(tcpReq, payloadLen + 6);
          targetClient->flush();

          unsigned long startWait = millis();
          while (millis() - startWait < 1000) {
            if (targetClient->available() >= 6) {
              uint8_t tcpHead[6];
              targetClient->read(tcpHead, 6);
              uint16_t tLen = (tcpHead[4] << 8) | tcpHead[5];
              if (tLen > 0 && tLen < 256) {
                uint8_t tPdu[260];
                size_t got = 0;
                unsigned long pduWait = millis();
                while (got < tLen && millis() - pduWait < 100) {
                  if (targetClient->available()) {
                    tPdu[got++] = targetClient->read();
                  }
                }

                if (got == tLen) {
                  g_telemetry.master1FrameCount++;
                  g_telemetry.master2FrameCount++;
                  uint16_t txCrc = calculateCRC16(tPdu, tLen);
                  tPdu[tLen] = txCrc & 0xFF;
                  tPdu[tLen + 1] = txCrc >> 8;

                  RS485Serial.write(tPdu, tLen + 2);
                  RS485Serial.flush();
                  break;
                }
              }
            }
            httpServerLoop();
            delay(1);
          }
        }
      }
    } else {
      g_telemetry.busCrcErrorCount++;
    }
  }
}

void BridgeMode::begin() {
  RS485Serial.flush();
  RS485Serial.end();
  delay(10);
  rs485BeginCurrent();
  const Rs485PortConfig &cfg = getCurrentRs485Config();
  APP_LOG("[Bridge] RS485 initialized: %d %d%s%d", cfg.baud, cfg.dataBits,
          (cfg.parity == 0 ? "N" : (cfg.parity == 1 ? "E" : "O")), cfg.stopBits);

  if (g_bridgeConfig.direction == 0) {
    uint8_t net = g_bridgeConfig.netInterface;
    if (net == 0 || net == 1) {
      if (s_bridgeTcpEthServer) delete s_bridgeTcpEthServer;
      s_bridgeTcpEthServer = new EspEthernetServer(g_bridgeConfig.tcpPort);
      s_bridgeTcpEthServer->begin();
      APP_LOG("[Bridge] TCP Server (Ethernet) started on port %d",
              g_bridgeConfig.tcpPort);
    }
    if (net == 0 || net == 2) {
      if (s_bridgeTcpWifiServer) delete s_bridgeTcpWifiServer;
      s_bridgeTcpWifiServer = new WiFiServer(g_bridgeConfig.tcpPort);
      s_bridgeTcpWifiServer->begin();
      APP_LOG("[Bridge] TCP Server (WiFi) started on port %d",
              g_bridgeConfig.tcpPort);
    }
  } else {
    APP_LOG("[Bridge] RTU to TCP mode active. Target: %s:%d",
            g_bridgeConfig.targetIp.c_str(), g_bridgeConfig.targetPort);
  }
}

void BridgeMode::loop() {
  if (g_bridgeConfig.direction == 0) {
    handleTcpToRtu();
  } else {
    handleRtuToTcp();
  }
}
