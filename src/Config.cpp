#include "Config.h"
#include "Logger.h"
#include "esp_mac.h"

AppConfig g_cfg;

void configLoad() {
  Preferences p;
  p.begin("cfg", true);

  // ── WiFi ────────────────────────────────────────────────────────────────────
  g_cfg.wifiSsid = p.getString("ssid",  DEFAULT_WIFI_SSID);
  g_cfg.wifiPass = p.getString("wpass", DEFAULT_WIFI_PASS);
  g_cfg.hostname = p.getString("host",  DEFAULT_HOSTNAME);

  // ── Ethernet static IP ──────────────────────────────────────────────────────
  g_cfg.ethIp      = p.getString("eth_ip",  DEFAULT_ETH_IP);
  g_cfg.ethGateway = p.getString("eth_gw",  DEFAULT_ETH_GW);
  g_cfg.ethSubnet  = p.getString("eth_sn",  DEFAULT_ETH_SN);
  g_cfg.ethDns     = p.getString("eth_dns", DEFAULT_ETH_DNS);

  IPAddress probe;
  auto validIP = [&](const String &s, const char *def) -> String {
    return probe.fromString(s) ? s : String(def);
  };
  g_cfg.ethIp      = validIP(g_cfg.ethIp,      DEFAULT_ETH_IP);
  g_cfg.ethGateway = validIP(g_cfg.ethGateway,  DEFAULT_ETH_GW);
  g_cfg.ethSubnet  = validIP(g_cfg.ethSubnet,   DEFAULT_ETH_SN);
  g_cfg.ethDns     = validIP(g_cfg.ethDns,      DEFAULT_ETH_DNS);

  // ── MQTT ────────────────────────────────────────────────────────────────────
  g_cfg.mqttBroker    = p.getString("mbroker", DEFAULT_MQTT_BROKER);
  g_cfg.mqttPort      = (uint16_t)p.getUInt("mport",   DEFAULT_MQTT_PORT);
  g_cfg.mqttUser      = p.getString("muser",  DEFAULT_MQTT_USER);
  g_cfg.mqttPass      = p.getString("mpass",  DEFAULT_MQTT_PASS);
  g_cfg.mqttClientId  = p.getString("mcid",   DEFAULT_MQTT_CLIENT_ID);
  g_cfg.mqttBaseTopic = p.getString("mtopic", DEFAULT_MQTT_BASE_TOPIC);

  // ── SignalK ─────────────────────────────────────────────────────────────────
  g_cfg.skServer     = p.getString("skhost",   DEFAULT_SK_SERVER);
  g_cfg.skPort       = (uint16_t)p.getUInt("skport",  DEFAULT_SK_PORT);
  g_cfg.skToken      = p.getString("sktoken",  DEFAULT_SK_TOKEN);
  g_cfg.skUser       = p.getString("skuser",   DEFAULT_SK_USER);
  g_cfg.skPass       = p.getString("skpass",   DEFAULT_SK_PASS);
  g_cfg.skVesselName = p.getString("skvessel", DEFAULT_SK_VESSEL_NAME);

  if (g_cfg.skVesselName == "vessels.self" ||
      g_cfg.skVesselName.startsWith("vessels.urn:") ||
      g_cfg.skVesselName.length() == 0) {
    g_cfg.skVesselName = DEFAULT_SK_VESSEL_NAME;
    LOGf("[CFG] Corrected invalid skVesselName → %s", g_cfg.skVesselName.c_str());
  }

  g_cfg.skClientId = p.getString("skclientid", "");
  if (g_cfg.skClientId.length() == 0) {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char buf[38];
    snprintf(buf, sizeof(buf),
             "%02x%02x%02x%02x-%02x%02x-4%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],
             mac[0]^0x10,mac[1]^0x20,mac[2]^0x30,mac[3]^0x40,
             mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    g_cfg.skClientId = String(buf);
    p.end();
    Preferences pw; pw.begin("cfg", false);
    pw.putString("skclientid", g_cfg.skClientId);
    pw.end();
    p.begin("cfg", true);
    LOGf("[CFG] Generated SK clientId: %s", g_cfg.skClientId.c_str());
  }

  // ── NMEA 2000 ───────────────────────────────────────────────────────────────
  g_cfg.n2kDeviceInstance = (uint8_t)p.getUInt("n2k_inst", DEFAULT_N2K_DEVICE_INSTANCE);
  g_cfg.n2kSourceAddress  = (uint8_t)p.getUInt("n2k_addr", DEFAULT_N2K_SOURCE_ADDRESS);
  if (g_cfg.n2kDeviceInstance > 252) g_cfg.n2kDeviceInstance = DEFAULT_N2K_DEVICE_INSTANCE;
  if (g_cfg.n2kSourceAddress  > 251) g_cfg.n2kSourceAddress  = DEFAULT_N2K_SOURCE_ADDRESS;

  // ── Relay labels ─────────────────────────────────────────────────────────────
  for (uint8_t i = 0; i < BoardPins::RELAY_COUNT; ++i) {
    char key[6]; snprintf(key, sizeof(key), "rl%d", i);
    char def[4]; snprintf(def, sizeof(def), "R%d", i + 1);
    g_cfg.relayLabels[i] = p.getString(key, def);
    if (g_cfg.relayLabels[i].length() == 0 || g_cfg.relayLabels[i].length() > 16) {
      g_cfg.relayLabels[i] = String(def);
    }
  }

  p.end();

  LOGf("[CFG] host=%s eth=%s mqtt=%s:%u sk=%s:%u n2k_inst=%u",
       g_cfg.hostname.c_str(), g_cfg.ethIp.c_str(),
       g_cfg.mqttBroker.c_str(), g_cfg.mqttPort,
       g_cfg.skServer.c_str(), g_cfg.skPort,
       g_cfg.n2kDeviceInstance);
}

void configSave() {
  Preferences p;
  p.begin("cfg", false);

  p.putString("ssid",  g_cfg.wifiSsid);
  p.putString("wpass", g_cfg.wifiPass);
  p.putString("host",  g_cfg.hostname);

  p.putString("eth_ip",  g_cfg.ethIp);
  p.putString("eth_gw",  g_cfg.ethGateway);
  p.putString("eth_sn",  g_cfg.ethSubnet);
  p.putString("eth_dns", g_cfg.ethDns);

  p.putString("mbroker", g_cfg.mqttBroker);
  p.putUInt("mport",     g_cfg.mqttPort);
  p.putString("muser",   g_cfg.mqttUser);
  p.putString("mpass",   g_cfg.mqttPass);
  p.putString("mcid",    g_cfg.mqttClientId);
  p.putString("mtopic",  g_cfg.mqttBaseTopic);

  p.putString("skhost",    g_cfg.skServer);
  p.putUInt("skport",      g_cfg.skPort);
  p.putString("sktoken",   g_cfg.skToken);
  p.putString("skuser",    g_cfg.skUser);
  p.putString("skpass",    g_cfg.skPass);
  p.putString("skclientid", g_cfg.skClientId);
  p.putString("skvessel",  g_cfg.skVesselName);

  p.putUInt("n2k_inst", g_cfg.n2kDeviceInstance);
  p.putUInt("n2k_addr", g_cfg.n2kSourceAddress);

  for (uint8_t i = 0; i < BoardPins::RELAY_COUNT; ++i) {
    char key[6]; snprintf(key, sizeof(key), "rl%d", i);
    p.putString(key, g_cfg.relayLabels[i]);
  }

  p.end();
  LOG("[CFG] Saved to NVS");
}

void configReset() {
  g_cfg.wifiSsid      = DEFAULT_WIFI_SSID;
  g_cfg.wifiPass      = DEFAULT_WIFI_PASS;
  g_cfg.hostname      = DEFAULT_HOSTNAME;
  g_cfg.ethIp         = DEFAULT_ETH_IP;
  g_cfg.ethGateway    = DEFAULT_ETH_GW;
  g_cfg.ethSubnet     = DEFAULT_ETH_SN;
  g_cfg.ethDns        = DEFAULT_ETH_DNS;
  g_cfg.mqttBroker    = DEFAULT_MQTT_BROKER;
  g_cfg.mqttPort      = DEFAULT_MQTT_PORT;
  g_cfg.mqttUser      = DEFAULT_MQTT_USER;
  g_cfg.mqttPass      = DEFAULT_MQTT_PASS;
  g_cfg.mqttClientId  = DEFAULT_MQTT_CLIENT_ID;
  g_cfg.mqttBaseTopic = DEFAULT_MQTT_BASE_TOPIC;
  g_cfg.skServer      = DEFAULT_SK_SERVER;
  g_cfg.skPort        = DEFAULT_SK_PORT;
  g_cfg.skToken       = DEFAULT_SK_TOKEN;
  g_cfg.skUser        = DEFAULT_SK_USER;
  g_cfg.skPass        = DEFAULT_SK_PASS;
  // skClientId intentionally NOT reset
  g_cfg.skVesselName       = DEFAULT_SK_VESSEL_NAME;
  g_cfg.n2kDeviceInstance  = DEFAULT_N2K_DEVICE_INSTANCE;
  g_cfg.n2kSourceAddress   = DEFAULT_N2K_SOURCE_ADDRESS;
  for (uint8_t i = 0; i < BoardPins::RELAY_COUNT; ++i) {
    char def[4]; snprintf(def, sizeof(def), "R%d", i + 1);
    g_cfg.relayLabels[i] = String(def);
  }
  configSave();
  LOG("[CFG] Reset to defaults");
}