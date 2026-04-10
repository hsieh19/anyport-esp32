#pragma once

#include "Globals.h"
#include "ModbusHandler.h"

/**
 * @brief 协议互转模式核心处理逻辑 (Modbus TCP <-> RTU)
 */

static EspEthernetServer* g_bridgeTcpEthServer = nullptr;
static EthernetClient g_bridgeTcpEthClients[2];
static WiFiServer* g_bridgeTcpWifiServer = nullptr;
static WiFiClient g_bridgeTcpWifiClients[2];

static EthernetClient g_bridgeTargetEthClient;
static WiFiClient g_bridgeTargetWifiClient;

static void initBridgeMode() {
    // 初始化 RS485 端口
    RS485Serial.flush();
    RS485Serial.end();
    delay(10);
    
    uint32_t config = SERIAL_8N1;
    if (g_bridgeConfig.dataBits == 8) {
        if (g_bridgeConfig.parity == 1) config = (g_bridgeConfig.stopBits == 1) ? SERIAL_8E1 : SERIAL_8E2;
        else if (g_bridgeConfig.parity == 2) config = (g_bridgeConfig.stopBits == 1) ? SERIAL_8O1 : SERIAL_8O2;
        else config = (g_bridgeConfig.stopBits == 1) ? SERIAL_8N1 : SERIAL_8N2;
    } else {
        if (g_bridgeConfig.parity == 1) config = (g_bridgeConfig.stopBits == 1) ? SERIAL_7E1 : SERIAL_7E2;
        else if (g_bridgeConfig.parity == 2) config = (g_bridgeConfig.stopBits == 1) ? SERIAL_7O1 : SERIAL_7O2;
        else config = (g_bridgeConfig.stopBits == 1) ? SERIAL_7N1 : SERIAL_7N2;
    }
    RS485Serial.begin(g_bridgeConfig.baud, config, PIN_RS485_RX, PIN_RS485_TX);
    APP_LOG("[Bridge] RS485 initialized: %d %d%s%d", g_bridgeConfig.baud, g_bridgeConfig.dataBits, 
            (g_bridgeConfig.parity==0?"N":(g_bridgeConfig.parity==1?"E":"O")), g_bridgeConfig.stopBits);

    if (g_bridgeConfig.direction == 0) {
        uint8_t net = g_bridgeConfig.netInterface;
        if (net == 0 || net == 1) { // W5500
            if (g_bridgeTcpEthServer) delete g_bridgeTcpEthServer;
            g_bridgeTcpEthServer = new EspEthernetServer(g_bridgeConfig.tcpPort);
            g_bridgeTcpEthServer->begin();
            APP_LOG("[Bridge] TCP Server (Ethernet) started on port %d", g_bridgeConfig.tcpPort);
        }
        if (net == 0 || net == 2) { // WiFi
            if (g_bridgeTcpWifiServer) delete g_bridgeTcpWifiServer;
            g_bridgeTcpWifiServer = new WiFiServer(g_bridgeConfig.tcpPort);
            g_bridgeTcpWifiServer->begin();
            APP_LOG("[Bridge] TCP Server (WiFi) started on port %d", g_bridgeConfig.tcpPort);
        }
    } else {
        APP_LOG("[Bridge] RTU to TCP mode active. Target: %s:%d", g_bridgeConfig.targetIp.c_str(), g_bridgeConfig.targetPort);
    }
}

// TCP to RTU 处理：处理来自上位机的 TCP 请求转换为串口 RTU
static void processBridgeTcpClient(Client& client) {
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
            uint8_t targetUid = (g_bridgeConfig.bridgeMode == 1) ? g_bridgeConfig.slaveId : uid;
            
            rtuFrame[0] = targetUid;
            int pduLen = 1;
            unsigned long waitStart = millis();
            while (pduLen < len && millis() - waitStart < 50) {
                if (client.available()) {
                    rtuFrame[pduLen++] = client.read();
                }
            }

            if (pduLen == len) {
                uint16_t crc = calculateCRC(rtuFrame, len);
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
                    g_httpServer.handleClient();
                    delay(1);
                }
                g_rs485Busy = false;

                if (respLen >= 4) {
                    uint16_t rxCrc = (respBuf[respLen - 1] << 8) | respBuf[respLen - 2];
                    if (calculateCRC(respBuf, respLen - 2) == rxCrc) {
                        uint8_t tcpResp[260];
                        tcpResp[0] = (tid >> 8);
                        tcpResp[1] = (tid & 0xFF);
                        tcpResp[2] = 0; tcpResp[3] = 0;
                        uint16_t payloadLen = respLen - 2;
                        tcpResp[4] = (payloadLen >> 8);
                        tcpResp[5] = (payloadLen & 0xFF);
                        memcpy(&tcpResp[6], respBuf, payloadLen);
                        
                        client.write(tcpResp, payloadLen + 6);
                        client.flush();
                    }
                }
            }
        }
    }
}

static void handleTcpToRtu() {
    // 1. Ethernet
    if (g_bridgeTcpEthServer) {
        EthernetClient newEth = g_bridgeTcpEthServer->accept();
        if (newEth) {
            bool ok = false;
            for (int i = 0; i < 2; i++) {
                if (!g_bridgeTcpEthClients[i] || !g_bridgeTcpEthClients[i].connected()) {
                    g_bridgeTcpEthClients[i] = newEth; ok = true; break;
                }
            }
            if (!ok) newEth.stop();
        }
        for (int i = 0; i < 2; i++) processBridgeTcpClient(g_bridgeTcpEthClients[i]);
    }
    // 2. WiFi
    if (g_bridgeTcpWifiServer) {
        WiFiClient newWifi = g_bridgeTcpWifiServer->accept();
        if (newWifi) {
            bool ok = false;
            for (int i = 0; i < 2; i++) {
                if (!g_bridgeTcpWifiClients[i] || !g_bridgeTcpWifiClients[i].connected()) {
                    g_bridgeTcpWifiClients[i] = newWifi; ok = true; break;
                }
            }
            if (!ok) newWifi.stop();
        }
        for (int i = 0; i < 2; i++) processBridgeTcpClient(g_bridgeTcpWifiClients[i]);
    }
}

// RTU to TCP 处理：处理来自总线 Master 的 RTU 请求转换为 TCP 发送给 Slave
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
        
        // 模式过滤：单个设备模式下只转发特定 ID
        if (g_bridgeConfig.bridgeMode == 1 && rtuReq[0] != g_bridgeConfig.slaveId) {
            return; 
        }

        uint16_t rxCrc = (rtuReq[len - 1] << 8) | rtuReq[len - 2];
        if (calculateCRC(rtuReq, len - 2) == rxCrc) {
            // 转换为 TCP 并转发
            IPAddress targetIp;
            extern bool parseIpAddress(const char *str, IPAddress &outIp);
            if (parseIpAddress(g_bridgeConfig.targetIp.c_str(), targetIp)) {
                Client* targetClient = nullptr;
                if (g_bridgeConfig.netInterface == 2) { // 仅 WiFi
                    targetClient = &g_bridgeTargetWifiClient;
                } else { // 默认/以太网
                    targetClient = &g_bridgeTargetEthClient;
                }

                if (!targetClient->connected()) {
                    APP_LOG("[Bridge] Connecting to %s:%d...", g_bridgeConfig.targetIp.c_str(), g_bridgeConfig.targetPort);
                    targetClient->connect(targetIp, g_bridgeConfig.targetPort);
                }
                
                if (targetClient->connected()) {
                    uint8_t tcpReq[260];
                    tcpReq[0] = 0; tcpReq[1] = 1; // Fake TID
                    tcpReq[2] = 0; tcpReq[3] = 0; // PID
                    uint16_t payloadLen = len - 2;
                    tcpReq[4] = (payloadLen >> 8);
                    tcpReq[5] = (payloadLen & 0xFF);
                    memcpy(&tcpReq[6], rtuReq, payloadLen);
                    
                    targetClient->write(tcpReq, payloadLen + 6);
                    targetClient->flush();
                    
                    // 等待 TCP 响应
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
                                    // 转换回 RTU 响应
                                    uint16_t txCrc = calculateCRC(tPdu, tLen);
                                    tPdu[tLen] = txCrc & 0xFF;
                                    tPdu[tLen + 1] = txCrc >> 8;
                                    
                                    RS485Serial.write(tPdu, tLen + 2);
                                    RS485Serial.flush();
                                    break;
                                }
                            }
                        }
                        g_httpServer.handleClient(); // 保持 Web 响应
                        delay(1);
                    }
                }
            }
        }
    }
}

void bridgeLoop() {
    static bool bridgeStarted = false;
    if (!bridgeStarted) {
        initBridgeMode();
        bridgeStarted = true;
    }

    if (g_bridgeConfig.direction == 0) {
        handleTcpToRtu();
    } else {
        handleRtuToTcp();
    }
}
