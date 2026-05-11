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
float       sensorTdsMeter_getEc(uint8_t zone);        // µS/cm
float       sensorTdsMeter_getTds(uint8_t zone);       // ppm
uint16_t    sensorTdsMeter_getFailCount(uint8_t zone);
const char* sensorTdsMeter_getLastError(uint8_t zone);
