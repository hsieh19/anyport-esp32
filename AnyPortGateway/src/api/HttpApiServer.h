#pragma once

#include "../domain/AppTypes.h"
#include "../domain/ProtocolUtils.h"
#include <ArduinoJson.h>
#include <WebServer.h>

/**
 * @brief 接口适配层：HTTP API 服务器基础设施、CORS/PNA 预检与通用 JSON 校验器
 */

static constexpr const char *API_MASK = "********";

extern WebServer g_httpServer;

void apiSendJson(int code, const String &json);
void apiError(int code, const String &message);
void apiOk(bool restart = false);
bool apiRequireJson();

template <typename T>
inline bool apiNum(JsonObject o, const char *k, long lo, long hi, T &out,
                   String &err) {
  if (!o.containsKey(k)) return true;
  JsonVariant v = o[k];
  if (!v.is<long>()) {
    err = String(k) + "_must_be_integer";
    return false;
  }
  long x = v.as<long>();
  if (x < lo || x > hi) {
    err = String(k) + "_out_of_range";
    return false;
  }
  out = static_cast<T>(x);
  return true;
}

bool apiStr(JsonObject o, const char *k, size_t maxLen, String &out,
            String &err);
bool apiSecret(JsonObject o, const char *k, size_t maxLen, String &out,
               String &err);
bool apiIp(JsonObject o, const char *k, IPAddress &out, bool &present,
           String &err);

// 启动 HTTP 服务器并挂载全部控制器路由
void httpServerInit();

// 处理客户端 HTTP 请求循环
void httpServerLoop();
