# ESP32-S3 Ethernet Relay Board

Firmware for the **Waveshare ESP32-S3-ETH-8DI-8RO**: eight relay outputs and eight digital inputs, with a local web interface, MQTT, Signal K, and a Shelly-compatible interface for Victron GX devices.

Built with PlatformIO, pioarduino, and the ESP32 Arduino 3.x framework.

## Features

- W5500 Ethernet, Wi-Fi station mode, and fallback access point.
- Eight relays controlled through the TCA9554 I/O expander; outputs initialize **OFF**.
- Eight digital inputs with debounced state reporting.
- Web dashboard for relay control, input status, configuration, and logs.
- MQTT relay commands and state publishing.
- Signal K relay/input deltas, token-based device authorization, and inbound relay PUT handling.
- Shelly Gen2-style discovery and RPC exposing eight switch channels.
- USB flashing, Arduino OTA, and RGB status LED.
- Persistent settings in ESP32 NVS.

## Hardware

Target module: ESP32-S3-WROOM-1U-N16R8, with 16 MB flash and 8 MB octal PSRAM.

Pin assignments live in [src/BoardPins.h](src/BoardPins.h):

| Function | Pins / address |
| --- | --- |
| W5500 SPI | SCLK 15, MISO 14, MOSI 13 |
| W5500 control | CS 16, interrupt 12, reset 39 |
| Relay expander I2C | SDA 42, SCL 41; TCA9554 address 0x20 |
| Digital inputs 1â€“8 | GPIO 4â€“11 |
| RGB LED | GPIO 38 |

## Build and upload

Install VS Code with PlatformIO, or use the PlatformIO CLI. Clone the repository and open the folder containing `platformio.ini`.

```sh
git clone https://github.com/SFH008/esp32-s3-eth-relay-board.git
cd esp32-s3-eth-relay-board
python -m platformio run
python -m platformio device list
```

Upload using the port reported for your board. COM6 is an example, not a required port:

```sh
python -m platformio run --target upload --upload-port COM6
python -m platformio device monitor --port COM6 --baud 115200
```

Close the serial monitor before uploading. The build pins WebSockets to **2.7.3** for the shared HTTP/WebSocket implementation. No precompiled firmware is provided.

## Network and configuration

Review [src/Config.h](src/Config.h) before first use, including the existing Wi-Fi defaults, broker, server, and static Ethernet settings.

The compiled Ethernet default is `192.168.2.220/24`, with gateway `192.168.2.1`. Static Ethernet settings require recompilation. Wi-Fi receives its address from the network.

Open `http://BOARD-IP/` to configure Wi-Fi, hostname, MQTT, Signal K, and Shelly compatibility. Saved runtime settings take precedence over compiled defaults and survive uploads that preserve NVS.

The firmware prefers Ethernet, then Wi-Fi, and provides a fallback access point when neither is available. Consult the startup log for the active interface and address.

## Signal K

1. Set the Signal K host and port in the board's web interface.
2. Click **Request access now**.
3. Approve the request in Signal K Admin with the permissions needed to publish data.
4. After the token is saved, reboot the board to reload its WebSocket authorization headers.

The client connects to `/signalk/v1/stream?subscribe=none`, supplies its token in the HTTP **Authorization: Bearer** header, sends an initial state snapshot, and publishes changes and periodic relay snapshots.

| Value | Path |
| --- | --- |
| Relay 1â€“8 | `electrical.switches.bank.relays.<1â€“8>.state` |
| Input 1â€“8 | `electrical.switches.bank.inputs.<1â€“8>.state` |

A one-time startup probe logs `[SK-DIAG]` status without printing credentials:

- `101 Switching Protocols`: upgrade accepted.
- `401 Unauthorized`: stored token was not accepted; reauthorize and reboot.
- TCP connection failure: check the configured address, port, and network.

The WebSocket library handles reconnects at ten-second intervals and uses ping/pong checks. Serial logging avoids waiting for an attached USB host; the web log retains complete application log lines. Long JSON may be truncated in the displayed log without truncating the outgoing delta.

## MQTT

Configure the broker and base topic through the web interface. For a base topic of `relayboard_can`:

| Topic | Example payload | Action |
| --- | --- | --- |
| `relayboard_can/relay/1/set` | `ON` or `OFF` | Set relay 1 |
| `relayboard_can/relay/1/set` | `{"state":true}` | Set relay 1 |
| `relayboard_can/relay/all/set` | `{"mask":5}` | Set relays 1 and 3 ON; others OFF |
| `relayboard_can/relay/all/set` | `ALLON` or `ALLOFF` | Set all outputs |

Relay topic numbers are **one-based**.

## Victron Cerbo / Shelly compatibility

The board advertises a stable mDNS name:

```text
shelly-waveshare-<12 lowercase MAC digits>.local
```

It offers WebSocket RPC at `ws://BOARD-IP/rpc` on the same port 80 as the web UI. Check identification at `http://BOARD-IP/shelly`.

In the GX device's Shelly integration settings, discover the board and enable the desired channels. If available in your Venus OS version, manual IP entry can be used when multicast discovery is unavailable.

Shelly switch IDs are **zero-based**: `switch:0` maps to physical relay 1, through `switch:7` for relay 8. The interface reports switching capability without energy measurements.

See [README-SHELLY.md](README-SHELLY.md) for RPC methods, persistent names, limitations, and acceptance checks. This is custom compatibility firmware, not official Shelly firmware.

## Verification status

The operator compiled and uploaded the firmware and confirmed:

- Wi-Fi, MQTT, and the Shelly identification endpoint.
- A Signal K WebSocket upgrade using the approved device token.
- Approximately 30 minutes of Signal K heartbeats without disconnects.

End-to-end Cerbo relay switching, network failover, and the supplied live acceptance script remain unverified.

Read-only live checks:

```sh
python -m pip install websocket-client
python test/shelly_live.py BOARD-IP
```

To explicitly exercise relay 1 and restore its original state:

```sh
python test/shelly_live.py BOARD-IP --exercise-relay 0
```

## Source layout

| File | Responsibility |
| --- | --- |
| `src/main.cpp` | Startup, networking, digital input polling |
| `src/RelayControl.cpp` | Relay hardware and serialized state changes |
| `src/HttpHandlers.cpp` | Web dashboard and HTTP API |
| `src/ShellyRpc.cpp` | Shelly RPC, discovery, and notifications |
| `src/SignalKClient.cpp` | Signal K connection and publishing |
| `src/MqttManager.cpp` | MQTT commands and publishing |
| `src/Config.cpp` | Persistent runtime configuration |
| `src/Logger.cpp` | Web log and nonblocking serial mirror |
