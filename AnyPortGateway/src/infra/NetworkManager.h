#pragma once

#include "../domain/AppTypes.h"
#include <Ethernet.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>

/**
 * @brief 基础设施层：WiFi、W5500 硬以太网 PHY 建链与 NTP 时间同步管理
 */

// 针对 ESP32 核心的 EthernetServer 兼容性封装
class EspEthernetServer : public EthernetServer {
public:
  explicit EspEthernetServer(uint16_t port) : EthernetServer(port) {}
  void begin(uint16_t port) override { EthernetServer::begin(); }
  void begin() { EthernetServer::begin(); }
};

extern uint8_t g_macAddress[6];

void initWifi();
void initSpiAndEthernet();
