/*
  SignalKClient.cpp

  Connects to a SignalK server over WebSocket (ws://) and:
    - Authenticates with a Bearer token stored in NVS (g_cfg.skToken)
    - Publishes relay and digital input states as SignalK delta messages
    - Handles inbound "put" requests to toggle relays
    - Runs entirely on core 0 alongside the MQTT task
    - Exposes a simple publish API safe to call from any task

  State machine:
    DISCONNECTED → CONNECTING → AUTHENTICATING → RUNNING → DISCONNECTED

  SignalK paths used:
    electrical.switches.relay1.state  … relay8.state   (bool, relays)
    electrical.switches.input1.state  … input8.state   (bool, DI)

  Put request format (received from SignalK server):
    {
      "requestId": "...",
      "put": { "path": "electrical.switches.relay3.state", "value": true }
    }

  Delta publish format:
    {
      "context": "vessels.self",
      "updates": [{
        "source": { "label": "<skVesselName>", "type": "sensor" },
        "values": [
          { "path": "electrical.switches.relay1.state", "value": true }
        ]
      }]
    }
*/

#include "SignalKClient.h"
#include "Config.h"
#include "Logger.h"
#include "BoardPins.h"
#include "RelayControl.h"

#include <WebSocketsClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// ── Internal types ────────────────────────────────────────────────────────────

// Messages enqueued by the publish API and drained by the SK task
struct SkMsg {
  enum Type : uint8_t {
    PUB_RELAY_ONE,   // data = relay index (0-7)
    PUB_RELAY_ALL,
    PUB_INPUT_ONE,   // data = (di_mask << 8) | idx
    PUB_INPUT_ALL,
  } type;
  uint16_t data;
};

enum SkState : uint8_t {
  SK_DISCONNECTED,
  SK_CONNECTING,
  SK_AUTHENTICATING,
  SK_RUNNING,
};

// ── Module-private state ──────────────────────────────────────────────────────
static WebSocketsClient  s_ws;
static TaskHandle_t      s_task          = nullptr;
static QueueHandle_t     s_queue         = nullptr;
static SemaphoreHandle_t s_wsMutex       = nullptr;
static volatile SkState  s_state              = SK_DISCONNECTED;
static volatile bool     s_connected          = false;
static volatile bool     s_snapshotPending    = false;
static volatile bool     s_tokenRefreshNeeded = false;  // set when server rejects token

static constexpr uint32_t RECONNECT_INTERVAL_MS = 10000;
static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 30000;
static constexpr char     SK_PATH[]             = "/signalk/v1/stream?subscribe=none";

// ── Forward declarations ──────────────────────────────────────────────────────
static void      wsEventHandler(WStype_t type, uint8_t* payload, size_t length);
static void      sendDelta(String json);
static String    buildRelayDelta(uint8_t idxMask, uint8_t stateMask);
static String    buildInputDelta(uint8_t idxMask, uint8_t di_mask);
static void      handlePut(const JsonObject& put, const char* requestId);
static void      drainQueue();
static void      skTask(void*);
bool             skLoginFallback();  // fallback for servers using /auth/login

// External DI reader (defined in main.cpp)
extern uint8_t readDI_mask();

// ── WebSocket event handler ───────────────────────────────────────────────────
// IMPORTANT: this fires from inside s_ws.loop() — must be side-effect free.
// Never call sendTXT, vTaskDelay, or xSemaphoreTake from here.
// Only set state flags and enqueue SkMsg items; the task loop acts on them.
static void wsEventHandler(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {

    case WStype_DISCONNECTED:
      s_connected = false;
      s_state     = SK_DISCONNECTED;
      LOG("[SK] Disconnected");
      break;

  case WStype_CONNECTED:
    LOGf("[SK] WebSocket connected to %s", (char*)payload);
    s_state           = SK_RUNNING;
    s_connected       = true;
    s_snapshotPending = true;
    LOG("[SK] Running (authenticated via header)");
    break;

    case WStype_TEXT: {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, payload, length);
      if (err) { LOGf("[SK] JSON parse error: %s", err.c_str()); break; }

      if (s_state == SK_AUTHENTICATING) {
        if (!doc["statusCode"].is<int>()) {
          // Not an auth response (likely the server's initial "hello" envelope) — ignore and keep waiting
          LOGf("[SK] Non-auth message while authenticating (ignored): %.100s", (char*)payload);
          break;
        }
        int code = doc["statusCode"];
        if (code == 200) {
          s_state           = SK_RUNNING;
          s_connected       = true;
          s_snapshotPending = true;
          LOG("[SK] Authenticated — running");
        } else {
          LOGf("[SK] Auth rejected: %d %s", code,
              doc["message"].as<const char*>() ?: "");
          s_tokenRefreshNeeded = true;
          s_state              = SK_DISCONNECTED;
          s_connected          = false;
        }
        break;
      }

      // Running — handle put requests
      if (doc["put"].is<JsonObject>()) {
        const char* reqId = doc["requestId"] | "";
        handlePut(doc["put"], reqId);
      }

      if (doc["requestId"].is<const char*>() && doc["state"].is<const char*>()) {
        LOGf("[SK] Put response: %s", doc["state"].as<const char*>());
      }
      break;
    }

    case WStype_ERROR:
      LOG("[SK] WebSocket error");
      s_connected = false;
      s_state     = SK_DISCONNECTED;
      break;

    default:
      break;
  }
}

// ── Put request handler ───────────────────────────────────────────────────────
static void handlePut(const JsonObject& put, const char* requestId) {
  const char* path = put["path"];
  if (!path) return;

  String p = String(path);
  LOGf("[SK] Put: %s = %s", path, put["value"].as<String>().c_str());

  const String relayPrefix = "electrical.switches.bank.relays.";
  const String inputPrefix = "electrical.switches.bank.inputs.";

  bool handled = false;
  int  statusCode = 400;

  if (p.startsWith(relayPrefix) && p.endsWith(".state")) {
    String numStr = p.substring(relayPrefix.length(), p.length() - strlen(".state"));
    int rn = numStr.toInt();
    if (rn >= 1 && rn <= (int)BoardPins::RELAY_COUNT) {
      bool on = put["value"].as<bool>();
      relaySet((uint8_t)(rn - 1), on);
      handled = true;
      statusCode = 200;
    }
  } else if (p.startsWith(inputPrefix)) {
    LOG("[SK] Put ignored: digital inputs are read-only");
    statusCode = 405;  // method not allowed — read-only path
  }

  // Acknowledge back to the server with the matching requestId, per the
  // Signal K PUT protocol — without this, callers may treat the request
  // as timed out even though the relay switched.
  if (requestId && requestId[0] != '\0') {
    JsonDocument resp;
    resp["requestId"]  = requestId;
    resp["state"]      = handled ? "COMPLETED" : "COMPLETED";  // COMPLETED either way; statusCode conveys result
    resp["statusCode"] = statusCode;
    String out; serializeJson(resp, out);
    sendDelta(out);  // reuse existing WS send helper
  }
}

// ── Delta builders ────────────────────────────────────────────────────────────
// Paths use the SignalK schema for electrical switches:
//   electrical.switches.relay<N>.state  — relay outputs (bank 0, channels 1-8)
//   electrical.switches.input<N>.state  — digital inputs (bank 1, channels 1-8)
//
// Context is omitted — server assumes vessels.self when absent.
// Timestamp is included so the server doesn't have to fill it in.

static String isoTimestamp() {
  time_t now = time(nullptr);
  if (now < 1700000000) {  // before ~Nov 2023 — SNTP hasn't synced yet, fall back
    uint32_t ms = millis();
    uint32_t s  = ms / 1000;
    char buf[28];
    snprintf(buf, sizeof(buf), "1970-01-01T%02u:%02u:%02u.%03uZ",
             (unsigned)((s / 3600) % 24),
             (unsigned)((s / 60)   % 60),
             (unsigned)(s % 60),
             (unsigned)(ms % 1000));
    return String(buf);
  }
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
  char full[40];
  snprintf(full, sizeof(full), "%s.%03uZ", buf, (unsigned)(millis() % 1000));
  return String(full);
}

static String buildRelayDelta(uint8_t idxMask, uint8_t stateMask) {
  JsonDocument doc;
  JsonArray updates = doc["updates"].to<JsonArray>();
  JsonObject update  = updates.add<JsonObject>();
  JsonObject source  = update["source"].to<JsonObject>();
  source["label"]    = g_cfg.skVesselName;
  source["type"]     = "sensor";
  update["timestamp"]= isoTimestamp();

  JsonArray values = update["values"].to<JsonArray>();
  for (uint8_t i = 0; i < BoardPins::RELAY_COUNT; ++i) {
    if (!((idxMask >> i) & 1)) continue;
    JsonObject v = values.add<JsonObject>();
    char path[64];
    // Schema path: electrical.switches.bank.<bankId>.<channelId>.state
    // Bank 0 = relay outputs, channels 1-8
    snprintf(path, sizeof(path),
             "electrical.switches.bank.relays.%u.state", (unsigned)(i + 1));
    v["path"]  = path;
    v["value"] = (bool)((stateMask >> i) & 1);
  }

  String out; serializeJson(doc, out);
  return out;
}

static String buildInputDelta(uint8_t idxMask, uint8_t di_mask) {
  JsonDocument doc;
  JsonArray updates  = doc["updates"].to<JsonArray>();
  JsonObject update  = updates.add<JsonObject>();
  JsonObject source  = update["source"].to<JsonObject>();
  source["label"]    = g_cfg.skVesselName;
  source["type"]     = "sensor";
  update["timestamp"]= isoTimestamp();

  JsonArray values = update["values"].to<JsonArray>();
  for (uint8_t i = 0; i < BoardPins::DI_COUNT; ++i) {
    if (!((idxMask >> i) & 1)) continue;
    JsonObject v = values.add<JsonObject>();
    char path[64];
    // Bank 1 = digital inputs, channels 1-8
    snprintf(path, sizeof(path),
             "electrical.switches.bank.inputs.%u.state", (unsigned)(i + 1));
    v["path"]  = path;
    v["value"] = (bool)((di_mask >> i) & 1);
  }

  String out; serializeJson(doc, out);
  return out;
}

// ── Send helper ───────────────────────────────────────────────────────────────
static void sendDelta(String json) {   // by value — sendTXT needs non-const ref
  if (!s_connected) return;
  xSemaphoreTake(s_wsMutex, portMAX_DELAY);
  s_ws.sendTXT(json);
  xSemaphoreGive(s_wsMutex);
}

// ── Queue drain (called from SK task) ────────────────────────────────────────
static void drainQueue() {
  SkMsg m;
  while (xQueueReceive(s_queue, &m, 0) == pdTRUE) {
    if (!s_connected) continue;  // drop if not ready
    switch (m.type) {
      case SkMsg::PUB_RELAY_ONE: {
        uint8_t idx = (uint8_t)m.data;
        sendDelta(buildRelayDelta(1u << idx, relayGetMask()));
        break;
      }
      case SkMsg::PUB_RELAY_ALL:
        sendDelta(buildRelayDelta(0xFF, relayGetMask()));
        break;
      case SkMsg::PUB_INPUT_ONE: {
        uint8_t idx     = (uint8_t)(m.data & 0xFF);
        uint8_t di_mask = (uint8_t)(m.data >> 8);
        sendDelta(buildInputDelta(1u << idx, di_mask));
        break;
      }
      case SkMsg::PUB_INPUT_ALL:
        sendDelta(buildInputDelta(0xFF, readDI_mask()));
        break;
    }
  }
}

// ── Device access request (HTTP — blocking, call from core 1 only) ───────────
//
// Implements the SignalK device access request spec:
//   POST /signalk/v1/access/requests  { clientId, description }
//   Poll returned href until COMPLETED
//   Extract token on APPROVED, set denied flag on DENIED
//
static volatile bool s_accessDenied = false;

bool skRequestAccess() {
  if (s_accessDenied) {
    LOG("[SK] Access previously denied — not retrying (call skClearDenied() to reset)");
    return false;
  }

  String base = "http://" + g_cfg.skServer + ":" + String(g_cfg.skPort);
  String reqUrl = base + "/signalk/v1/access/requests";

  LOGf("[SK] Submitting device access request to %s", reqUrl.c_str());
  LOGf("[SK] clientId: %s", g_cfg.skClientId.c_str());

  // ── Step 1: POST access request ──────────────────────────────────────────
  HTTPClient http;
  http.begin(reqUrl);
  http.addHeader("Content-Type", "application/json");

  JsonDocument req;
  req["clientId"]    = g_cfg.skClientId;
  req["description"] = "ESP32-S3 relay board (" + g_cfg.skVesselName + ")";
  String body; serializeJson(req, body);

  int code = http.POST(body);
  String resp = http.getString();
  http.end();

  if (code == 501) {
    LOG("[SK] Server does not support access requests (501)");
    return false;
  }
  if (code != 202) {
    LOGf("[SK] Access request rejected — HTTP %d: %s", code, resp.c_str());
    return false;
  }

  JsonDocument postDoc;
  if (deserializeJson(postDoc, resp) != DeserializationError::Ok) {
    LOG("[SK] Access request response — invalid JSON");
    return false;
  }

  const char* href = postDoc["href"];
  if (!href) {
    LOG("[SK] Access request response missing href");
    return false;
  }

  String pollUrl = base + String(href);
  LOGf("[SK] Access request PENDING — polling %s", pollUrl.c_str());
  LOG("[SK] Approve this device in the SignalK server admin UI");

  // ── Step 2: Poll until COMPLETED or timeout (120 s) ──────────────────────
  constexpr uint32_t POLL_INTERVAL_MS = 3000;
  constexpr uint32_t POLL_TIMEOUT_MS  = 120000;
  uint32_t started = millis();

  while (millis() - started < POLL_TIMEOUT_MS) {
    vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));

    HTTPClient poll;
    poll.begin(pollUrl);
    int pc = poll.GET();
    String pr = poll.getString();
    poll.end();

    if (pc != 200) {
      LOGf("[SK] Poll failed — HTTP %d", pc);
      continue;
    }

    JsonDocument pollDoc;
    if (deserializeJson(pollDoc, pr) != DeserializationError::Ok) continue;

    const char* state = pollDoc["state"];
    if (!state) continue;

    LOGf("[SK] Access request state: %s", state);

    if (strcmp(state, "PENDING") == 0) continue;  // keep polling

    if (strcmp(state, "COMPLETED") == 0) {
      int statusCode = pollDoc["statusCode"] | 0;
      const char* permission = pollDoc["accessRequest"]["permission"];

      if (permission && strcmp(permission, "APPROVED") == 0) {
        const char* token = pollDoc["accessRequest"]["token"];
        if (!token || strlen(token) == 0) {
          LOG("[SK] Approved but no token in response");
          return false;
        }
        g_cfg.skToken        = String(token);
        s_tokenRefreshNeeded = false;
        s_accessDenied       = false;
        configSave();
        LOGf("[SK] Access APPROVED — token saved (%u chars)", g_cfg.skToken.length());
        return true;

      } else if (permission && strcmp(permission, "DENIED") == 0) {
        LOG("[SK] Access DENIED by server administrator");
        s_accessDenied       = true;
        s_tokenRefreshNeeded = false;
        return false;

      } else {
        // statusCode 400 = error (e.g. duplicate clientId already has a token)
        const char* msg = pollDoc["message"];
        LOGf("[SK] Access request error %d: %s", statusCode, msg ? msg : "");
        // 400 with duplicate clientId usually means we already have a token
        // on the server — try /auth/login as fallback if credentials available
        if (statusCode == 400 && g_cfg.skUser.length() > 0) {
          LOGf("[SK] Attempting /auth/login fallback with user: %s", g_cfg.skUser.c_str());
          return skLoginFallback();
        }
        return false;
      }
    }
  }

  LOG("[SK] Access request timed out after 120 s — approve in SK admin UI and reboot");
  return false;
}

// Fallback: /auth/login for servers that use username/password (non-device flow)
bool skLoginFallback() {
  if (g_cfg.skUser.length() == 0 || g_cfg.skPass.length() == 0) return false;

  String url = "http://" + g_cfg.skServer + ":" + String(g_cfg.skPort)
               + "/signalk/v1/auth/login";
  LOGf("[SK] Login fallback: POST %s", url.c_str());

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  JsonDocument req;
  req["username"] = g_cfg.skUser;
  req["password"] = g_cfg.skPass;
  String body; serializeJson(req, body);

  int code = http.POST(body);
  String resp = http.getString();
  http.end();

  if (code != 200) { LOGf("[SK] Login failed — HTTP %d", code); return false; }

  JsonDocument doc;
  if (deserializeJson(doc, resp) != DeserializationError::Ok) return false;

  const char* token = doc["token"];
  if (!token || strlen(token) == 0) return false;

  g_cfg.skToken        = String(token);
  s_tokenRefreshNeeded = false;
  configSave();
  LOGf("[SK] Login token saved (%u chars)", g_cfg.skToken.length());
  return true;
}

void skClearDenied() {
  s_accessDenied       = false;
  s_tokenRefreshNeeded = false;
  g_cfg.skToken        = "";
  configSave();
  LOG("[SK] Denied flag cleared — will submit new access request");
}

bool skAccessDenied()        { return s_accessDenied; }
bool skTokenRefreshNeeded()  { return s_tokenRefreshNeeded; }

// ── FreeRTOS task ─────────────────────────────────────────────────────────────
static void skTask(void*) {
  uint32_t lastConnectAttempt = 0;
  uint32_t lastHeartbeat      = 0;
  bool     tokenSent          = false;  // guards one-shot token send per connect

  s_ws.onEvent(wsEventHandler);
  s_ws.setReconnectInterval(0);
  s_ws.disableHeartbeat();

  for (;;) {
    // s_tokenRefreshNeeded is set by wsEventHandler when the server rejects
    // our token. The actual HTTP refresh is handled by main.cpp loop() on
    // core 1 — never call skRequestToken() from inside this task because
    // HTTPClient allocates ~8KB internally and will fragment the heap while
    // the WS library also holds buffers.

    // Attempt connect when fully disconnected and interval elapsed
    if (s_state == SK_DISCONNECTED &&
        (millis() - lastConnectAttempt) >= RECONNECT_INTERVAL_MS) {
      lastConnectAttempt = millis();
      tokenSent          = false;
      LOGf("[SK] Connecting to ws://%s:%u%s",
           g_cfg.skServer.c_str(), g_cfg.skPort, SK_PATH);
      s_state = SK_CONNECTING;
      xSemaphoreTake(s_wsMutex, portMAX_DELAY);

    if (s_state == SK_DISCONNECTED &&
        (millis() - lastConnectAttempt) >= RECONNECT_INTERVAL_MS) {
      lastConnectAttempt = millis();
      tokenSent          = false;
      LOGf("[SK] Connecting to ws://%s:%u%s",
          g_cfg.skServer.c_str(), g_cfg.skPort, SK_PATH);
      s_state = SK_CONNECTING;

      xSemaphoreTake(s_wsMutex, portMAX_DELAY);
      if (g_cfg.skToken.length() > 0) {
        String authHeader = "Authorization: Bearer " + g_cfg.skToken;
        s_ws.setExtraHeaders(authHeader.c_str());
        LOGf("[SK] Connecting with auth header (%u char token)", g_cfg.skToken.length());
      } else {
        s_ws.setExtraHeaders("");  // no token — anonymous connect
      }
      s_ws.begin(g_cfg.skServer.c_str(), g_cfg.skPort, SK_PATH);
      xSemaphoreGive(s_wsMutex);
    }

      s_ws.begin(g_cfg.skServer.c_str(), g_cfg.skPort, SK_PATH);
      xSemaphoreGive(s_wsMutex);
    }

    // Drive WebSocket state machine
    xSemaphoreTake(s_wsMutex, portMAX_DELAY);
    s_ws.loop();
    xSemaphoreGive(s_wsMutex);

    // Send token AFTER s_ws.loop() returns — safe to call sendTXT here
    if (s_state == SK_AUTHENTICATING && !tokenSent && g_cfg.skToken.length() > 0) {
      tokenSent = true;
      JsonDocument doc;
      doc["token"] = g_cfg.skToken;
      String auth; serializeJson(doc, auth);
      sendDelta(auth);
      LOGf("[SK] Sent auth token (%u chars)", g_cfg.skToken.length());
    }

    // Send initial state snapshot after connect — 500 ms after loop() clears
    // to let the SignalK server fully register the new connection before
    // the first delta arrives.
    static uint32_t snapshotAfterMs = 0;
    if (s_snapshotPending && snapshotAfterMs == 0) {
      snapshotAfterMs = millis() + 500;
    }
    if (s_snapshotPending && snapshotAfterMs > 0 && millis() >= snapshotAfterMs) {
      s_snapshotPending = false;
      snapshotAfterMs   = 0;
      LOG("[SK] Sending initial state snapshot");
      String relayDelta = buildRelayDelta(0xFF, relayGetMask());
      String inputDelta = buildInputDelta(0xFF, readDI_mask());
      LOGf("[SK] Relay delta: %s", relayDelta.c_str());
      sendDelta(relayDelta);
      sendDelta(inputDelta);
    }
    // Reset snapshot timer if we disconnect before it fires
    if (s_state == SK_DISCONNECTED) {
      s_snapshotPending = false;
      snapshotAfterMs   = 0;
    }

    // Timeout: CONNECTING for > 5 s → force back to DISCONNECTED
    static uint32_t connectingStart = 0;
    if (s_state == SK_CONNECTING) {
      if (connectingStart == 0) connectingStart = millis();
      if (millis() - connectingStart > 5000) {
        LOG("[SK] Connection timeout — backing off");
        xSemaphoreTake(s_wsMutex, portMAX_DELAY);
        s_ws.disconnect();
        xSemaphoreGive(s_wsMutex);
        s_state         = SK_DISCONNECTED;
        connectingStart = 0;
      }
    } else {
      connectingStart = 0;
    }

    // Auth rejected — disconnect cleanly
    if (!s_connected && s_state == SK_DISCONNECTED && tokenSent) {
      xSemaphoreTake(s_wsMutex, portMAX_DELAY);
      s_ws.disconnect();
      xSemaphoreGive(s_wsMutex);
      tokenSent = false;
    }

    // Drain outbound publish queue — always outside the mutex
    drainQueue();

    // Periodic heartbeat
    if (s_connected && (millis() - lastHeartbeat) >= HEARTBEAT_INTERVAL_MS) {
      lastHeartbeat = millis();
      sendDelta(buildRelayDelta(0xFF, relayGetMask()));
      // Log stack high-water mark so we can tune the allocation
      UBaseType_t hwm = uxTaskGetStackHighWaterMark(nullptr);
      LOGf("[SK] Heartbeat — stack HWM: %u bytes free", (unsigned)(hwm * sizeof(StackType_t)));
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ── Public API ────────────────────────────────────────────────────────────────
void skStart() {
  s_queue   = xQueueCreate(32, sizeof(SkMsg));
  s_wsMutex = xSemaphoreCreateMutex();

  // Auto-request access on boot if no token stored and not previously denied
  if (g_cfg.skToken.length() == 0 && !s_accessDenied) {
    LOG("[SK] No token in NVS — submitting device access request");
    skRequestAccess();  // blocking — runs before task starts, safe here
  }

  xTaskCreatePinnedToCore(
    skTask, "signalk",
    /*stack*/ 8192,   // 8KB — HWM confirmed at 3244 bytes used
    nullptr,
    /*priority*/ 1,
    &s_task,
    /*core*/ 0
  );
  LOGf("[SK] Task started — target ws://%s:%u",
       g_cfg.skServer.c_str(), g_cfg.skPort);
}

void skStop() {
  if (s_task) { vTaskDelete(s_task); s_task = nullptr; }
  if (s_connected) s_ws.disconnect();
  s_connected = false;
  s_state     = SK_DISCONNECTED;
  LOG("[SK] Stopped");
}

bool skIsConnected() { return s_connected; }

// ── Publish API (enqueue from any task) ──────────────────────────────────────
void skPublishRelayState(uint8_t idx) {
  if (!s_queue) return;
  SkMsg m{ SkMsg::PUB_RELAY_ONE, idx };
  xQueueSend(s_queue, &m, 0);  // non-blocking; drop if queue full
}

void skPublishAllRelayStates() {
  if (!s_queue) return;
  SkMsg m{ SkMsg::PUB_RELAY_ALL, 0 };
  xQueueSend(s_queue, &m, 0);
}

void skPublishInputState(uint8_t idx, uint8_t di_mask) {
  if (!s_queue) return;
  SkMsg m{ SkMsg::PUB_INPUT_ONE, (uint16_t)((di_mask << 8) | idx) };
  xQueueSend(s_queue, &m, 0);
}

void skPublishAllInputStates() {
  if (!s_queue) return;
  SkMsg m{ SkMsg::PUB_INPUT_ALL, 0 };
  xQueueSend(s_queue, &m, 0);
}