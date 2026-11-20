#pragma once

#include <WebServer.h>

/**
 * @brief 接口适配层：系统状态、全量配置读写、工作模式切换与 OTA 升级路由控制器
 */

void registerSystemRoutes(WebServer &server);
