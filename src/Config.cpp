#include "Config.h"
#include "Logger.h"
#include "esp_mac.h"

AppConfig g_cfg;

void configLoad() {
  Preferences p;
  p.begin("cfg", /*readOnly=*/true);

  // Network
  g_cfg.wifiSsid      = p.getString("ssid",     DEFAULT_WIFI_SSID);
  g_cfg.wifiPass      = p.getString("wpass",    DEFAULT_WIFI_PASS);
  g_cfg.hostname      = p.getString("host",     DEFAULT_HOSTNAME);

  // MQTT
  g_cfg.mqttBroker    = p.getString("mbroker",  DEFAULT_MQTT_BROKER);
  g_cfg.mqttPort      = (uint16_t)p.getUInt("mport",   DEFAULT_MQTT_PORT);
  g_cfg.mqttUser      = p.getString("muser",    DEFAULT_MQTT_USER);
  g_cfg.mqttPass      = p.getString("mpass",    DEFAULT_MQTT_PASS);
  g_cfg.mqttClientId  = p.getString("mcid",     DEFAULT_MQTT_CLIENT_ID);
  g_cfg.mqttBaseTopic = p.getString("mtopic",   DEFAULT_MQTT_BASE_TOPIC);

  // SignalK
  g_cfg.skServer      = p.getString("skhost",    DEFAULT_SK_SERVER);
  g_cfg.skPort        = (uint16_t)p.getUInt("skport",   DEFAULT_SK_PORT);
  g_cfg.skToken       = p.getString("sktoken",   DEFAULT_SK_TOKEN);
  g_cfg.skUser        = p.getString("skuser",    DEFAULT_SK_USER);
  g_cfg.skPass        = p.getString("skpass",    DEFAULT_SK_PASS);
  g_cfg.skVesselName  = p.getString("skvessel",  DEFAULT_SK_VESSEL_NAME);
  // Guard against vessels.self being stored as the source label — that is a
  // context path, not a device identifier, and causes the server to drop deltas
  if (g_cfg.skVesselName == "vessels.self" ||
      g_cfg.skVesselName.startsWith("vessels.urn:") ||
      g_cfg.skVesselName.length() == 0) {
    g_cfg.skVesselName = DEFAULT_SK_VESSEL_NAME;
    LOGf("[CFG] Corrected invalid skVesselName — using default: %s",
         g_cfg.skVesselName.c_str());
  }

  // clientId — generated from MAC if absent (stable across reboots)
  g_cfg.skClientId    = p.getString("skclientid", "");
  if (g_cfg.skClientId.length() == 0) {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char buf[38];
    snprintf(buf, sizeof(buf),
             "%02x%02x%02x%02x-%02x%02x-4%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             mac[0] ^ 0x10, mac[1] ^ 0x20,
             mac[2] ^ 0x30, mac[3] ^ 0x40,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    g_cfg.skClientId = String(buf);
    // Save immediately so it persists
    p.end();
    Preferences pw;
    pw.begin("cfg", false);
    pw.putString("skclientid", g_cfg.skClientId);
    pw.end();
    p.begin("cfg", true);
    LOGf("[CFG] Generated SK clientId: %s", g_cfg.skClientId.c_str());
  }

  p.end();

  LOGf("[CFG] host=%s mqtt=%s:%u sk=%s:%u",
       g_cfg.hostname.c_str(),
       g_cfg.mqttBroker.c_str(), g_cfg.mqttPort,
       g_cfg.skServer.c_str(),   g_cfg.skPort);
}

void configSave() {
  Preferences p;
  p.begin("cfg", /*readOnly=*/false);

  p.putString("ssid",     g_cfg.wifiSsid);
  p.putString("wpass",    g_cfg.wifiPass);
  p.putString("host",     g_cfg.hostname);

  p.putString("mbroker",  g_cfg.mqttBroker);
  p.putUInt("mport",      g_cfg.mqttPort);
  p.putString("muser",    g_cfg.mqttUser);
  p.putString("mpass",    g_cfg.mqttPass);
  p.putString("mcid",     g_cfg.mqttClientId);
  p.putString("mtopic",   g_cfg.mqttBaseTopic);

  p.putString("skhost",      g_cfg.skServer);
  p.putUInt("skport",        g_cfg.skPort);
  p.putString("sktoken",     g_cfg.skToken);
  p.putString("skuser",      g_cfg.skUser);
  p.putString("skpass",      g_cfg.skPass);
  p.putString("skclientid",  g_cfg.skClientId);
  p.putString("skvessel",    g_cfg.skVesselName);

  p.end();
  LOG("[CFG] Saved to NVS");
}

void configReset() {
  g_cfg.wifiSsid      = DEFAULT_WIFI_SSID;
  g_cfg.wifiPass      = DEFAULT_WIFI_PASS;
  g_cfg.hostname      = DEFAULT_HOSTNAME;
  g_cfg.mqttBroker    = DEFAULT_MQTT_BROKER;
  g_cfg.mqttPort      = DEFAULT_MQTT_PORT;
  g_cfg.mqttUser      = DEFAULT_MQTT_USER;
  g_cfg.mqttPass      = DEFAULT_MQTT_PASS;
  g_cfg.mqttClientId  = DEFAULT_MQTT_CLIENT_ID;
  g_cfg.mqttBaseTopic = DEFAULT_MQTT_BASE_TOPIC;
  g_cfg.skServer      = DEFAULT_SK_SERVER;
  g_cfg.skPort        = DEFAULT_SK_PORT;
  g_cfg.skToken       = DEFAULT_SK_TOKEN;  // clear token — force new access request
  g_cfg.skUser        = DEFAULT_SK_USER;
  g_cfg.skPass        = DEFAULT_SK_PASS;
  // skClientId intentionally NOT reset — must remain stable across resets
  g_cfg.skVesselName  = DEFAULT_SK_VESSEL_NAME;
  configSave();
  LOG("[CFG] Reset to defaults");
}