#include "RelayControl.h"
#include "BoardPins.h"
#include "Logger.h"
#include "RgbLed_WS2812.h"
#include <Wire.h>
#include <TCA9554.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern RgbLed rgb;

// Forward declarations — MqttManager and SignalKClient publish on state change
void mqttPublishRelayState(uint8_t idx);
void mqttPublishAllRelayStates();
void skPublishRelayState(uint8_t idx);
void skPublishAllRelayStates();
extern bool g_mqtt_connected;
extern bool skIsConnected();

static TCA9554 s_tca(BoardPins::TCA9554_ADDR);
static uint8_t s_mask = 0;
static SemaphoreHandle_t s_mutex = nullptr;

void relayInit() {
  s_mutex = xSemaphoreCreateMutex();
  configASSERT(s_mutex);
  Wire.begin(BoardPins::I2C_SDA, BoardPins::I2C_SCL);
  if (!s_tca.isConnected()) {
    LOG("[ERR] TCA9554 not found — halting");
    while (true) delay(1000);
  }
  s_tca.write8(0x00);
  s_tca.pinMode8(0x00);
  s_mask = 0;
  rgb.setForMask(s_mask);
  LOG("[RELAY] TCA9554 ready");
}

void relaySet(uint8_t idx, bool on) { relaySetAndGetPrevious(idx, on); }

bool relaySetAndGetPrevious(uint8_t idx, bool on, bool toggle) {
  if (idx >= BoardPins::RELAY_COUNT) return false;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  bool previous = (s_mask >> idx) & 1u;
  if (toggle) on = !previous;
  s_tca.write1(idx, on ? HIGH : LOW);
  if (on) s_mask |= (1u << idx);
  else    s_mask &= ~(1u << idx);
  uint8_t current = s_mask;
  xSemaphoreGive(s_mutex);
  rgb.setRelayMask(current);   // triggers white activity pulse
  LOGf("[RELAY] R%d %s", idx + 1, on ? "ON" : "OFF");
  if (g_mqtt_connected) mqttPublishRelayState(idx);
  if (skIsConnected())  skPublishRelayState(idx);
  return previous;
}

void relayApplyMask(uint8_t mask) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  mask &= (uint8_t)((1u << BoardPins::RELAY_COUNT) - 1u);
  uint8_t diff = s_mask ^ mask;
  for (uint8_t i = 0; i < BoardPins::RELAY_COUNT; ++i) {
    bool on = (mask >> i) & 1;
    s_tca.write1(i, on ? HIGH : LOW);
    if (diff & (1u << i)) LOGf("[RELAY] R%d %s", i + 1, on ? "ON" : "OFF");
  }
  s_mask = mask;
  uint8_t current = s_mask;
  xSemaphoreGive(s_mutex);
  rgb.setRelayMask(current);   // triggers white activity pulse
  if (g_mqtt_connected) mqttPublishAllRelayStates();
  if (skIsConnected())  skPublishAllRelayStates();
}

uint8_t relayGetMask() {
  if (!s_mutex) return 0;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  uint8_t mask = s_mask;
  xSemaphoreGive(s_mutex);
  return mask;
}
