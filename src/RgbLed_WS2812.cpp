#include "RgbLed_WS2812.h"
#include "BoardPins.h"
#include <FastLED.h>

// ── FastLED setup ─────────────────────────────────────────────────────────────
static CRGB s_leds[1];

static constexpr uint8_t  MAX_BRIGHTNESS   = 80;   // 0-255; keep gentle indoors
static constexpr uint8_t  PULSE_PEAK       = 255;  // white pulse brightness
static constexpr uint8_t  PULSE_DECAY      = 12;   // subtracted per tick (~80 ms fade)
static constexpr uint8_t  HB_STEP         = 3;    // heartbeat fade step per tick
static constexpr uint32_t TICK_INTERVAL_MS = 20;  // ~50 fps

// Base colours per network mode
static CRGB modeColour(uint8_t mode) {
  switch (mode) {
    case 1:  return CRGB(MAX_BRIGHTNESS, 0, 0);               // AP  — red
    case 2:  return CRGB(0, MAX_BRIGHTNESS, 0);               // WiFi — green
    case 3:  return CRGB(0, 0, MAX_BRIGHTNESS);               // ETH  — blue
    default: return CRGB::Black;                               // none — off
  }
}

// ── Public API ────────────────────────────────────────────────────────────────

void RgbLed::begin() {
  FastLED.addLeds<WS2812, BoardPins::RGB_LED, GRB>(s_leds, 1);
  FastLED.setBrightness(MAX_BRIGHTNESS);
  FastLED.clear(true);
}

void RgbLed::setHeartbeatEnabled(bool enabled) {
  _heartbeat = enabled;
  if (!enabled) {
    _hbBrightness = MAX_BRIGHTNESS;
    _hbDir = 1;
  }
}

// Backward-compatible: values 0-3 treated as net mode, anything higher ignored
// (relay activity is triggered separately via setRelayMask)
void RgbLed::setForMask(uint8_t modeOrMask) {
  if (modeOrMask <= 3) {
    setNetMode(modeOrMask);
  }
  // values > 3 (relay bitmasks) are silently ignored here —
  // callers that want relay activity should call setRelayMask() instead
}

void RgbLed::setNetMode(uint8_t mode) {
  _mode = mode;
  _show();
}

void RgbLed::setRelayMask(uint8_t mask) {
  bool changed = (mask != _relayMask);
  _relayMask = mask;
  if (changed) {
    // Trigger activity pulse
    _pulseLevel  = PULSE_PEAK;
    _pulseFading = true;
  }
}

void RgbLed::tick() {
  uint32_t now = millis();
  if (now - _lastTick < TICK_INTERVAL_MS) return;
  _lastTick = now;

  // Decay activity pulse
  if (_pulseFading) {
    if (_pulseLevel > PULSE_DECAY) {
      _pulseLevel -= PULSE_DECAY;
    } else {
      _pulseLevel  = 0;
      _pulseFading = false;
    }
  }

  // Step heartbeat
  if (_heartbeat) {
    _hbBrightness = (uint8_t)constrain((int)_hbBrightness + _hbDir * HB_STEP, 10, MAX_BRIGHTNESS);
    if (_hbBrightness >= MAX_BRIGHTNESS) _hbDir = -1;
    if (_hbBrightness <= 10)             _hbDir =  1;
  } else {
    _hbBrightness = MAX_BRIGHTNESS;
  }

  _show();
}

void RgbLed::_show() {
  CRGB base = modeColour(_mode);

  // Scale base colour by heartbeat brightness
  uint8_t scale = _hbBrightness;
  base.r = scale8(base.r, scale);
  base.g = scale8(base.g, scale);
  base.b = scale8(base.b, scale);

  // Overlay white activity pulse
  if (_pulseLevel > 0) {
    uint8_t p = _pulseLevel;
    base.r = qadd8(base.r, p);
    base.g = qadd8(base.g, p);
    base.b = qadd8(base.b, p);
  }

  s_leds[0] = base;
  FastLED.show();
}
