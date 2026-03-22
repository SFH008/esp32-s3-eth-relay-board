#pragma once
#include <Arduino.h>

// Initialise MQTT client and start the dedicated FreeRTOS task.
// Must be called after network mode is determined.
void mqttStart();

// Stop the MQTT task and disconnect cleanly.
void mqttStop();

// State accessors used by HttpHandlers and RelayControl
bool mqttIsConnected();

// Called by RelayControl — safe to call from any task (queues internally)
void mqttPublishRelayState(uint8_t idx);
void mqttPublishAllRelayStates();
void mqttPublishInputState(uint8_t idx, uint8_t di_mask);
void mqttPublishAllInputStates();
