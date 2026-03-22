#pragma once
#include <Arduino.h>

/*
  RgbLed — single WS2812 status LED driver (FastLED-backed).

  Colour scheme:
    MODE_NONE (0) — off
    MODE_AP   (1) — red       slow heartbeat
    MODE_WIFI (2) — green     slow heartbeat
    MODE_ETH  (3) — blue      slow heartbeat

  On relay activity: brief white pulse overlaid on the base colour, then
  fades back. Called via setRelayMask() whenever relays change.

  tick() must be called every loop() iteration to drive animations.
*/

class RgbLed {
public:
  // Initialise FastLED — call once in setup() after Serial is ready
  void begin();

  // Enable/disable the 1 Hz heartbeat fade (default: disabled until begin())
  void setHeartbeatEnabled(bool enabled);

  // Set base colour from network mode (0=off, 1=AP/red, 2=WiFi/green, 3=ETH/blue)
  // Kept for backward compatibility with existing call sites
  void setForMask(uint8_t modeOrMask);

  // Preferred API — set base colour from network mode explicitly
  void setNetMode(uint8_t mode);

  // Trigger a brief white activity pulse (call when any relay changes)
  void setRelayMask(uint8_t mask);

  // Drive heartbeat + activity fade — call every loop()
  void tick();

private:
  uint8_t  _mode         = 0;
  uint8_t  _relayMask    = 0;
  bool     _heartbeat    = false;

  // Activity pulse state
  uint8_t  _pulseLevel   = 0;     // 0 = idle, 255 = peak
  bool     _pulseFading  = false;

  // Heartbeat state
  uint8_t  _hbBrightness = 0;
  int8_t   _hbDir        = 1;
  uint32_t _lastTick     = 0;

  void     _show();
};
