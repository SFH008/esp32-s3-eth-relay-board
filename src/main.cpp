/*
  main.cpp — Waveshare ESP32-S3-ETH-8DI-8RO  (Arduino core 3.x / pioarduino)

  Module responsibilities:
    Logger.cpp        — UILogger ring buffer, ESP-IDF log bridge, LOG/LOGf macros
    Config.cpp        — NVS-backed runtime config (WiFi, MQTT, SignalK, hostname)
    RelayControl.cpp  — TCA9554 relay driver; notifies MQTT + SignalK on change
    MqttManager.cpp   — MQTT on FreeRTOS core 0 task
    SignalKClient.cpp — SignalK WebSocket delta client on FreeRTOS core 0 task
    HttpHandlers.cpp  — WebServer routes, ArduinoJson state/config builders
    main.cpp          — Network bringup, failover, DI polling (core 1)

  Network priority: Ethernet (W5500 SPI) → WiFi STA → WiFi AP fallback.
  MQTT used as fallback when SignalK WebSocket is unavailable.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <ETH.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <SPI.h>
#include <esp_task_wdt.h>

#include "Logger.h"
#include "Config.h"
#include "BoardPins.h"
#include "RelayControl.h"
#include "MqttManager.h"
#include "SignalKClient.h"
#include "HttpHandlers.h"
#include "RgbLed_WS2812.h"
#include "time.h"

// ── RGB LED ───────────────────────────────────────────────────────────────────
RgbLed rgb;

// ── Network mode ──────────────────────────────────────────────────────────────
enum NetMode : uint8_t { MODE_NONE, MODE_AP, MODE_WIFI, MODE_ETH };
uint8_t g_mode = MODE_NONE;

// ── Digital inputs ────────────────────────────────────────────────────────────
// Defined here; called from RelayControl, MqttManager, SignalKClient via extern
uint8_t readDI_mask() {
  uint8_t m = 0;
  for (uint8_t i = 0; i < BoardPins::DI_COUNT; ++i)
    if (digitalRead(BoardPins::DI_PINS[i]) == HIGH) m |= (1u << i);
  return m;
}

// ── ETH event handler ─────────────────────────────────────────────────────────
static void onEthEvent(arduino_event_id_t event, arduino_event_info_t) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:        LOG("[ETH] Started");   break;
    case ARDUINO_EVENT_ETH_CONNECTED:    LOG("[ETH] Link up");   break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      LOGf("[ETH] IP: %s", ETH.localIP().toString().c_str());    break;
    case ARDUINO_EVENT_ETH_DISCONNECTED: LOG("[ETH] Link down"); break;
    case ARDUINO_EVENT_ETH_STOP:         LOG("[ETH] Stopped");   break;
    default: break;
  }
}

// ── Network bringup ───────────────────────────────────────────────────────────

// Called ONCE in setup() — initialises SPI and ETH driver
static bool ethInit() {
  LOG("[ETH] Starting W5500...");
  SPI.begin(BoardPins::SPI_SCLK, BoardPins::SPI_MISO,
            BoardPins::SPI_MOSI, BoardPins::W5500_CS);

  if (!ETH.begin(ETH_PHY_W5500, 1,
                 BoardPins::W5500_CS,
                 BoardPins::W5500_INT,
                 BoardPins::W5500_RST,
                 SPI)) {
    LOG("[ETH] ETH.begin failed"); return false;
  }

  uint32_t t0 = millis();
  while (!ETH.linkUp() && (millis() - t0) < 5000) delay(50);
  if (!ETH.linkUp()) { LOG("[ETH] No link"); return false; }

  IPAddress ip, gw, sn, dns;
  ip.fromString(ETH_STATIC_IP); gw.fromString(ETH_GATEWAY);
  sn.fromString(ETH_SUBNET);    dns.fromString(ETH_DNS);
  ETH.config(ip, gw, sn, dns);

  t0 = millis();
  while (ETH.localIP() == IPAddress(0,0,0,0) && (millis() - t0) < 4000) delay(100);
  if (ETH.localIP() == IPAddress(0,0,0,0)) { LOG("[ETH] No IP"); return false; }

  LOGf("[ETH] Ready: %s", ETH.localIP().toString().c_str());
  return true;
}

// Called on failover BACK to ETH — does NOT re-init SPI or ETH driver,
// just waits for the link to recover and re-applies the static IP.
static bool ethReconnect() {
  LOG("[ETH] Waiting for link recovery...");
  uint32_t t0 = millis();
  while (!ETH.linkUp() && (millis() - t0) < 8000) delay(100);
  if (!ETH.linkUp()) { LOG("[ETH] Link did not recover"); return false; }

  // Re-apply static IP — link recovery can clear it
  IPAddress ip, gw, sn, dns;
  ip.fromString(ETH_STATIC_IP); gw.fromString(ETH_GATEWAY);
  sn.fromString(ETH_SUBNET);    dns.fromString(ETH_DNS);
  ETH.config(ip, gw, sn, dns);

  uint32_t t1 = millis();
  while (ETH.localIP() == IPAddress(0,0,0,0) && (millis() - t1) < 4000) delay(100);
  if (ETH.localIP() == IPAddress(0,0,0,0)) { LOG("[ETH] No IP after recovery"); return false; }

  LOGf("[ETH] Recovered: %s", ETH.localIP().toString().c_str());
  return true;
}

static bool bringupWiFi_() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(g_cfg.hostname.c_str());
  WiFi.begin(g_cfg.wifiSsid.c_str(), g_cfg.wifiPass.c_str());
  LOGf("[WiFi] Connecting to \"%s\"...", g_cfg.wifiSsid.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 15000) delay(250);
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setSleep(false);   // <-- disable modem sleep, fixes intermittent socket stalls
    LOGf("[WiFi] %s  RSSI: %d", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
  }
  return false;
}

static bool bringupAP_() {
  String apName = g_cfg.hostname + "-AP";
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(apName.c_str(), "esp32s3rgb");
  if (ok) LOGf("[WiFi] AP: %s  IP: %s",
               apName.c_str(), WiFi.softAPIP().toString().c_str());
  else    LOG("[WiFi] AP failed");
  return ok;
}

static void otaBegin() {
  ArduinoOTA.setPort(3232);
  ArduinoOTA.setHostname(g_cfg.hostname.c_str());
  ArduinoOTA
    .onStart([]()  { LOG("[OTA] Start"); })
    .onEnd([]()    { LOG("[OTA] End"); })
    .onProgress([](unsigned int p, unsigned int t)
                   { LOGf("[OTA] %u%%", t ? p * 100 / t : 0u); })
    .onError([](ota_error_t e) { LOGf("[OTA] Error %u", (unsigned)e); });
  ArduinoOTA.begin();
  LOG("[OTA] Ready on port 3232");
}

// ── Failover helpers ──────────────────────────────────────────────────────────
static bool s_serverRunning = false;

static void startServer() {
  if (s_serverRunning) server.stop();
  httpStart();
  s_serverRunning = true;
}

static void switchToEth()  { ethReconnect(); g_mode = MODE_ETH; }
static void switchToWiFi() { bringupWiFi_();  startServer(); g_mode = MODE_WIFI; }
static void switchToAP()   { bringupAP_();    startServer(); g_mode = MODE_AP;   }

// ── setup() ───────────────────────────────────────────────────────────────────
void setup() {
  loggerInit();
  LOG("[BOOT] ESP32-S3 relay board (core 3.x)");
  LOGf("[BOOT] Free heap: %u", ESP.getFreeHeap());
  esp_task_wdt_deinit();

  configLoad();

  // Register ETH events once before ETH.begin
  Network.onEvent(onEthEvent);

  // Network — priority: ETH → WiFi STA → AP
  if      (ethInit())      g_mode = MODE_ETH;
  else if (bringupWiFi_()) g_mode = MODE_WIFI;
  else { bringupAP_();     g_mode = MODE_AP; }

  relayInit();
  rgb.begin();
  rgb.setHeartbeatEnabled(true);
  BoardPins::configInputs();

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");  // UTC, no DST offset — good for marine/UTC logging
  LOG("[TIME] SNTP sync requested");

  startServer();
  otaBegin();
  if (g_mode != MODE_AP) MDNS.begin(g_cfg.hostname.c_str());

  // Start background tasks on core 0 (skip in AP mode — no broker reachable)
  if (g_mode != MODE_AP) {
    mqttStart();
    skStart();
  }

  rgb.setForMask((uint8_t)g_mode);
  String ip = (g_mode == MODE_ETH) ? ETH.localIP().toString()
            : (g_mode == MODE_AP)  ? WiFi.softAPIP().toString()
            :                        WiFi.localIP().toString();
  LOGf("[NET] %s @ %s",
       (g_mode==MODE_ETH)?"Ethernet":(g_mode==MODE_WIFI)?"Wi-Fi":"AP",
       ip.c_str());
  LOG("[BOOT] Setup complete");
}

// ── loop() — core 1 ───────────────────────────────────────────────────────────
void loop() {
  const uint32_t now = millis();

  server.handleClient();
  ArduinoOTA.handle();
  rgb.tick();

  // ── SignalK token refresh — handled here on core 1, never inside the SK task
  if (skTokenRefreshNeeded() && !skAccessDenied()) {
    LOG("[NET] SK token rejected (403) — submitting new device access request");
    skStop();
    skRequestAccess();  // blocking HTTP + poll — safe on core 1
    skStart();          // restart task with updated token
  }

  // ── Network failover (800 ms debounce) ─────────────────────────────────────
  static uint32_t stableMs = 0;
  static uint8_t  pending  = MODE_NONE;

  auto ethGood  = []{ return ETH.linkUp() && ETH.localIP() != IPAddress(0,0,0,0); };
  auto wifiGood = []{ return WiFi.status() == WL_CONNECTED; };

  uint8_t want = ethGood()  ? (uint8_t)MODE_ETH  :
                 wifiGood() ? (uint8_t)MODE_WIFI  : (uint8_t)MODE_AP;

  if (want != g_mode) {
    if (pending != want) { pending = want; stableMs = now; }
    else if (now - stableMs >= 800) {
      LOGf("[NET] Switching → mode %d", want);
      if      (pending == MODE_ETH)  switchToEth();
      else if (pending == MODE_WIFI) switchToWiFi();
      else                           switchToAP();
      g_mode  = pending;
      pending = MODE_NONE;
    }
  } else {
    pending = MODE_NONE;
  }

  // ── Mode / IP change logging ────────────────────────────────────────────────
  static uint8_t   lastMode = 255;
  static IPAddress lastIP(0,0,0,0);
  IPAddress curIP = (g_mode == MODE_ETH) ? ETH.localIP()
                  : (g_mode == MODE_AP)  ? WiFi.softAPIP()
                  :                        WiFi.localIP();
  if (g_mode != lastMode || curIP != lastIP) {
    lastMode = g_mode; lastIP = curIP;
    rgb.setForMask(g_mode);
    LOGf("[NET] %s @ %s",
         (g_mode==MODE_ETH)?"Ethernet":(g_mode==MODE_WIFI)?"Wi-Fi":"AP",
         curIP.toString().c_str());
  }

  // ── Digital input polling @ 50 ms with one-poll debounce ───────────────────
  static uint32_t lastDIPoll  = 0;
  static uint8_t  prevDI      = readDI_mask();  // initialised from hardware
  static uint8_t  candidateDI = 0;              // reading from last poll
  static bool     firstPoll   = true;

  if (now - lastDIPoll >= 50) {
    lastDIPoll = now;
    uint8_t curDI = readDI_mask();

    if (firstPoll) {
      // Swallow the first poll — just capture stable baseline
      candidateDI = curDI;
      firstPoll   = false;
    } else if (curDI == candidateDI && curDI != prevDI) {
      // Reading stable across two consecutive polls — treat as real event
      for (uint8_t i = 0; i < BoardPins::DI_COUNT; ++i) {
        bool was = (prevDI >> i) & 1, is = (curDI >> i) & 1;
        if (was != is) {
          LOGf("DI%d %s", i + 1, is ? "activated" : "deactivated");
          mqttPublishInputState(i, curDI);
          skPublishInputState(i, curDI);
        }
      }
      mqttPublishAllInputStates();
      skPublishAllInputStates();
      prevDI = curDI;
    } else {
      // Reading changed or is new — store as candidate, check next poll
      candidateDI = curDI;
    }
  }
}