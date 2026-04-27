#include "session_flow.h"

#include <ArduinoJson.h>
#include <Preferences.h>

#include "config.h"
#include "net_mqtt.h"
#include "sensor_flow.h"
#include "store_sd.h"
#include "time_rtc.h"

static const char* SESSION_LOG_HEADER =
    "session_id,start_ts,end_ts,duration_s,volume_out_L,volume_in_L,peak_rate_out,peak_rate_in";

enum class SessionState { IDLE, ACTIVE, ENDING };

static SessionState   sessionState         = SessionState::IDLE;
static unsigned long  sessionEndingStartMs = 0;

static uint32_t sessionId          = 0;
static uint32_t sessionStartTs     = 0;
static float    sessionStartTotal1 = 0.0f;
static float    sessionStartTotal2 = 0.0f;
static float    sessionPeakOut     = 0.0f;
static float    sessionPeakIn      = 0.0f;

// Last completed session — returned by getters and included in status payload
static uint32_t lastSessionId        = 0;
static uint32_t lastSessionStartTs   = 0;
static uint32_t lastSessionEndTs     = 0;
static uint32_t lastSessionDurationS = 0;
static float    lastSessionVolumeOut = 0.0f;
static float    lastSessionVolumeIn  = 0.0f;
static float    lastSessionPeakOut   = 0.0f;
static float    lastSessionPeakIn    = 0.0f;

static void finaliseSession() {
    uint32_t endTs     = timeRtc_getUnixTime();
    uint32_t durationS = endTs > sessionStartTs ? (endTs - sessionStartTs) : 0;
    float    volOut    = sensorFlow_getTotalL(1) - sessionStartTotal1;
    float    volIn     = sensorFlow_getTotalL(2) - sessionStartTotal2;

    lastSessionId        = sessionId;
    lastSessionStartTs   = sessionStartTs;
    lastSessionEndTs     = endTs;
    lastSessionDurationS = durationS;
    lastSessionVolumeOut = volOut;
    lastSessionVolumeIn  = volIn;
    lastSessionPeakOut   = sessionPeakOut;
    lastSessionPeakIn    = sessionPeakIn;

    // MQTT — buffered so it survives offline periods
    JsonDocument doc;
    doc["session_id"]    = sessionId;
    doc["start_ts"]      = sessionStartTs;
    doc["end_ts"]        = endTs;
    doc["duration_s"]    = durationS;
    doc["volume_out_L"]  = serialized(String(volOut,    3));
    doc["volume_in_L"]   = serialized(String(volIn,     3));
    doc["peak_rate_out"] = serialized(String(sessionPeakOut, 3));
    doc["peak_rate_in"]  = serialized(String(sessionPeakIn,  3));
    char payload[256];
    if (serializeJson(doc, payload, sizeof(payload)) > 0) {
        netMqtt_publishSub(TOPIC_SESSION, payload);
    }

    // SD
    char row[160];
    snprintf(row, sizeof(row), "%lu,%lu,%lu,%lu,%.3f,%.3f,%.3f,%.3f",
             (unsigned long)sessionId,
             (unsigned long)sessionStartTs,
             (unsigned long)endTs,
             (unsigned long)durationS,
             volOut, volIn,
             sessionPeakOut, sessionPeakIn);
    storeSd_appendCsvRow(SD_SESSION_LOG_PATH, row, SESSION_LOG_HEADER);

    Serial.printf("[SESSION] #%lu ended — out=%.3fL in=%.3fL dur=%lus\n",
                  (unsigned long)sessionId, volOut, volIn, (unsigned long)durationS);

    // Persist incremented session_id
    sessionId++;
    Preferences prefs;
    prefs.begin("session", false);
    prefs.putUInt("sid", sessionId);
    prefs.end();
}

bool sessionFlow_begin() {
    Preferences prefs;
    prefs.begin("session", true);
    sessionId = prefs.getUInt("sid", 0);
    prefs.end();
    Serial.printf("[SESSION] started — next session_id=%lu\n", (unsigned long)sessionId);
    return true;
}

void sessionFlow_loop() {
    float rate1  = sensorFlow_getRateLpm(1);
    float rate2  = sensorFlow_getRateLpm(2);
    bool  anyFlow = (rate1 > FLOW_ACTIVE_THRESHOLD_LPM || rate2 > FLOW_ACTIVE_THRESHOLD_LPM);

    switch (sessionState) {
        case SessionState::IDLE:
            if (anyFlow) {
                sessionState       = SessionState::ACTIVE;
                sessionStartTs     = timeRtc_getUnixTime();
                sessionStartTotal1 = sensorFlow_getTotalL(1);
                sessionStartTotal2 = sensorFlow_getTotalL(2);
                sessionPeakOut     = rate1;
                sessionPeakIn      = rate2;
                Serial.printf("[SESSION] #%lu started\n", (unsigned long)sessionId);
            }
            break;

        case SessionState::ACTIVE:
            if (rate1 > sessionPeakOut) sessionPeakOut = rate1;
            if (rate2 > sessionPeakIn)  sessionPeakIn  = rate2;
            if (!anyFlow) {
                sessionState         = SessionState::ENDING;
                sessionEndingStartMs = millis();
            }
            break;

        case SessionState::ENDING:
            if (anyFlow) {
                sessionState = SessionState::ACTIVE;
                if (rate1 > sessionPeakOut) sessionPeakOut = rate1;
                if (rate2 > sessionPeakIn)  sessionPeakIn  = rate2;
            } else if (millis() - sessionEndingStartMs >= SESSION_IDLE_TIMEOUT_MS) {
                finaliseSession();
                sessionState = SessionState::IDLE;
            }
            break;
    }
}

uint32_t sessionFlow_getLastId()        { return lastSessionId; }
uint32_t sessionFlow_getLastStartTs()   { return lastSessionStartTs; }
uint32_t sessionFlow_getLastEndTs()     { return lastSessionEndTs; }
uint32_t sessionFlow_getLastDurationS() { return lastSessionDurationS; }
float    sessionFlow_getLastVolumeOut() { return lastSessionVolumeOut; }
float    sessionFlow_getLastVolumeIn()  { return lastSessionVolumeIn; }
float    sessionFlow_getLastPeakOut()   { return lastSessionPeakOut; }
float    sessionFlow_getLastPeakIn()    { return lastSessionPeakIn; }
