#pragma once
/*
  N2kManager.h — NMEA 2000 integration for the Waveshare ESP32-S3-ETH-8DI-8RO-C

  Hardware:
    CAN TX → GPIO17  (TWAI, isolated transceiver on-board)
    CAN RX → GPIO18  (TWAI, isolated transceiver on-board)
    120Ω termination jumper on board — enable if this is a bus endpoint.

  Library stack (add to platformio.ini lib_deps):
    ttlappalainen/NMEA2000-library
    https://github.com/sergei/NMEA2000_esp32_twai   ← TWAI driver, required for S3

  PGNs used:
    127501  Binary Switch Bank Status  (TX — relay + DI states → bus every 1 s)
    127502  Switch Bank Control        (RX — relay commands from MFD / chartplotter)

  Device instance and address:
    N2K_DEVICE_INSTANCE  — unique per physical device on the bus (0–252).
                           Set via web UI config card like all other settings.
    N2K_SOURCE_ADDRESS   — initial preferred address (0–251). The library handles
                           address claiming automatically.

  Thread safety:
    n2kPublishRelayStates() and n2kPublishInputStates() are called from the
    N2K FreeRTOS task only (via the 1 s ticker inside the task).  External callers
    (RelayControl, DI polling in loop()) post a flag via n2kRequestPublish() which
    is acted on inside the task on the next iteration — no mutex needed.
*/

#include <Arduino.h>

// ── Public API ────────────────────────────────────────────────────────────────

// Start the N2K FreeRTOS task (call after network is up and config is loaded).
void n2kStart();

// Stop and clean up the N2K task (e.g. before a restart).
void n2kStop();

// Called by RelayControl and DI polling to request an immediate state publish.
// Safe to call from any task / core.
void n2kRequestPublish();

// Publish current relay state mask on PGN 127501 (bank 0).
// Called internally by the N2K task; exposed so HttpHandlers can trigger it.
void n2kPublishRelayStates(uint8_t relayMask);

// Publish current digital-input state mask on PGN 127501 (bank 1).
void n2kPublishInputStates(uint8_t diMask);

// Returns true if the N2K bus has been successfully initialised.
bool n2kIsRunning();
