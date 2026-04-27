#pragma once
#include <Arduino.h>

bool     sessionFlow_begin();
void     sessionFlow_loop();

// Getters for last completed session — used in status payload and HA discovery
uint32_t sessionFlow_getLastId();
uint32_t sessionFlow_getLastStartTs();
uint32_t sessionFlow_getLastEndTs();
uint32_t sessionFlow_getLastDurationS();
float    sessionFlow_getLastVolumeOut();   // sensor 1 — RO purified output (L)
float    sessionFlow_getLastVolumeIn();    // sensor 2 — RO total input (L)
float    sessionFlow_getLastPeakOut();     // peak L/min on channel 1
float    sessionFlow_getLastPeakIn();      // peak L/min on channel 2
