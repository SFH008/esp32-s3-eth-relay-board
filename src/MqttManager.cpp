#include "MqttManager.h"
#include "Config.h"
#include "Logger.h"
#include "BoardPins.h"
#include "RelayControl.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ── Globals ───────────────────────────────────────────────────────────────────
bool g_mqtt_connected = false;

static WiFiClient       s_client;
static PubSubClient     s_mqtt;
static TaskHandle_t     s_task       = nullptr;
static SemaphoreHandle_t s_mutex     = nullptr;  // guards s_mqtt calls
static uint32_t         s_lastAttempt = 0;
static uint32_t         s_lastStatePub = 0;

static constexpr uint32_t RECONNECT_INTERVAL_MS = 5000;
static constexpr uint32_t STATE_PUB_INTERVAL_MS = 5000;

// ── Forward declarations ──────────────────────────────────────────────────────
static void mqttCallback(char* topic, byte* payload, unsigned int length);
static void mqttReconnect();

// ── Digital input helper (defined in main.cpp) ────────────────────────────────
extern uint8_t readDI_mask();

// ── Callback ─────────────────────────────────────────────────────────────────
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Parse payload safely with ArduinoJson
  JsonDocument doc;
  // Attempt JSON parse; fall back to plain string
  String plain = "";
  bool isJson = false;
  if (length > 0 && payload[0] == '{') {
    DeserializationError err = deserializeJson(doc, payload, length);
    isJson = (err == DeserializationError::Ok);
  }
  if (!isJson) {
    plain.reserve(length);
    for (unsigned int i = 0; i < length; i++) plain += (char)payload[i];
    plain.toUpperCase();
    plain.trim();
  }

  LOGf("[MQTT] [%s]: %s", topic, isJson ? "(json)" : plain.c_str());

  const String base = g_cfg.mqttBaseTopic + "/relay/";
  String t = String(topic);
  if (!t.startsWith(base)) return;
  String rem = t.substring(base.length());

  if (rem.startsWith("all/set")) {
    uint8_t newMask = 0;
    if (isJson) {
      newMask = doc["mask"] | 0;
    } else {
      if (plain == "CLEAR" || plain == "OFF" || plain == "ALLOFF") { relayApplyMask(0x00); return; }
      if (plain == "ALLON")                                          { relayApplyMask(0xFF); return; }
      if (plain.startsWith("0X")) newMask = (uint8_t)strtol(plain.c_str(), nullptr, 16);
      else                        newMask = (uint8_t)plain.toInt();
    }
    relayApplyMask(newMask);
  } else {
    int sl = rem.indexOf('/');
    if (sl > 0) {
      uint8_t rn = (uint8_t)rem.substring(0, sl).toInt();
      if (rn >= 1 && rn <= BoardPins::RELAY_COUNT) {
        bool on = isJson ? (bool)doc["state"]
                         : (plain == "ON" || plain == "1" || plain == "TRUE");
        relaySet(rn - 1, on);
      }
    }
  }
}

// ── Reconnect (called from task) ──────────────────────────────────────────────
static void mqttReconnect() {
  if (millis() - s_lastAttempt < RECONNECT_INTERVAL_MS) return;
  s_lastAttempt = millis();

  LOGf("[MQTT] Connecting to %s:%u...", g_cfg.mqttBroker.c_str(), g_cfg.mqttPort);

  bool ok = (g_cfg.mqttUser.length() > 0)
    ? s_mqtt.connect(g_cfg.mqttClientId.c_str(), g_cfg.mqttUser.c_str(), g_cfg.mqttPass.c_str())
    : s_mqtt.connect(g_cfg.mqttClientId.c_str());

  if (ok) {
    g_mqtt_connected = true;
    LOG("[MQTT] Connected");
    for (uint8_t i = 1; i <= BoardPins::RELAY_COUNT; i++) {
      String sub = g_cfg.mqttBaseTopic + "/relay/" + String(i) + "/set";
      s_mqtt.subscribe(sub.c_str());
    }
    s_mqtt.subscribe((g_cfg.mqttBaseTopic + "/relay/all/set").c_str());
    mqttPublishAllRelayStates();
    mqttPublishAllInputStates();
  } else {
    g_mqtt_connected = false;
    LOGf("[MQTT] Failed, rc=%d", s_mqtt.state());
  }
}

// ── FreeRTOS task ─────────────────────────────────────────────────────────────
static void mqttTask(void*) {
  for (;;) {
    // Only take the mutex to check connection state — never hold it across
    // a blocking call. mqttReconnect() can block for up to
    // MQTT_SOCKET_TIMEOUT seconds inside connect(); if we held s_mutex
    // during that, any HTTP handler calling mqttPublishRelayState() would
    // stall for the same duration waiting on the mutex.
    xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
    bool connected = s_mqtt.connected();
    xSemaphoreGiveRecursive(s_mutex);

    if (!connected) {
      g_mqtt_connected = false;
      mqttReconnect();  // blocks without holding s_mutex
    } else {
      xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
      g_mqtt_connected = true;
      s_mqtt.loop();
      if (millis() - s_lastStatePub > STATE_PUB_INTERVAL_MS) {
        s_lastStatePub = millis();
        uint8_t mask = relayGetMask();
        for (uint8_t i = 0; i < BoardPins::RELAY_COUNT; i++) {
          String topic = g_cfg.mqttBaseTopic + "/relay/" + String(i + 1) + "/state";
          s_mqtt.publish(topic.c_str(), (mask >> i) & 1 ? "ON" : "OFF", true);
        }
      }
      xSemaphoreGiveRecursive(s_mutex);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}


// ── Public API ────────────────────────────────────────────────────────────────
void mqttStart() {
  IPAddress brokerIP;
  if (!brokerIP.fromString(g_cfg.mqttBroker)) {
    LOGf("[MQTT] Invalid broker IP: %s", g_cfg.mqttBroker.c_str());
    return;
  }

  // Recursive mutex — task loop holds it while calling publish helpers
  // which also take the mutex; non-recursive would deadlock.
  s_mutex = xSemaphoreCreateRecursiveMutex();
  s_mqtt.setClient(s_client);
  s_mqtt.setServer(brokerIP, g_cfg.mqttPort);
  s_mqtt.setCallback(mqttCallback);
  s_mqtt.setKeepAlive(60);
  s_mqtt.setSocketTimeout(10);

  // Pin MQTT task to core 0; main loop runs on core 1
  xTaskCreatePinnedToCore(
    mqttTask, "mqtt",
    /*stack*/ 4096,
    nullptr,
    /*priority*/ 1,
    &s_task,
    /*core*/ 0
  );
  LOGf("[MQTT] Task started — broker %s:%u", g_cfg.mqttBroker.c_str(), g_cfg.mqttPort);
}

void mqttStop() {
  if (s_task) { vTaskDelete(s_task); s_task = nullptr; }
  if (s_mqtt.connected()) s_mqtt.disconnect();
  g_mqtt_connected = false;
  LOG("[MQTT] Stopped");
}

bool mqttIsConnected() { return g_mqtt_connected; }

// ── Publish helpers (mutex-guarded, safe from any task) ──────────────────────
void mqttPublishRelayState(uint8_t idx) {
  if (!s_mqtt.connected()) return;
  String topic = g_cfg.mqttBaseTopic + "/relay/" + String(idx + 1) + "/state";
  String payload = (relayGetMask() >> idx) & 1 ? "ON" : "OFF";
  xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
  s_mqtt.publish(topic.c_str(), payload.c_str(), /*retain=*/true);
  xSemaphoreGiveRecursive(s_mutex);
}

void mqttPublishAllRelayStates() {
  if (!s_mqtt.connected()) return;
  uint8_t mask = relayGetMask();
  xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
  for (uint8_t i = 0; i < BoardPins::RELAY_COUNT; i++) {
    String topic = g_cfg.mqttBaseTopic + "/relay/" + String(i + 1) + "/state";
    s_mqtt.publish(topic.c_str(), (mask >> i) & 1 ? "ON" : "OFF", true);
  }
  // Publish aggregate JSON
  JsonDocument doc;
  doc["mask"] = mask;
  String binary = "0b";
  for (int i = 7; i >= 0; i--) binary += (char)('0' + ((mask >> i) & 1));
  doc["binary"] = binary;
  char hexbuf[8]; snprintf(hexbuf, sizeof(hexbuf), "0x%02X", mask);
  doc["hex"] = hexbuf;
  String payload; serializeJson(doc, payload);
  s_mqtt.publish((g_cfg.mqttBaseTopic + "/relay/all/state").c_str(), payload.c_str(), true);
  xSemaphoreGiveRecursive(s_mutex);
}

void mqttPublishInputState(uint8_t idx, uint8_t di_mask) {
  if (!s_mqtt.connected()) return;
  String topic = g_cfg.mqttBaseTopic + "/input/" + String(idx + 1) + "/state";
  String payload = (di_mask >> idx) & 1 ? "ON" : "OFF";
  xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
  s_mqtt.publish(topic.c_str(), payload.c_str(), true);
  xSemaphoreGiveRecursive(s_mutex);
}

void mqttPublishAllInputStates() {
  if (!s_mqtt.connected()) return;
  uint8_t di = readDI_mask();
  xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
  for (uint8_t i = 0; i < BoardPins::DI_COUNT; i++) {
    String topic = g_cfg.mqttBaseTopic + "/input/" + String(i + 1) + "/state";
    s_mqtt.publish(topic.c_str(), (di >> i) & 1 ? "ON" : "OFF", true);
  }
  JsonDocument doc;
  doc["mask"] = di;
  String binary = "0b";
  for (int i = 7; i >= 0; i--) binary += (char)('0' + ((di >> i) & 1));
  doc["binary"] = binary;
  char hexbuf[8]; snprintf(hexbuf, sizeof(hexbuf), "0x%02X", di);
  doc["hex"] = hexbuf;
  String payload; serializeJson(doc, payload);
  s_mqtt.publish((g_cfg.mqttBaseTopic + "/input/all/state").c_str(), payload.c_str(), true);
  xSemaphoreGiveRecursive(s_mutex);
}