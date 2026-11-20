#pragma once

#include "../domain/AppTypes.h"

/**
 * @brief 基础设施层：远程固件更新 (OTA) 与原生 A/B 分区安全回滚服务
 */

// 获取备用分区信息（用于回滚展示）
void getBackupPartitionInfo(String &label, bool &canRollback);

// 执行固件回滚 - 切换启动分区到上一个版本
bool executeRollback();

// 获取并校验下载链接 (刷新带签名 URL)
bool validateFirmwareExistence();

// 检查是否已配置有效的 OTA 服务地址 (非空且非示例占位域名)
bool isOtaApiConfigured();

// 检查是否存在新版本 (返回: 1=有更新, 0=无更新, -1=请求或解析失败, -2=无网络, -3=未配置OTA地址)
int checkOtaUpdate();

// 主循环 24h 定时自检
void otaAutoCheckLoop();

// 执行固件更新 (使用已预检的链接)
void executeFirmwareUpdate();

// 状态只读访问器
const String &otaGetRemoteVersion();
const String &otaGetChangelog();
bool otaIsUpdateFound();
