#pragma once
#include <Arduino.h>
#include <Preferences.h>

// ── Compile-time defaults ─────────────────────────────────────────────────────
// All NVS-backed values can be overridden at runtime via the web UI config card.

#ifndef DEFAULT_WIFI_SSID
  #define DEFAULT_WIFI_SSID        "Network_2.4"
#endif
#ifndef DEFAULT_WIFI_PASS
  #define DEFAULT_WIFI_PASS        "dots1234"
#endif
#ifndef DEFAULT_HOSTNAME
  #define DEFAULT_HOSTNAME         "relayboard_can"
#endif

// MQTT
#ifndef DEFAULT_MQTT_BROKER
  #define DEFAULT_MQTT_BROKER      "192.168.76.171"
#endif
#ifndef DEFAULT_MQTT_PORT
  #define DEFAULT_MQTT_PORT        1883
#endif
#ifndef DEFAULT_MQTT_USER
  #define DEFAULT_MQTT_USER        ""
#endif
#ifndef DEFAULT_MQTT_PASS
  #define DEFAULT_MQTT_PASS        ""
#endif
#ifndef DEFAULT_MQTT_CLIENT_ID
  #define DEFAULT_MQTT_CLIENT_ID   "relayboard_can"
#endif
#ifndef DEFAULT_MQTT_BASE_TOPIC
  #define DEFAULT_MQTT_BASE_TOPIC  "relayboard_can"
#endif

// SignalK
#ifndef DEFAULT_SK_SERVER
  #define DEFAULT_SK_SERVER        "192.168.76.171"
#endif
#ifndef DEFAULT_SK_PORT
  #define DEFAULT_SK_PORT          3000
#endif
#ifndef DEFAULT_SK_TOKEN
  #define DEFAULT_SK_TOKEN         ""
#endif
#ifndef DEFAULT_SK_USER
  #define DEFAULT_SK_USER          ""
#endif
#ifndef DEFAULT_SK_PASS
  #define DEFAULT_SK_PASS          ""
#endif
#ifndef DEFAULT_SK_VESSEL_NAME
  #define DEFAULT_SK_VESSEL_NAME   "relayboard_can"
#endif

// Static Ethernet IP (not NVS-backed — change requires reflash)
#ifndef ETH_STATIC_IP
  #define ETH_STATIC_IP            "192.168.76.220"
#endif
#ifndef ETH_GATEWAY
  #define ETH_GATEWAY              "192.168.76.1"
#endif
#ifndef ETH_SUBNET
  #define ETH_SUBNET               "255.255.255.0"
#endif
#ifndef ETH_DNS
  #define ETH_DNS                  "192.168.76.1"
#endif

// ── Runtime config struct ─────────────────────────────────────────────────────
struct AppConfig {
  // Network
  String wifiSsid;
  String wifiPass;
  String hostname;

  // MQTT
  String   mqttBroker;
  uint16_t mqttPort;
  String   mqttUser;
  String   mqttPass;
  String   mqttClientId;
  String   mqttBaseTopic;

  // SignalK
  String   skServer;      // IP or hostname of the SignalK server
  uint16_t skPort;        // default 3000
  String   skToken;       // Bearer token — obtained via device access request
  String   skUser;        // optional: admin username for /auth/login fallback
  String   skPass;        // optional: admin password for /auth/login fallback
  String   skClientId;    // stable device UUID stored in NVS (auto-generated from MAC)
  String   skVesselName;  // used as source.label in delta messages
};

// Global instance — defined in Config.cpp
extern AppConfig g_cfg;

// Load from NVS, falling back to compile-time defaults for absent keys
void configLoad();

// Persist g_cfg to NVS
void configSave();

// Reset all NVS keys to compile-time defaults and save
void configReset();