#include "RelayControl.h"
#include "BoardPins.h"
#include "Logger.h"
#include "RgbLed_WS2812.h"
#include <Wire.h>
#include <TCA9554.h>

extern RgbLed rgb;

// Forward declarations — publish on state change
void mqttPublishRelayState(uint8_t idx);
void mqttPublishAllRelayStates();
void skPublishRelayState(uint8_t idx);
void skPublishAllRelayStates();
void n2kRequestPublish();          // N2kManager.cpp — safe to call from any task

extern bool g_mqtt_connected;
extern bool skIsConnected();

static TCA9554 s_tca(BoardPins::TCA9554_ADDR);
static uint8_t s_mask = 0;

void relayInit() {
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

void relaySet(uint8_t idx, bool on) {
  if (idx >= BoardPins::RELAY_COUNT) return;
  s_tca.write1(idx, on ? HIGH : LOW);
  if (on) s_mask |= (1u << idx);
  else    s_mask &= ~(1u << idx);
  rgb.setRelayMask(s_mask);
  LOGf("[RELAY] R%d %s", idx + 1, on ? "ON" : "OFF");
  if (g_mqtt_connected) mqttPublishRelayState(idx);
  if (skIsConnected())  skPublishRelayState(idx);
  n2kRequestPublish();
}

void relayApplyMask(uint8_t mask) {
  mask &= (uint8_t)((1u << BoardPins::RELAY_COUNT) - 1u);
  uint8_t diff = s_mask ^ mask;
  for (uint8_t i = 0; i < BoardPins::RELAY_COUNT; ++i) {
    bool on = (mask >> i) & 1;
    s_tca.write1(i, on ? HIGH : LOW);
    if (diff & (1u << i)) LOGf("[RELAY] R%d %s", i + 1, on ? "ON" : "OFF");
  }
  s_mask = mask;
  rgb.setRelayMask(s_mask);
  if (g_mqtt_connected) mqttPublishAllRelayStates();
  if (skIsConnected())  skPublishAllRelayStates();
  n2kRequestPublish();
}

uint8_t relayGetMask() { return s_mask; }
