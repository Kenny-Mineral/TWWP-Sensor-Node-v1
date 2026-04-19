#pragma once
#include <Arduino.h>

bool sensorLeak_begin();
void sensorLeak_loop();
bool sensorLeak_isWet();      // true = water detected (LOW signal)
bool sensorLeak_hasChanged(); // true if state changed since last call (clears flag)
