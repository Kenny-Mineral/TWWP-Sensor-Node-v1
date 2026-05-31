#pragma once
#include <Arduino.h>

// Tank volume integration driver.
// Tracks pressurised storage tank level by integrating Ch2 (RO output into tank)
// minus Ch1 (user consumption from tap). No physical level sensor required.
//
// Tank full detection:
//   - Ch2 flow stops for TANK_FULL_STOP_DURATION_MS AND level >= 90% capacity
//   - On full: snaps level to capacity (corrects integration drift)
//
// Tank calibration:
//   - User runs tanks dry, presses tank_reset_empty in HA
//   - Fill cycle runs (2-3 hrs); Ch3 integrates volume
//   - When flow stops (tank full): accumulated volume becomes new capacity estimate
//   - HA number entity tank_capacity_l persisted to NVS

void  tankMonitor_begin();
void  tankMonitor_loop();

// Called each 1s from main loop with the current flow rates.
void  tankMonitor_feedFlow(float ch2LpmPureOut, float ch1LpmOut);

float tankMonitor_getLevelL();      // current estimated level (L)
float tankMonitor_getLevelPct();    // level as % of capacity (0-100)
bool  tankMonitor_isFull();         // true if flow-stopped AND level >= 90% capacity

float tankMonitor_getCapacityL();
void  tankMonitor_setCapacityL(float l);  // update capacity, persists to NVS

// Reset level to zero (start of calibration fill)
void  tankMonitor_resetEmpty();
