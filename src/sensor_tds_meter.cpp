#include "sensor_tds_meter.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

static const uint32_t TDS_STALE_MS = 60000UL;

struct ProbeState {
    float         temp;
    float         ec;
    float         tds;
    unsigned long lastSuccessMs;
    uint16_t      failCount;
    char          lastError[40];
};

static ProbeState probes[2];

static bool validZone(uint8_t zone) { return zone < 2; }

void sensorTdsMeter_begin() {
    memset(probes, 0, sizeof(probes));
    Serial.println("[TDS] EC/TDS meter driver ready");
}

void sensorTdsMeter_loop() {
    // staleness is lazy-evaluated in isOnline(); nothing to poll
}

void sensorTdsMeter_onFrame(const char* line) {
    float t1, ec1, p1, t2, ec2, p2;
    if (sscanf(line, "$WM,%f,%f,%f,%f,%f,%f", &t1, &ec1, &p1, &t2, &ec2, &p2) == 6) {
        unsigned long now = millis();

        probes[0].temp = t1;  probes[0].ec = ec1;  probes[0].tds = p1;
        probes[0].lastSuccessMs = now;
        strncpy(probes[0].lastError, "ok", sizeof(probes[0].lastError) - 1);
        probes[0].lastError[sizeof(probes[0].lastError) - 1] = '\0';

        probes[1].temp = t2;  probes[1].ec = ec2;  probes[1].tds = p2;
        probes[1].lastSuccessMs = now;
        strncpy(probes[1].lastError, "ok", sizeof(probes[1].lastError) - 1);
        probes[1].lastError[sizeof(probes[1].lastError) - 1] = '\0';

        Serial.printf("[TDS] P1 %.1f°C %.0fµS %.0fppm  P2 %.1f°C %.0fµS %.0fppm\n",
                      t1, ec1, p1, t2, ec2, p2);
    } else {
        for (uint8_t i = 0; i < 2; i++) {
            probes[i].failCount++;
            strncpy(probes[i].lastError, "bad frame", sizeof(probes[i].lastError) - 1);
            probes[i].lastError[sizeof(probes[i].lastError) - 1] = '\0';
        }
        Serial.println("[TDS] bad frame");
    }
}

bool sensorTdsMeter_isOnline(uint8_t zone) {
    if (!validZone(zone)) return false;
    if (probes[zone].lastSuccessMs == 0) return false;
    return (millis() - probes[zone].lastSuccessMs) <= TDS_STALE_MS;
}

float sensorTdsMeter_getTemp(uint8_t zone) {
    return validZone(zone) ? probes[zone].temp : 0.0f;
}

float sensorTdsMeter_getEc(uint8_t zone) {
    return validZone(zone) ? probes[zone].ec : 0.0f;
}

float sensorTdsMeter_getTds(uint8_t zone) {
    return validZone(zone) ? probes[zone].tds : 0.0f;
}

uint16_t sensorTdsMeter_getFailCount(uint8_t zone) {
    return validZone(zone) ? probes[zone].failCount : 0;
}

const char* sensorTdsMeter_getLastError(uint8_t zone) {
    return validZone(zone) ? probes[zone].lastError : "";
}
