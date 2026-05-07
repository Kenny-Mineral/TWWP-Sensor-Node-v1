#pragma once
#include <Arduino.h>

bool     sessionFlow_begin();
void     sessionFlow_loop();

bool     sessionFlow_isEnabled();
void     sessionFlow_setEnabled(bool en);  // persisted to NVS
void     sessionFlow_factoryReset();       // clear session ID + last-session state from NVS

// Runtime-configurable thresholds (persisted to NVS)
void     sessionFlow_setIdleTimeout(uint32_t s);   // 5–100 s
uint32_t sessionFlow_getIdleTimeoutS();
void     sessionFlow_setFlowThreshold(float lpm);  // 0.01–0.5 L/min
float    sessionFlow_getFlowThreshold();

// Leak suspect: non-zero flow below threshold while no session is active
bool     sessionFlow_getLeakSuspect(uint8_t ch);   // ch = 1 or 2

// Republish the retained sessions_recent topic (call on MQTT reconnect)
void     sessionFlow_republishRecentSessions();

// Getters for last completed session — used in status payload
uint32_t sessionFlow_getLastId();
uint32_t sessionFlow_getLastStartTs();
uint32_t sessionFlow_getLastEndTs();
uint32_t sessionFlow_getLastDurationS();
uint32_t sessionFlow_getLastFlowDurationS(); // actual time water was flowing (excludes idle gaps)
uint32_t sessionFlow_getLastIdleTimeS();     // idle gap time within session (dur - flow_dur)
float    sessionFlow_getLastVolumeOut();     // sensor 1 — RO purified output (L)
float    sessionFlow_getLastVolumeIn();      // sensor 2 — RO total input (L)
float    sessionFlow_getLastPeakOut();       // peak L/min on channel 1
float    sessionFlow_getLastPeakIn();        // peak L/min on channel 2
