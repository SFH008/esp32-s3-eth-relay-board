#pragma once
#include <Arduino.h>

// Initialise I2C and TCA9554. Halts on failure.
void relayInit();

// Set a single relay (0-indexed). Publishes MQTT state if connected.
void relaySet(uint8_t idx, bool on);

// Apply a full 8-bit mask. Publishes MQTT state if connected.
void relayApplyMask(uint8_t mask);

// Current relay state bitmask
uint8_t relayGetMask();

// Atomic read-modify-write for RPC was_on and Toggle, shared across cores.
bool relaySetAndGetPrevious(uint8_t idx, bool on, bool toggle = false);
