#pragma once
#include <Arduino.h>

// All socket and NVS work runs in the Arduino loop, never a relay callback.
void shellyBegin();
void shellyLoop();
void shellyNetworkChanged();
void shellyRegisterHttpRoutes();
const String& shellyDeviceId();
bool shellyEnabled();
void shellySetEnabled(bool enabled); // persists; reboot to apply discovery changes


void shellyReset(); // reset this integration alongside the existing config
