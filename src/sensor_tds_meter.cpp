#include "sensor_tds_meter.h"

#include <Arduino.h>
#include <Preferences.h>
#include <stdio.h>
#include <string.h>

#include "time_rtc.h"

static const uint32_t TDS_STALE_MS = 60000UL;

struct ProbeState {
    float         temp;
    float         ec;
    float         tds;
    float         rawEc;
    unsigned long lastSuccessMs;
    uint16_t      failCount;
    char          lastError[40];
};

static ProbeState probes[2];

// NVS-persisted calibration factors
static float        ecCalFactor[2] = {1.0f, 1.0f};
static char         calDate[2][24]  = {"", ""};

// RAM-only calibration state machine per zone
enum class TdsCalState : uint8_t { IDLE, ACTIVE, DONE };
static TdsCalState  calState[2]          = {TdsCalState::IDLE, TdsCalState::IDLE};
static float        calSuggestedFactor[2] = {0.0f, 0.0f};
static float        calRefEc[2]           = {0.0f, 0.0f};
static float        calSnapshotRawEc[2]   = {0.0f, 0.0f};

static Preferences prefs;

static bool validZone(uint8_t zone) { return zone < 2; }

static void loadNvs() {
    prefs.begin("tds_cal", true);
    ecCalFactor[0] = prefs.getFloat("ecf_0", 1.0f);
    ecCalFactor[1] = prefs.getFloat("ecf_1", 1.0f);
    prefs.getString("dt_0", calDate[0], sizeof(calDate[0]));
    prefs.getString("dt_1", calDate[1], sizeof(calDate[1]));
    prefs.end();
}

void sensorTdsMeter_begin() {
    memset(probes, 0, sizeof(probes));
    loadNvs();
    Serial.println("[TDS] EC/TDS meter driver ready");
}

void sensorTdsMeter_loop() {
    // staleness is lazy-evaluated in isOnline(); nothing to poll
}

void sensorTdsMeter_onFrame(const char* line) {
    float t1, ec1, p1, t2, ec2, p2;
    if (sscanf(line, "$WM,%f,%f,%f,%f,%f,%f", &t1, &ec1, &p1, &t2, &ec2, &p2) == 6) {
        unsigned long now = millis();

        probes[0].rawEc = ec1;
        probes[0].temp  = t1;
        probes[0].ec    = ec1 * ecCalFactor[0];
        probes[0].tds   = p1  * ecCalFactor[0];
        probes[0].lastSuccessMs = now;
        strncpy(probes[0].lastError, "ok", sizeof(probes[0].lastError) - 1);
        probes[0].lastError[sizeof(probes[0].lastError) - 1] = '\0';

        probes[1].rawEc = ec2;
        probes[1].temp  = t2;
        probes[1].ec    = ec2 * ecCalFactor[1];
        probes[1].tds   = p2  * ecCalFactor[1];
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

// ── Calibration ──────────────────────────────────────────────────────────────

void sensorTdsMeter_calBegin(uint8_t zone) {
    if (!validZone(zone)) return;
    calSnapshotRawEc[zone] = probes[zone].rawEc;
    calState[zone]         = TdsCalState::ACTIVE;
}

bool sensorTdsMeter_calCommit(uint8_t zone, float refEcUscm) {
    if (!validZone(zone)) return false;
    if (calState[zone] != TdsCalState::ACTIVE) {
        Serial.printf("[TDS] calCommit zone=%d: not active\n", zone);
        return false;
    }
    if (!sensorTdsMeter_isOnline(zone)) {
        Serial.printf("[TDS] calCommit zone=%d: not online\n", zone);
        return false;
    }
    float raw = probes[zone].rawEc;
    if (raw <= 0.0f) {
        Serial.printf("[TDS] calCommit zone=%d: rawEc is zero\n", zone);
        return false;
    }
    calRefEc[zone]           = refEcUscm;
    calSuggestedFactor[zone] = refEcUscm / raw;
    calState[zone]           = TdsCalState::DONE;
    return true;
}

bool sensorTdsMeter_calAccept(uint8_t zone) {
    if (!validZone(zone)) return false;
    if (calState[zone] != TdsCalState::DONE) return false;
    ecCalFactor[zone] = calSuggestedFactor[zone];
    String ts = timeRtc_getISOTimestamp();
    strlcpy(calDate[zone], ts.c_str(), sizeof(calDate[zone]));
    prefs.begin("tds_cal", false);
    if (zone == 0) {
        prefs.putFloat("ecf_0", ecCalFactor[0]);
        prefs.putString("dt_0", calDate[0]);
    } else {
        prefs.putFloat("ecf_1", ecCalFactor[1]);
        prefs.putString("dt_1", calDate[1]);
    }
    prefs.end();
    calState[zone] = TdsCalState::IDLE;
    return true;
}

void sensorTdsMeter_calAbort(uint8_t zone) {
    if (validZone(zone)) calState[zone] = TdsCalState::IDLE;
}

const char* sensorTdsMeter_getCalState(uint8_t zone) {
    if (!validZone(zone)) return "idle";
    switch (calState[zone]) {
        case TdsCalState::ACTIVE: return "active";
        case TdsCalState::DONE:   return "done";
        default:                  return "idle";
    }
}

float sensorTdsMeter_getCalSuggestedFactor(uint8_t zone) {
    return validZone(zone) ? calSuggestedFactor[zone] : 0.0f;
}

float sensorTdsMeter_getCalRefEc(uint8_t zone) {
    return validZone(zone) ? calRefEc[zone] : 0.0f;
}

float sensorTdsMeter_getRawEc(uint8_t zone) {
    return validZone(zone) ? probes[zone].rawEc : 0.0f;
}

float sensorTdsMeter_getEcCalFactor(uint8_t zone) {
    return validZone(zone) ? ecCalFactor[zone] : 1.0f;
}

bool sensorTdsMeter_setEcCalFactor(uint8_t zone, float factor) {
    if (!validZone(zone) || factor <= 0.0f) return false;
    ecCalFactor[zone] = factor;
    prefs.begin("tds_cal", false);
    prefs.putFloat(zone == 0 ? "ecf_0" : "ecf_1", factor);
    prefs.end();
    return true;
}

const char* sensorTdsMeter_getCalDate(uint8_t zone) {
    return validZone(zone) ? calDate[zone] : "";
}

bool sensorTdsMeter_setCalDate(uint8_t zone, const char* date) {
    if (!validZone(zone) || !date) return false;
    strlcpy(calDate[zone], date, sizeof(calDate[zone]));
    prefs.begin("tds_cal", false);
    prefs.putString(zone == 0 ? "dt_0" : "dt_1", date);
    prefs.end();
    return true;
}

void sensorTdsMeter_setCalRefEc(uint8_t zone, float ec) {
    if (validZone(zone)) calRefEc[zone] = ec;
}
