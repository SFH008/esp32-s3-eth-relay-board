# Waveshare relay board: Cerbo Shelly compatibility

This change adds a local Shelly Gen2-style RPC interface to the existing
ESP32-S3-ETH-8DI-8RO firmware. It is a compatibility implementation, not official
Shelly firmware. The existing TCA9554 driver controls all eight outputs.

## Build and flash

Open this project in PlatformIO, then run:

```sh
pio run
pio run --target upload
pio device monitor
```

WebSockets is pinned to 2.7.3 because the shared-port adapter uses its
WebSocketsServerCore API. The existing pioarduino platform selection is retained.
Use your normal USB upload first. Existing NVS network settings are preserved.
No precompiled binary is included.

## Connect to Cerbo

1. Put the board and Cerbo on the same LAN, with multicast DNS available.
2. Visit the board at its IP (the existing Ethernet default is 192.168.2.220).
3. The configuration form has a Victron Shelly compatibility checkbox, enabled
   by default. Save and reboot if changing it.
4. In Cerbo's Shelly integration screen, discover the board and enable the
   desired switch channels. Channel 0 maps to physical relay 1, through channel
   7 for physical relay 8. These are switches without energy metering.
5. If discovery is unavailable, try adding the board IP manually in Cerbo's
   Shelly settings. Available controls depend on the installed Venus OS version.

The device identifies as Waveshare8RO / Waveshare-8RO, with a stable ID and
mDNS hostname of shelly-waveshare-<12 lowercase MAC digits>.local. The existing
configured hostname remains the display name, MQTT identity and Signal K
configuration; use the new mDNS hostname or the IP for network access.
Arduino OTA is advertised on the same mDNS responder, port 3232.
The reported protocol version 1.4.0 is emulated compatibility metadata.

## Supported interface

- WebSocket JSON RPC at ws://BOARD-IP/rpc, on the same port 80 as the web UI.
- HTTP JSON RPC: POST /rpc; HTTP GET /rpc/Method?arguments; GET /shelly.
- Shelly.GetDeviceInfo, GetConfig, GetStatus, GetComponents and ListMethods.
- Switch.GetConfig, GetStatus, Set, Toggle and SetConfig (name only).
- GetComponents pages four fixed channels at a time, honors offset, and returns
  an empty dynamic-only collection. All eight switches appear in status/config.
- Persistent names, boolean validation and channel range validation.
- NotifyStatus after observed state changes, plus NotifyFullStatus every 30s.
  Changes within one loop pass can be coalesced to their final state.
- Relay writes are serialized across MQTT, Signal K, HTTP and Shelly RPC.
- The web UI now refreshes relay button states for changes from other clients.

All outputs still initialize OFF. RPC is local and unauthenticated (auth_en=false).
Power measurements, Shelly cloud, timers, schedules, scripts, firmware updates,
and changes to boot state through Shelly RPC are not implemented. Timed Set
commands are rejected, so a requested timeout is never silently ignored.
Configuration reset also clears Shelly names and restores default enablement.

## Verification

The firmware was compiled and uploaded by the operator. On-device checks
confirmed the Shelly identification endpoint and Signal K's authenticated
WebSocket upgrade. A subsequent log showed approximately 30 minutes of
heartbeats without disconnects. End-to-end Cerbo relay control, failover,
and the acceptance script remain to be verified.

Signal K now sends its stored token using the Authorization Bearer header at
upgrade time. USB serial logging does not wait for a connected host, and the
WebSocket client owns its reconnect loop with a 10-second retry interval.
A one-time SK-DIAG startup probe reports TCP/HTTP upgrade status without logging
credentials. HTTP 401 indicates the token must be reauthorized; after approval,
reboot the board to load the new token into the running client's headers.

After uploading, run the provided acceptance script from a machine on the LAN:

```sh
python -m pip install websocket-client
python test/shelly_live.py 192.168.2.220
```

Default checks do not actuate outputs: they exercise HTTP and WebSocket on the
same listener, device initialization, eight-channel pagination, invalid-command
rejection and web UI access. Run it while other controllers are idle so its
state-consistency assertions are meaningful.

To explicitly exercise physical relay 1 and restore its original state:

```sh
python test/shelly_live.py 192.168.2.220 --exercise-relay 0
```

Then verify in Cerbo that each enabled channel switches its corresponding
relay; changes made through MQTT, Signal K and the web UI should appear in Cerbo.
Verify disconnect/reconnect, Ethernet/Wi-Fi failover, persisted names after
reboot, and that all outputs start OFF. The firmware's existing Signal K access
request can block the Arduino loop for up to 120 seconds during pairing; Cerbo
may need to reconnect afterwards.

References:
- https://github.com/victronenergy/dbus-shelly/blob/master/discovery.py
- https://github.com/victronenergy/dbus-shelly/blob/master/shelly_device.py
- https://github.com/home-assistant-libs/aioshelly/blob/master/aioshelly/rpc_device/device.py
