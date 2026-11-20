#include "HttpApiServer.h"
#include "SimulatorController.h"
#include "SystemController.h"

WebServer g_httpServer(80);

static const char *API_HEADER_KEYS[] = {"Content-Type"};

void apiSendJson(int code, const String &json) {
  g_httpServer.sendHeader("Cache-Control", "no-cache");
  g_httpServer.send(code, "application/json", json);
}

void apiError(int code, const String &message) {
  StaticJsonDocument<192> doc;
  doc["ok"] = false;
  doc["error"] = message;
  String out;
  serializeJson(doc, out);
  apiSendJson(code, out);
}

void apiOk(bool restart) {
  apiSendJson(200,
              restart ? "{\"ok\":true,\"restart\":true}" : "{\"ok\":true}");
}

bool apiRequireJson() {
  String ct = g_httpServer.header("Content-Type");
  ct.toLowerCase();
  if (ct.startsWith("application/json")) return true;
  apiError(415, "content_type_must_be_application_json");
  return false;
}

static void apiNotFound() {
  if (g_httpServer.method() == HTTP_OPTIONS) {
    g_httpServer.sendHeader("Access-Control-Allow-Private-Network", "true");
    g_httpServer.sendHeader("Access-Control-Max-Age", "600");
    g_httpServer.send(204);
    return;
  }
  apiError(404, "not_found");
}

bool apiStr(JsonObject o, const char *k, size_t maxLen, String &out,
            String &err) {
  if (!o.containsKey(k)) return true;
  JsonVariant v = o[k];
  if (!v.is<const char *>()) {
    err = String(k) + "_must_be_string";
    return false;
  }
  String s = v.as<const char *>();
  if (s.length() > maxLen) {
    err = String(k) + "_too_long";
    return false;
  }
  out = s;
  return true;
}

bool apiSecret(JsonObject o, const char *k, size_t maxLen, String &out,
               String &err) {
  String s;
  if (!apiStr(o, k, maxLen, s, err)) return false;
  if (s.length() > 0 && s != API_MASK) out = s;
  return true;
}

bool apiIp(JsonObject o, const char *k, IPAddress &out, bool &present,
           String &err) {
  present = o.containsKey(k);
  if (!present) return true;
  String s;
  if (!apiStr(o, k, 15, s, err)) return false;
  if (s.length() == 0) {
    out = IPAddress(0, 0, 0, 0);
    return true;
  }
  if (!parseIpAddress(s.c_str(), out)) {
    err = String(k) + "_invalid_ip";
    return false;
  }
  return true;
}

void httpServerInit() {
  g_httpServer.enableCORS(true);
  g_httpServer.collectHeaders(API_HEADER_KEYS, 1);
  g_httpServer.onNotFound(apiNotFound);

  registerSystemRoutes(g_httpServer);
  registerSimulatorRoutes(g_httpServer);

  g_httpServer.begin();
}

void httpServerLoop() { g_httpServer.handleClient(); }
