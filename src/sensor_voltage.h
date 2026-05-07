#pragma once
#include <Arduino.h>

bool        sensorVoltage_begin();
void        sensorVoltage_loop();

float       sensorVoltage_getVoltageV();    // calibrated battery voltage
float       sensorVoltage_getDividerVoltageV(); // raw ADS1115 input after divider
float       sensorVoltage_getPercentPct();  // 0–100, clamped
const char* sensorVoltage_getState();       // "Charging" / "Discharging" / "Stable"

float sensorVoltage_getVMin();
float sensorVoltage_getVMax();
float sensorVoltage_getCalFactor();

bool sensorVoltage_setVMin(float v);        // persists to NVS
bool sensorVoltage_setVMax(float v);        // persists to NVS
bool sensorVoltage_setCalFactor(float f);   // persists to NVS
