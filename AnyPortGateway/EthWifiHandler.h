#pragma once

#include "Globals.h"

/**
 * @brief 以太网转 WiFi 桥接模式处理逻辑
 * 
 * 场景：
 * 1. WiFi TCP Server <-> Ethernet TCP Client (透明转发)
 */

static EspEthernetServer* g_ewEthServer = nullptr;
static WiFiServer* g_ewWifiServer = nullptr;

static EthernetClient g_ewEthClients[2];
static WiFiClient g_ewWifiClients[2];

// 简单逻辑：WiFi 开启 Server，收到数据转发给 Ethernet 目标
static WiFiClient g_ewExternalWifiClient;
static EthernetClient g_ewInternalEthClient;

void ethWifiBridgeLoop() {
    static bool ewStarted = false;
    if (!ewStarted) {
        if (g_ewWifiServer) delete g_ewWifiServer;
        g_ewWifiServer = new WiFiServer(g_ethWifiConfig.listenPort);
        g_ewWifiServer->begin();
        APP_LOG("[Eth-WiFi] WiFi Server started on port %d", g_ethWifiConfig.listenPort);
        ewStarted = true;
    }

    // 接受来自 WiFi 的连接
    WiFiClient newWifiClient = g_ewWifiServer->accept();
    if (newWifiClient) {
        if (g_ewExternalWifiClient.connected()) {
            g_ewExternalWifiClient.stop();
        }
        g_ewExternalWifiClient = newWifiClient;
        APP_LOG("[Eth-WiFi] New WiFi Client connected");
        
        // 尝试连接以太网目标
        IPAddress targetIp;
        extern bool parseIpAddress(const char *str, IPAddress &outIp);
        if (parseIpAddress(g_ethWifiConfig.targetIp.c_str(), targetIp)) {
            if (g_ewInternalEthClient.connect(targetIp, g_ethWifiConfig.targetPort)) {
                APP_LOG("[Eth-WiFi] Connected to Ethernet target %s:%d", g_ethWifiConfig.targetIp.c_str(), g_ethWifiConfig.targetPort);
            } else {
                APP_LOG("[Eth-WiFi] Failed to connect to Ethernet target");
                g_ewExternalWifiClient.stop();
            }
        }
    }

    // 双向转发
    if (g_ewExternalWifiClient.connected() && g_ewInternalEthClient.connected()) {
        // WiFi -> Ethernet
        while (g_ewExternalWifiClient.available()) {
            uint8_t buf[512];
            size_t n = g_ewExternalWifiClient.read(buf, sizeof(buf));
            g_ewInternalEthClient.write(buf, n);
            g_ewInternalEthClient.flush();
        }
        // Ethernet -> WiFi
        while (g_ewInternalEthClient.available()) {
            uint8_t buf[512];
            size_t n = g_ewInternalEthClient.read(buf, sizeof(buf));
            g_ewExternalWifiClient.write(buf, n);
            g_ewExternalWifiClient.flush();
        }
    } else {
        // 如果有一方断开，清理另一方
        if (g_ewExternalWifiClient && !g_ewExternalWifiClient.connected()) g_ewInternalEthClient.stop();
        if (g_ewInternalEthClient && !g_ewInternalEthClient.connected()) g_ewExternalWifiClient.stop();
    }
}
