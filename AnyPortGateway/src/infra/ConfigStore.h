#pragma once

#include "../domain/AppTypes.h"

/**
 * @brief 基础设施层：NVS 持久化配置中心 (统一收口所有 Preferences 读写)
 */

// 启动时加载全量系统持久化配置
void configLoadAll();

// 原子提交全量/部分更新后的系统配置到 NVS 并同步运行时全局变量
bool configSaveSystem(uint8_t workMode, uint8_t rsCh, const String &otaApi,
                      const WifiStaConfig &wifi, const EthStaticConfig &eth,
                      const MqttRuntimeConfig &mqtt,
                      const Rs485PortConfig &rs1,
                      const Rs485PortConfig &rs2, uint32_t tBaud,
                      uint8_t tData, uint8_t tParity, uint8_t tStop,
                      BridgeConfig &br, const EthWifiConfig &ew,
                      DualMasterConfig &dm);

// 单独持久化工作模式切换
bool configSaveWorkMode(uint8_t mode);

// 单独持久化双主站模式 (Mode 6) 在线热重载配置
bool configSaveDualMasterHotReload(const DualMasterConfig &dm);

// 加载与保存从站模拟器配置及寄存器列表
void configLoadSimulator();
void configSaveSimulator();

// 读取与保存 OTA 升级前固件版本快照 (用于 A/B 分区回滚预演)
String configGetPrevFirmwareVersion();
void configSavePrevFirmwareVersion(const String &ver);
