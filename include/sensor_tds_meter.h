#pragma once

#include <stdint.h>

// Driver for the standalone EC/TDS + temperature meter (ESP32 + ADS1115).
// Receives $WM ASCII frames from rs485_mux; no UART knowledge.
//
// Frame format (from ec-meter firmware, every 3 s):
//   $WM,<temp1>,<ec1>,<ppm1>,<temp2>,<ec2>,<ppm2>\r\n
//
// Zone mapping:
//   TDS_ZONE_PRE_RO  (0) — Probe 1
//   TDS_ZONE_POST_RO (1) — Probe 2

#define TDS_ZONE_PRE_RO  0
#define TDS_ZONE_POST_RO 1

void        sensorTdsMeter_begin();
void        sensorTdsMeter_loop();                     // no-op; staleness is lazy
void        sensorTdsMeter_onFrame(const char* line);  // called by rs485_mux

bool        sensorTdsMeter_isOnline(uint8_t zone);
float       sensorTdsMeter_getTemp(uint8_t zone);      // °C
float       sensorTdsMeter_getEc(uint8_t zone);        // µS/cm (cal-corrected)
float       sensorTdsMeter_getTds(uint8_t zone);       // ppm   (cal-corrected)
uint16_t    sensorTdsMeter_getFailCount(uint8_t zone);
const char* sensorTdsMeter_getLastError(uint8_t zone);

// Calibration — software correction factor (NVS-persisted)
// State machine per zone: IDLE → calBegin → ACTIVE → calCommit → DONE → calAccept/Abort → IDLE
void        sensorTdsMeter_calBegin(uint8_t zone);
bool        sensorTdsMeter_calCommit(uint8_t zone, float refEcUscm);
bool        sensorTdsMeter_calAccept(uint8_t zone);
void        sensorTdsMeter_calAbort(uint8_t zone);
const char* sensorTdsMeter_getCalState(uint8_t zone);
float       sensorTdsMeter_getCalSuggestedFactor(uint8_t zone);
float       sensorTdsMeter_getCalRefEc(uint8_t zone);
float       sensorTdsMeter_getRawEc(uint8_t zone);
float       sensorTdsMeter_getEcCalFactor(uint8_t zone);
bool        sensorTdsMeter_setEcCalFactor(uint8_t zone, float factor);
const char* sensorTdsMeter_getCalDate(uint8_t zone);
bool        sensorTdsMeter_setCalDate(uint8_t zone, const char* date);
void        sensorTdsMeter_setCalRefEc(uint8_t zone, float ec);
