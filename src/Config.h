#pragma once
#include <Arduino.h>
#include <Preferences.h>

// ── Compile-time defaults ─────────────────────────────────────────────────────
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

// Ethernet static IP — NVS-backed; no reflash needed to change.
#ifndef DEFAULT_ETH_IP
  #define DEFAULT_ETH_IP           "192.168.76.220"
#endif
#ifndef DEFAULT_ETH_GW
  #define DEFAULT_ETH_GW           "192.168.76.1"
#endif
#ifndef DEFAULT_ETH_SN
  #define DEFAULT_ETH_SN           "255.255.255.0"
#endif
#ifndef DEFAULT_ETH_DNS
  #define DEFAULT_ETH_DNS          "192.168.76.1"
#endif

// NMEA 2000
#ifndef DEFAULT_N2K_DEVICE_INSTANCE
  #define DEFAULT_N2K_DEVICE_INSTANCE  0
#endif
#ifndef DEFAULT_N2K_SOURCE_ADDRESS
  #define DEFAULT_N2K_SOURCE_ADDRESS   35
#endif

#include "BoardPins.h"

// ── Runtime config struct ─────────────────────────────────────────────────────
struct AppConfig {
  // Network — WiFi
  String wifiSsid;
  String wifiPass;
  String hostname;

  // Network — Ethernet static IP
  String   ethIp;
  String   ethGateway;
  String   ethSubnet;
  String   ethDns;

  // MQTT
  String   mqttBroker;
  uint16_t mqttPort;
  String   mqttUser;
  String   mqttPass;
  String   mqttClientId;
  String   mqttBaseTopic;

  // SignalK
  String   skServer;
  uint16_t skPort;
  String   skToken;
  String   skUser;
  String   skPass;
  String   skClientId;
  String   skVesselName;

  // NMEA 2000
  uint8_t  n2kDeviceInstance;
  uint8_t  n2kSourceAddress;

  // Relay labels — user-editable names shown on buttons
  String   relayLabels[BoardPins::RELAY_COUNT];
};

extern AppConfig g_cfg;

void configLoad();
void configSave();
void configReset();