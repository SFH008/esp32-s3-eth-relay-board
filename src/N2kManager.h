#pragma once
#include <Arduino.h>

void    n2kStart();
void    n2kStop();
void    n2kRequestPublish();
void    n2kPublishRelayStates(uint8_t relayMask);
void    n2kPublishInputStates(uint8_t diMask);
bool    n2kIsRunning();
// Returns ms elapsed since last Cerbo frame (0 = never seen)
uint32_t n2kCerboLastSeenMs();