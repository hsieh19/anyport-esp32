#include "SimulatorMode.h"
#include "../domain/AppTypes.h"
#include "../domain/ProtocolUtils.h"
#include "../domain/SimulatorDomain.h"
#include "../infra/ConfigStore.h"
#include "../infra/NetworkManager.h"
#include "../infra/Rs485Driver.h"

static EspEthernetServer *s_simTcpEthServer = nullptr;
static EthernetClient s_simTcpEthClients[2];
static WiFiServer *s_simTcpWifiServer = nullptr;
static WiFiClient s_simTcpWifiClients[2];

static void handleRtuSerial() {
  if (RS485Serial.available() > 0) {
    uint8_t buf[256];
    int len = 0;
    unsigned long lastByte = millis();
    while (millis() - lastByte < 5 && len < 256) {
      if (RS485Serial.available()) {
        buf[len++] = RS485Serial.read();
        lastByte = millis();
      }
    }

    if (len < 4) return;

    uint16_t receivedCrc = (buf[len - 1] << 8) | buf[len - 2];
    if (calculateCRC16(buf, len - 2) != receivedCrc) {
      g_telemetry.busCrcErrorCount++;
      return;
    }

    uint8_t response[256];
    int respLen =
        simHandleModbusFrame(buf, len - 2, response, simGetConfig().unitId);
    if (respLen > 0) {
      g_telemetry.master1FrameCount++;
      uint16_t crc = calculateCRC16(response, respLen);
      response[respLen++] = crc & 0xFF;
      response[respLen++] = crc >> 8;

      RS485Serial.write(response, respLen);
      RS485Serial.flush();
    }
  }
}

static void processGenericTcpClient(Client &client) {
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
        uint8_t responsePdu[256];
        int respPduLen = simHandleModbusFrame(pdu, len, responsePdu,
                                              simGetConfig().tcpUnitId);

        if (respPduLen > 0) {
          g_telemetry.master2FrameCount++;
          uint8_t respFrame[263];
          memcpy(respFrame, mbap, 4);
          respFrame[4] = (respPduLen >> 8);
          respFrame[5] = (respPduLen & 0xFF);
          memcpy(&respFrame[6], responsePdu, respPduLen);

          client.write(respFrame, 6 + respPduLen);
          client.flush();
        }
      }
    }
  }
}

static void handleTcpServer() {
  if (s_simTcpEthServer) {
    EthernetClient newEth = s_simTcpEthServer->accept();
    if (newEth) {
      bool ok = false;
      for (int i = 0; i < 2; i++) {
        if (!s_simTcpEthClients[i] || !s_simTcpEthClients[i].connected()) {
          s_simTcpEthClients[i] = newEth;
          ok = true;
          break;
        }
      }
      if (!ok) newEth.stop();
    }
    for (int i = 0; i < 2; i++) {
      processGenericTcpClient(s_simTcpEthClients[i]);
    }
  }

  if (s_simTcpWifiServer) {
    WiFiClient newWifi = s_simTcpWifiServer->accept();
    if (newWifi) {
      bool ok = false;
      for (int i = 0; i < 2; i++) {
        if (!s_simTcpWifiClients[i] || !s_simTcpWifiClients[i].connected()) {
          s_simTcpWifiClients[i] = newWifi;
          ok = true;
          break;
        }
      }
      if (!ok) newWifi.stop();
    }
    for (int i = 0; i < 2; i++) {
      processGenericTcpClient(s_simTcpWifiClients[i]);
    }
  }
}

void SimulatorMode::begin() {
  configLoadSimulator();

  const SimulatorGlobalConfig &cfg = simGetConfig();
  if (cfg.rtuEnabled) {
    rs485BeginCurrent();
  }

  if (cfg.tcpEnabled) {
    uint8_t net = cfg.netInterface;
    if (net == 0 || net == 1) {
      if (s_simTcpEthServer) delete s_simTcpEthServer;
      s_simTcpEthServer = new EspEthernetServer(cfg.tcpPort);
      s_simTcpEthServer->begin();
      APP_LOG("[Simulator] TCP Server (Ethernet) started on port %d",
              cfg.tcpPort);
    }
    if (net == 0 || net == 2) {
      if (s_simTcpWifiServer) delete s_simTcpWifiServer;
      s_simTcpWifiServer = new WiFiServer(cfg.tcpPort);
      s_simTcpWifiServer->begin();
      APP_LOG("[Simulator] TCP Server (WiFi) started on port %d", cfg.tcpPort);
    }
  }
}

void SimulatorMode::loop() {
  const SimulatorGlobalConfig &cfg = simGetConfig();
  simUpdateDynamicValues();
  if (cfg.rtuEnabled) handleRtuSerial();
  if (cfg.tcpEnabled) handleTcpServer();
}
