# Changelog

All notable changes to this project are documented here.

## [Unreleased]

### Fixed

- **MQTT connection timeout (`rc=-4`) on boot.** ESP32 Wi-Fi modem sleep (power-save) was introducing intermittent multi-second latency across all sockets, causing `PubSubClient::connect()` to time out even with a healthy, reachable broker. Fixed by calling `WiFi.setSleep(false)` after Wi-Fi association succeeds (`main.cpp`, `bringupWiFi_()`).

- **Web UI could freeze for up to `setSocketTimeout()` seconds during an MQTT reconnect.** `MqttManager.cpp`'s task loop held `s_mutex` across the entire blocking `mqttReconnect()` call, including the underlying `PubSubClient::connect()`. HTTP relay handlers take the same mutex via the publish helpers, so a slow or failing MQTT reconnect could stall unrelated HTTP requests (e.g. `/api/state`, relay toggles). Fixed by only holding the mutex to read connection state, releasing it before the blocking reconnect call.

- **Status LED showed wrong colors per network mode** (e.g. Wi-Fi mode rendered red instead of green). Root cause: WS2812 color order was configured as `GRB` in `RgbLed_WS2812.cpp` but the physical LED expected `RGB`. Fixed by changing the `FastLED.addLeds<...>` template parameter. Cosmetic only — no functional impact.

- **SignalK delta timestamps read as `1970-01-01T...`.** `isoTimestamp()` in `SignalKClient.cpp` built timestamps from `millis()`-since-boot rather than wall-clock time. Added `configTime(0, 0, "pool.ntp.org", "time.nist.gov")` SNTP sync in `setup()`; `isoTimestamp()` now uses `time(nullptr)` once SNTP has synced (validated with a sanity threshold), falling back to the millis-based placeholder during the brief window before sync completes (~1–3s after boot in testing).

### Notes

- Increased `PubSubClient::setSocketTimeout()` from the original `1` second to `10` seconds during investigation — this alone did not resolve the connection issue (the Wi-Fi power-save fix was the actual root cause) but is a reasonable safety margin to keep regardless, since 1 second is aggressive for any real-world network round trip.