#pragma once

#include "Globals.h"
#include "ModbusHandler.h"

/**
 * @brief 协议互转模式核心处理逻辑 (Modbus TCP <-> RTU)
 */

static EspEthernetServer* g_bridgeTcpServer = nullptr;
static EthernetClient g_bridgeTcpClients[4];
static EthernetClient g_bridgeTargetClient;

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

    if (g_bridgeConfig.direction == 0) { // 使用 Globals.h 中定义的 EspEthernetServer
        if (g_bridgeTcpServer) delete g_bridgeTcpServer;
        g_bridgeTcpServer = new EspEthernetServer(g_bridgeConfig.tcpPort);
        g_bridgeTcpServer->begin();
        APP_LOG("[Bridge] TCP Server started on port %d", g_bridgeConfig.tcpPort);
    } else {
        APP_LOG("[Bridge] RTU to TCP mode active. Target: %s:%d", g_bridgeConfig.targetIp.c_str(), g_bridgeConfig.targetPort);
    }
}

// TCP to RTU 处理：处理来自上位机的 TCP 请求转换为串口 RTU
static void handleTcpToRtu() {
    if (!g_bridgeTcpServer) return;

    EthernetClient newClient = g_bridgeTcpServer->accept();
    if (newClient) {
        bool accepted = false;
        for (int i = 0; i < 4; i++) {
            if (!g_bridgeTcpClients[i] || !g_bridgeTcpClients[i].connected()) {
                g_bridgeTcpClients[i] = newClient;
                accepted = true;
                break;
            }
        }
        if (!accepted) newClient.stop();
    }

    for (int i = 0; i < 4; i++) {
        if (g_bridgeTcpClients[i] && g_bridgeTcpClients[i].connected()) {
            if (g_bridgeTcpClients[i].available() >= 7) {
                uint8_t mbap[7];
                g_bridgeTcpClients[i].read(mbap, 7);
                
                uint16_t tid = (mbap[0] << 8) | mbap[1];
                uint16_t pid = (mbap[2] << 8) | mbap[3];
                uint16_t len = (mbap[4] << 8) | mbap[5];
                uint8_t uid = mbap[6];

                if (pid == 0 && len > 0 && len < 256) {
                    uint8_t rtuFrame[260];
                    // 地址处理 logic: 单个设备模式强制覆盖 UID，总线模式使用透传 UID
                    uint8_t targetUid = (g_bridgeConfig.bridgeMode == 1) ? g_bridgeConfig.slaveId : uid;
                    
                    rtuFrame[0] = targetUid;
                    int pduLen = 1;
                    unsigned long waitStart = millis();
                    while (pduLen < len && millis() - waitStart < 50) {
                        if (g_bridgeTcpClients[i].available()) {
                            rtuFrame[pduLen++] = g_bridgeTcpClients[i].read();
                        }
                    }

                    if (pduLen == len) {
                        // 计算并添加 CRC
                        uint16_t crc = calculateCRC(rtuFrame, len);
                        rtuFrame[len] = crc & 0xFF;
                        rtuFrame[len + 1] = crc >> 8;

                        // 发送到 RS485
                        while (g_rs485Busy) delay(1);
                        g_rs485Busy = true;
                        
                        // 清空缓冲区
                        while (RS485Serial.available()) RS485Serial.read();
                        
                        RS485Serial.write(rtuFrame, len + 2);
                        RS485Serial.flush();

                        // 等待从站响应
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
                                break; // 帧结束
                            }
                            g_httpServer.handleClient(); // 保持 Web 响应
                            delay(1);
                        }
                        g_rs485Busy = false;

                        if (respLen >= 4) { // Valid RTU response: Min 4 bytes (Addr, Func, CRC_L, CRC_H)
                            uint16_t rxCrc = (respBuf[respLen - 1] << 8) | respBuf[respLen - 2];
                            if (calculateCRC(respBuf, respLen - 2) == rxCrc) {
                                // 转换回 TCP 响应
                                uint8_t tcpResp[260];
                                tcpResp[0] = (tid >> 8);
                                tcpResp[1] = (tid & 0xFF);
                                tcpResp[2] = 0; tcpResp[3] = 0; // Protocol ID
                                uint16_t payloadLen = respLen - 2;
                                tcpResp[4] = (payloadLen >> 8);
                                tcpResp[5] = (payloadLen & 0xFF);
                                memcpy(&tcpResp[6], respBuf, payloadLen);
                                
                                g_bridgeTcpClients[i].write(tcpResp, payloadLen + 6);
                                g_bridgeTcpClients[i].flush();
                            }
                        }
                    }
                }
            }
        }
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
                if (!g_bridgeTargetClient.connected()) {
                    APP_LOG("[Bridge] Connecting to %s:%d...", g_bridgeConfig.targetIp.c_str(), g_bridgeConfig.targetPort);
                    g_bridgeTargetClient.connect(targetIp, g_bridgeConfig.targetPort);
                }
                
                if (g_bridgeTargetClient.connected()) {
                    uint8_t tcpReq[260];
                    tcpReq[0] = 0; tcpReq[1] = 1; // Fake TID
                    tcpReq[2] = 0; tcpReq[3] = 0; // PID
                    uint16_t payloadLen = len - 2;
                    tcpReq[4] = (payloadLen >> 8);
                    tcpReq[5] = (payloadLen & 0xFF);
                    memcpy(&tcpReq[6], rtuReq, payloadLen);
                    
                    g_bridgeTargetClient.write(tcpReq, payloadLen + 6);
                    g_bridgeTargetClient.flush();
                    
                    // 等待 TCP 响应
                    unsigned long startWait = millis();
                    while (millis() - startWait < 1000) {
                        if (g_bridgeTargetClient.available() >= 6) {
                            uint8_t tcpHead[6];
                            g_bridgeTargetClient.read(tcpHead, 6);
                            uint16_t tLen = (tcpHead[4] << 8) | tcpHead[5];
                            if (tLen > 0 && tLen < 256) {
                                uint8_t tPdu[260];
                                size_t got = 0;
                                unsigned long pduWait = millis();
                                while (got < tLen && millis() - pduWait < 100) {
                                    if (g_bridgeTargetClient.available()) {
                                        tPdu[got++] = g_bridgeTargetClient.read();
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
