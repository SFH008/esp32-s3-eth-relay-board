#include "ShellyRpc.h"
#include "HttpHandlers.h"
#include "RelayControl.h"
#include "BoardPins.h"
#include "Config.h"
#include "Logger.h"
#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ETH.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <time.h>

namespace {
constexpr uint8_t COUNT = BoardPins::RELAY_COUNT;
constexpr size_t MAX_RPC = 4096;
String identity, macAddress;
String names[COUNT];
bool enabled = true, mdnsStarted = false;
uint8_t lastMask = 0;
uint32_t configRevision = 1, lastFull = 0;
String fragments[WEBSOCKETS_SERVER_CLIENT_MAX];

// WebServer already parsed the upgrade headers. Feed them into the library's
// handshake implementation; its core owns the socket from this point onwards.
class SharedWebSockets : public WebSocketsServerCore {
public:
  SharedWebSockets() : WebSocketsServerCore("", "") {}
  bool upgrade() {
    if (connectedClients() >= WEBSOCKETS_SERVER_CLIENT_MAX) return false;
    auto* tcp = new WiFiClient(server.client());
    WSclient_t* c = newClient(tcp);
    if (!c) { delete tcp; return false; }
    server.releaseClient();
    auto line = [&](String text) { handleHeader(c, &text); };
    line("GET /rpc HTTP/1.1\r");
    line("Upgrade: websocket\r");
    line("Connection: Upgrade\r");
    line("Sec-WebSocket-Version: 13\r");
    line("Sec-WebSocket-Key: " + server.header("Sec-WebSocket-Key") + "\r");
    line("\r");
    return true;
  }
};
SharedWebSockets ws;

void fail(JsonDocument& doc, int code, const char* message) {
  doc.remove("result");
  auto e = doc["error"].to<JsonObject>();
  e["code"] = code;
  e["message"] = message;
}
void info(JsonObject o) {
  o["id"] = identity;
  o["mac"] = macAddress;
  o["name"] = g_cfg.hostname;
  o["model"] = "Waveshare-8RO";
  o["gen"] = 2;
  o["app"] = "Waveshare8RO";
  o["ver"] = "1.4.0"; // emulated protocol compatibility version
  o["fw_id"] = "20261108-000000/1.4.0-waveshare";
  o["auth_en"] = false;
  o["profile"] = "switch";
}
void switchConfig(JsonObject o, uint8_t id) {
  o["id"] = id;
  o["name"] = names[id];
  o["initial_state"] = "off";
  o["auto_on"] = false;
  o["auto_off"] = false;
}
void switchStatus(JsonObject o, uint8_t id, uint8_t mask) {
  o["id"] = id;
  o["source"] = "init";
  o["output"] = bool(mask & (1u << id));
}
void status(JsonObject o, uint8_t mask) {
  auto sys = o["sys"].to<JsonObject>();
  sys["mac"] = macAddress;
  sys["uptime"] = millis() / 1000;
  sys["ram_free"] = ESP.getFreeHeap();
  sys["cfg_rev"] = configRevision;
  auto eth = o["eth"].to<JsonObject>();
  eth["ip"] = ETH.localIP().toString();
  for (uint8_t id = 0; id < COUNT; ++id)
    switchStatus(o["switch:" + String(id)].to<JsonObject>(), id, mask);
}
void config(JsonObject o) {
  auto device = o["sys"]["device"].to<JsonObject>();
  device["name"] = g_cfg.hostname;
  device["mac"] = macAddress;
  device["fw_id"] = "20261108-000000/1.4.0-waveshare";
  device["profile"] = "switch";
  for (uint8_t id = 0; id < COUNT; ++id)
    switchConfig(o["switch:" + String(id)].to<JsonObject>(), id);
}

// HTTP and WebSocket requests use the same dispatcher. No unchecked toInt():
// malformed/negative channel IDs must never turn into a command for relay 1.
void dispatch(JsonVariantConst req, JsonDocument& response) {
  response["id"] = req["id"];
  response["src"] = identity;
  if (req["src"].is<const char*>()) response["dst"] = req["src"];
  if (!req.is<JsonObjectConst>() || !req["method"].is<const char*>()) {
    fail(response, -32600, "Invalid request"); return;
  }
  String method = req["method"].as<String>();
  auto result = response["result"].to<JsonObject>();
  auto params = req["params"];
  if (method == "Shelly.GetDeviceInfo") { info(result); return; }
  if (method == "Shelly.GetStatus") { status(result, relayGetMask()); return; }
  if (method == "Shelly.GetConfig") { config(result); return; }
  if (method == "Shelly.ListMethods") {
    auto a = result["methods"].to<JsonArray>();
    for (auto m : {"Shelly.GetDeviceInfo", "Shelly.GetStatus", "Shelly.GetConfig",
                   "Shelly.GetComponents", "Shelly.ListMethods", "Switch.GetStatus",
                   "Switch.GetConfig", "Switch.SetConfig", "Switch.Set", "Switch.Toggle"}) a.add(m);
    return;
  }
  if (method == "Shelly.GetComponents") {
    int offset = 0;
    if (!params["offset"].isNull()) {
      if (!params["offset"].is<int>() || params["offset"].as<int>() < 0) {
        fail(response, -32602, "Invalid offset"); return;
      }
      offset = params["offset"].as<int>();
    }
    if (!params["dynamic_only"].isNull() && !params["dynamic_only"].is<bool>()) {
      fail(response, -32602, "Invalid dynamic_only"); return;
    }
    bool dynamic = params["dynamic_only"] | false;
    result["cfg_rev"] = configRevision;
    result["offset"] = offset;
    result["total"] = dynamic ? 0 : COUNT;
    auto list = result["components"].to<JsonArray>();
    // Only fixed hardware components. Dynamic component queries must be empty.
    uint8_t mask = relayGetMask();
    for (int i = offset; !dynamic && i < COUNT && i < offset + 4; ++i) {
      auto item = list.add<JsonObject>();
      item["key"] = "switch:" + String(i);
      switchConfig(item["config"].to<JsonObject>(), i);
      switchStatus(item["status"].to<JsonObject>(), i, mask);
    }
    return;
  }
  if (!method.startsWith("Switch.")) { fail(response, -32601, "Method not found"); return; }
  if (!params["id"].is<int>() || params["id"].as<int>() < 0 || params["id"].as<int>() >= COUNT) {
    fail(response, -32602, "Invalid switch id (use 0-7)"); return;
  }
  uint8_t id = params["id"].as<int>();
  if (method == "Switch.GetStatus") { switchStatus(result, id, relayGetMask()); return; }
  if (method == "Switch.GetConfig") { switchConfig(result, id); return; }
  if (method == "Switch.SetConfig") {
    auto cfg = params["config"];
    if (!cfg.is<JsonObjectConst>() || (!cfg["name"].is<const char*>() && !cfg["name"].isNull()) || cfg["name"].as<String>().length() > 64) {
      fail(response, -32602, "Only a name up to 64 bytes is supported"); return;
    }
    for (JsonPairConst pair : cfg.as<JsonObjectConst>()) {
      if (String(pair.key().c_str()) != "name") { fail(response, -32602, "Unsupported configuration key"); return; }
    }
    if (!cfg.as<JsonObjectConst>().containsKey("name")) { fail(response, -32602, "Missing name"); return; }
    String name = cfg["name"].isNull() ? String("") : cfg["name"].as<String>();
    Preferences p;
    if (!p.begin("shelly", false)) { fail(response, -32603, "NVS unavailable"); return; }
    size_t written = p.putString(("name" + String(id)).c_str(), name);
    p.end();
    if (written == 0 && name.length() != 0) { fail(response, -32603, "Cannot save switch name"); return; }
    names[id] = name;
    ++configRevision;
    lastFull = millis() - 30000;
    result["restart_required"] = false;
    return;
  }
  if (method != "Switch.Set" && method != "Switch.Toggle") { fail(response, -32601, "Method not found"); return; }
  if (!params["toggle_after"].isNull()) { fail(response, -32602, "Timed switching is not supported"); return; }
  if (method == "Switch.Set" && !params["on"].is<bool>()) { fail(response, -32602, "on must be boolean"); return; }
  bool wasOn = relaySetAndGetPrevious(id, params["on"] | false, method == "Switch.Toggle");
  result["was_on"] = wasOn;
}

String rpcFrame(const char* data, size_t length) {
  JsonDocument req, response;
  response["id"] = nullptr;
  response["src"] = identity;
  if (length > MAX_RPC) { fail(response, -32600, "Request too large"); }
  else if (deserializeJson(req, data, length)) { fail(response, -32700, "Parse error"); }
  else dispatch(req.as<JsonVariantConst>(), response);
  String out; serializeJson(response, out); return out;
}
void onWs(uint8_t n, WStype_t type, uint8_t* data, size_t length) {
  if (type == WStype_DISCONNECTED || type == WStype_CONNECTED) { fragments[n] = ""; return; }
  if (type == WStype_TEXT) { String out = rpcFrame((const char*)data, length); ws.sendTXT(n, out); return; }
  if (type == WStype_FRAGMENT_TEXT_START) fragments[n] = "";
  if (type == WStype_FRAGMENT_TEXT_START || type == WStype_FRAGMENT || type == WStype_FRAGMENT_FIN) {
    if (fragments[n].length() + length > MAX_RPC) { fragments[n] = ""; ws.disconnect(n); return; }
    fragments[n].concat((const char*)data, length);
    if (type == WStype_FRAGMENT_FIN) {
      String out = rpcFrame(fragments[n].c_str(), fragments[n].length());
      fragments[n] = ""; ws.sendTXT(n, out);
    }
  }
}
void httpRpc() {
  if (!enabled) { server.send(503, "application/json", "{\"error\":\"Shelly disabled\"}"); return; }
  if (server.method() == HTTP_GET && server.header("Upgrade").equalsIgnoreCase("websocket")) {
    String conn = server.header("Connection"); conn.toLowerCase();
    if (server.uri() != "/rpc" || conn.indexOf("upgrade") < 0 ||
        server.header("Sec-WebSocket-Version") != "13" || server.header("Sec-WebSocket-Key").length() != 24) {
      server.send(400, "text/plain", "Invalid WebSocket upgrade"); return;
    }
    if (!ws.upgrade()) server.send(503, "text/plain", "WebSocket clients full");
    return;
  }
  if (server.uri() == "/rpc" && server.method() == HTTP_POST) {
    String body = server.arg("plain");
    server.send(200, "application/json", rpcFrame(body.c_str(), body.length())); return;
  }
  JsonDocument req, res;
  if (server.uri() == "/shelly") { info(res.to<JsonObject>()); }
  else {
    req["method"] = server.uri().substring(5);
    auto params = req["params"].to<JsonObject>();
    for (int i = 0; i < server.args(); ++i) {
      JsonDocument value;
      String text = server.arg(i);
      if (deserializeJson(value, text)) { params[server.argName(i)] = text; }
      else params[server.argName(i)] = value.as<JsonVariantConst>();
    }
    dispatch(req.as<JsonVariantConst>(), res);
    // HTTP /rpc/Method returns the result directly rather than an RPC envelope.
    if (!res["error"].isNull()) { String out; serializeJson(res, out); server.send(400, "application/json", out); return; }
    String out; serializeJson(res["result"], out); server.send(200, "application/json", out); return;
  }
  String out; serializeJson(res, out); server.send(200, "application/json", out);
}
} // namespace

const String& shellyDeviceId() { return identity; }
bool shellyEnabled() { return enabled; }
void shellySetEnabled(bool value) {
  Preferences p; p.begin("shelly", false); p.putBool("enabled", value); p.end();
  // Applied on reboot, alongside the existing configuration form.
}
void shellyReset() {
  Preferences p; p.begin("shelly", false); p.clear(); p.end();
}
void shellyRegisterHttpRoutes() {
  const char* headers[] = {"Upgrade", "Connection", "Sec-WebSocket-Key", "Sec-WebSocket-Version"};
  server.collectHeaders(headers, 4);
  server.on("/rpc", HTTP_GET, httpRpc);
  server.on("/rpc", HTTP_POST, httpRpc);
  server.on("/shelly", HTTP_GET, httpRpc);
  for (auto m : {"Shelly.GetDeviceInfo", "Shelly.GetStatus", "Shelly.GetConfig", "Shelly.GetComponents",
                 "Shelly.ListMethods", "Switch.GetStatus", "Switch.GetConfig", "Switch.SetConfig", "Switch.Set", "Switch.Toggle"})
    server.on(String("/rpc/") + m, HTTP_GET, httpRpc);
}
void shellyNetworkChanged() {
  // The Shelly driver derives the serial from the advertised host name.
  // Use the MAC-based Shelly hostname; the configured name remains the UI name.
  if (mdnsStarted) MDNS.end();
  mdnsStarted = MDNS.begin(enabled ? identity.c_str() : g_cfg.hostname.c_str());
  if (!mdnsStarted) { LOG("[Shelly] mDNS start failed"); return; }
  MDNS.enableArduino(3232, false);
  if (enabled) {
    MDNS.addService("shelly", "tcp", 80);
    MDNS.setInstanceName(identity.c_str());
    MDNS.addServiceTxt("shelly", "tcp", "gen", "2");
    MDNS.addServiceTxt("shelly", "tcp", "id", identity.c_str());
    MDNS.addServiceTxt("shelly", "tcp", "app", "Waveshare8RO");
    MDNS.addServiceTxt("shelly", "tcp", "ver", "1.4.0");
    MDNS.addServiceTxt("shelly", "tcp", "auth_en", "false");
  }
  LOGf("[Shelly] %s (%s)", identity.c_str(), enabled ? "enabled" : "disabled");
}
void shellyBegin() {
  uint8_t mac[6]; esp_efuse_mac_get_default(mac);
  char text[13];
  snprintf(text, sizeof(text), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  macAddress = text;
  identity = "shelly-waveshare-" + macAddress; identity.toLowerCase();
  Preferences p; p.begin("shelly", true);
  enabled = p.getBool("enabled", true);
  for (uint8_t i = 0; i < COUNT; ++i) names[i] = p.getString(("name" + String(i)).c_str(), "Relay " + String(i + 1));
  p.end();
  ws.begin(); ws.onEvent(onWs);
  lastMask = relayGetMask();
  shellyNetworkChanged();
}
void shellyLoop() {
  if (!enabled) return;
  ws.loop();
  // Mask snapshots coalesce changes from MQTT/SK tasks without touching sockets
  // on core 0; periodic full status also refreshes sys uptime/config revision.
  uint8_t mask = relayGetMask();
  bool full = millis() - lastFull >= 30000;
  if (mask == lastMask && !full) return;
  uint8_t changed = mask ^ lastMask; lastMask = mask;
  if (full) lastFull = millis();
  JsonDocument doc;
  doc["src"] = identity;
  doc["method"] = full ? "NotifyFullStatus" : "NotifyStatus";
  auto params = doc["params"].to<JsonObject>();
  params["ts"] = (double)time(nullptr);
  if (full) status(params, mask);
  else for (uint8_t i = 0; i < COUNT; ++i)
    if (changed & (1u << i)) switchStatus(params["switch:" + String(i)].to<JsonObject>(), i, mask);
  String out; serializeJson(doc, out); ws.broadcastTXT(out);
}
