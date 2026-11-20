#include "EthWifiBridgeMode.h"
#include "../domain/AppTypes.h"
#include "../domain/ProtocolUtils.h"
#include "../infra/NetworkManager.h"
#include <EthernetUdp.h>
#include <WiFiUdp.h>

// TCP 桥接实例
static WiFiServer *s_ewWifiServer = nullptr;
static EspEthernetServer *s_ewEthServer = nullptr;
static WiFiClient s_ewWifiClient;
static EthernetClient s_ewEthClient;

// UDP 桥接实例与客户端源地址会话记忆
static WiFiUDP s_wifiUdp;
static EthernetUDP s_ethUdp;
static IPAddress s_targetIp;
static bool s_targetIpValid = false;
static IPAddress s_lastSenderIp;
static uint16_t s_lastSenderPort = 0;

static void pumpTcpBidirectional(Client &inbound, Client &outbound) {
  if (inbound.connected() && outbound.connected()) {
    while (inbound.available()) {
      uint8_t buf[512];
      size_t n = inbound.read(buf, sizeof(buf));
      outbound.write(buf, n);
      outbound.flush();
      g_telemetry.master2FrameCount++;
    }
    while (outbound.available()) {
      uint8_t buf[512];
      size_t n = outbound.read(buf, sizeof(buf));
      inbound.write(buf, n);
      inbound.flush();
      g_telemetry.master2FrameCount++;
    }
  } else {
    if (inbound && !inbound.connected()) outbound.stop();
    if (outbound && !outbound.connected()) inbound.stop();
  }
}

static void pumpUdpWiFiToEth() {
  if (!s_targetIpValid) return;
  uint8_t buf[512];

  // 1. 正向: WiFi 监听端口收到客户端请求 -> 转发给 W5500 有线目标设备
  int pktSize = s_wifiUdp.parsePacket();
  if (pktSize > 0) {
    s_lastSenderIp = s_wifiUdp.remoteIP();
    s_lastSenderPort = s_wifiUdp.remotePort();
    int len = s_wifiUdp.read(buf, sizeof(buf));
    if (len > 0) {
      s_ethUdp.beginPacket(s_targetIp, g_ethWifiConfig.targetPort);
      s_ethUdp.write(buf, len);
      s_ethUdp.endPacket();
      g_telemetry.master2FrameCount++;
    }
  }

  // 2. 逆向: W5500 收到有线目标设备应答 -> 原路回传给刚才的 WiFi 客户端
  int respSize = s_ethUdp.parsePacket();
  if (respSize > 0) {
    int len = s_ethUdp.read(buf, sizeof(buf));
    if (len > 0 && s_lastSenderPort != 0) {
      s_wifiUdp.beginPacket(s_lastSenderIp, s_lastSenderPort);
      s_wifiUdp.write(buf, len);
      s_wifiUdp.endPacket();
      g_telemetry.master2FrameCount++;
    }
  }
}

static void pumpUdpEthToWiFi() {
  if (!s_targetIpValid) return;
  uint8_t buf[512];

  // 1. 正向: W5500 监听端口收到有线客户端请求 -> 转发给 WiFi 无线目标设备
  int pktSize = s_ethUdp.parsePacket();
  if (pktSize > 0) {
    s_lastSenderIp = s_ethUdp.remoteIP();
    s_lastSenderPort = s_ethUdp.remotePort();
    int len = s_ethUdp.read(buf, sizeof(buf));
    if (len > 0) {
      s_wifiUdp.beginPacket(s_targetIp, g_ethWifiConfig.targetPort);
      s_wifiUdp.write(buf, len);
      s_wifiUdp.endPacket();
      g_telemetry.master2FrameCount++;
    }
  }

  // 2. 逆向: WiFi 收到无线目标设备应答 -> 原路回传给刚才的有线客户端
  int respSize = s_wifiUdp.parsePacket();
  if (respSize > 0) {
    int len = s_wifiUdp.read(buf, sizeof(buf));
    if (len > 0 && s_lastSenderPort != 0) {
      s_ethUdp.beginPacket(s_lastSenderIp, s_lastSenderPort);
      s_ethUdp.write(buf, len);
      s_ethUdp.endPacket();
      g_telemetry.master2FrameCount++;
    }
  }
}

void EthWifiBridgeMode::begin() {
  if (s_ewWifiServer) {
    delete s_ewWifiServer;
    s_ewWifiServer = nullptr;
  }
  if (s_ewEthServer) {
    delete s_ewEthServer;
    s_ewEthServer = nullptr;
  }
  s_wifiUdp.stop();
  s_ethUdp.stop();
  s_lastSenderPort = 0;
  s_targetIpValid = parseIpAddress(g_ethWifiConfig.targetIp.c_str(), s_targetIp);

  const bool isEthToWifi = (g_ethWifiConfig.direction == 1);
  const bool isUdp = (g_ethWifiConfig.protocol == 1);

  if (isUdp) {
    // UDP 模式: 入口网卡绑定 listenPort，出口网卡绑定本地随机/对称端口用于收发应答
    uint16_t outLocalPort =
        (g_ethWifiConfig.listenPort < 65000) ? (g_ethWifiConfig.listenPort + 1) : 50000;
    if (isEthToWifi) {
      s_ethUdp.begin(g_ethWifiConfig.listenPort);
      s_wifiUdp.begin(outLocalPort);
      APP_LOG("[Eth-WiFi] UDP Bridge: W5500(:%u) -> WiFi(%s:%u)",
              g_ethWifiConfig.listenPort, g_ethWifiConfig.targetIp.c_str(),
              g_ethWifiConfig.targetPort);
    } else {
      s_wifiUdp.begin(g_ethWifiConfig.listenPort);
      s_ethUdp.begin(outLocalPort);
      APP_LOG("[Eth-WiFi] UDP Bridge: WiFi(:%u) -> W5500(%s:%u)",
              g_ethWifiConfig.listenPort, g_ethWifiConfig.targetIp.c_str(),
              g_ethWifiConfig.targetPort);
    }
  } else {
    // TCP 模式
    if (isEthToWifi) {
      s_ewEthServer = new EspEthernetServer(g_ethWifiConfig.listenPort);
      s_ewEthServer->begin();
      APP_LOG("[Eth-WiFi] TCP Bridge: W5500(:%u) -> WiFi(%s:%u)",
              g_ethWifiConfig.listenPort, g_ethWifiConfig.targetIp.c_str(),
              g_ethWifiConfig.targetPort);
    } else {
      s_ewWifiServer = new WiFiServer(g_ethWifiConfig.listenPort);
      s_ewWifiServer->begin();
      APP_LOG("[Eth-WiFi] TCP Bridge: WiFi(:%u) -> W5500(%s:%u)",
              g_ethWifiConfig.listenPort, g_ethWifiConfig.targetIp.c_str(),
              g_ethWifiConfig.targetPort);
    }
  }
}

void EthWifiBridgeMode::loop() {
  const bool isEthToWifi = (g_ethWifiConfig.direction == 1);
  const bool isUdp = (g_ethWifiConfig.protocol == 1);

  if (isUdp) {
    if (isEthToWifi) {
      pumpUdpEthToWiFi();
    } else {
      pumpUdpWiFiToEth();
    }
    return;
  }

  // TCP 桥接循环
  if (isEthToWifi) {
    if (!s_ewEthServer) return;
    EthernetClient newEthClient = s_ewEthServer->accept();
    if (newEthClient) {
      if (s_ewEthClient.connected()) s_ewEthClient.stop();
      s_ewEthClient = newEthClient;
      if (s_targetIpValid &&
          s_ewWifiClient.connect(s_targetIp, g_ethWifiConfig.targetPort)) {
        APP_LOG("[Eth-WiFi] TCP connected to WiFi target %s:%d",
                g_ethWifiConfig.targetIp.c_str(), g_ethWifiConfig.targetPort);
      } else {
        s_ewEthClient.stop();
      }
    }
    pumpTcpBidirectional(s_ewEthClient, s_ewWifiClient);
  } else {
    if (!s_ewWifiServer) return;
    WiFiClient newWifiClient = s_ewWifiServer->accept();
    if (newWifiClient) {
      if (s_ewWifiClient.connected()) s_ewWifiClient.stop();
      s_ewWifiClient = newWifiClient;
      if (s_targetIpValid &&
          s_ewEthClient.connect(s_targetIp, g_ethWifiConfig.targetPort)) {
        APP_LOG("[Eth-WiFi] TCP connected to W5500 target %s:%d",
                g_ethWifiConfig.targetIp.c_str(), g_ethWifiConfig.targetPort);
      } else {
        s_ewWifiClient.stop();
      }
    }
    pumpTcpBidirectional(s_ewWifiClient, s_ewEthClient);
  }
}
