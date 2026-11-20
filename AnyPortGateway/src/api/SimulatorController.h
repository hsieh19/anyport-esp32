#pragma once

#include <WebServer.h>

/**
 * @brief 接口适配层：从站模拟器专属 Web API 路由控制器
 */

void registerSimulatorRoutes(WebServer &server);
