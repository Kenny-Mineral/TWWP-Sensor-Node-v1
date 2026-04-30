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

static bool     sessionEnabled          = true;
static uint32_t sessionIdleTimeoutMs    = SESSION_IDLE_TIMEOUT_MS;
static float    sessionFlowThresholdLpm = FLOW_ACTIVE_THRESHOLD_LPM;

enum class SessionState { IDLE, ACTIVE, ENDING };

static SessionState   sessionState         = SessionState::IDLE;
static unsigned long  sessionEndingStartMs = 0;

static uint32_t sessionId          = 0;
static uint32_t sessionStartTs     = 0;
static float    sessionStartTotal1 = 0.0f;
static float    sessionStartTotal2 = 0.0f;
static float    sessionPeakOut     = 0.0f;
static float    sessionPeakIn      = 0.0f;

// Last completed session
static uint32_t lastSessionId        = 0;
static uint32_t lastSessionStartTs   = 0;
static uint32_t lastSessionEndTs     = 0;
static uint32_t lastSessionDurationS = 0;
static float    lastSessionVolumeOut = 0.0f;
static float    lastSessionVolumeIn  = 0.0f;
static float    lastSessionPeakOut   = 0.0f;
static float    lastSessionPeakIn    = 0.0f;

// Ring buffer of last SESSIONS_RECENT_MAX sessions (oldest-first, recentHead = oldest slot)
struct SessionRecord {
    uint32_t id;
    uint32_t startTs;
    uint32_t endTs;
    uint32_t durationS;
    float    volOut;
    float    volIn;
    float    peakOut;
    float    peakIn;
};

static SessionRecord recentSessions[SESSIONS_RECENT_MAX];
static uint8_t       recentCount = 0;
static uint8_t       recentHead  = 0;

// ── Ring buffer helpers ───────────────────────────────────────────────────────

static void pushRecentSession(uint32_t id, uint32_t startTs, uint32_t endTs,
                               uint32_t durationS, float volOut, float volIn,
                               float peakOut, float peakIn) {
    uint8_t slot;
    if (recentCount < SESSIONS_RECENT_MAX) {
        slot = (recentHead + recentCount) % SESSIONS_RECENT_MAX;
        recentCount++;
    } else {
        slot      = recentHead;
        recentHead = (recentHead + 1) % SESSIONS_RECENT_MAX;
    }
    recentSessions[slot] = { id, startTs, endTs, durationS, volOut, volIn, peakOut, peakIn };
}

// ── SD persistence ────────────────────────────────────────────────────────────

static void saveRecentSessionsToSd() {
    JsonDocument doc;
    JsonArray arr = doc["sessions"].to<JsonArray>();
    // Oldest-first so loading restores the same order
    for (uint8_t i = 0; i < recentCount; i++) {
        uint8_t slot = (recentHead + i) % SESSIONS_RECENT_MAX;
        JsonObject s = arr.add<JsonObject>();
        s["id"]  = recentSessions[slot].id;
        s["sts"] = recentSessions[slot].startTs;
        s["ets"] = recentSessions[slot].endTs;
        s["dur"] = recentSessions[slot].durationS;
        s["vo"]  = recentSessions[slot].volOut;
        s["vi"]  = recentSessions[slot].volIn;
        s["po"]  = recentSessions[slot].peakOut;
        s["pi"]  = recentSessions[slot].peakIn;
    }
    storeSd_writeJsonFile(SD_SESSIONS_RECENT_PATH, doc);
}

static void loadRecentSessionsFromSd() {
    JsonDocument doc;
    if (!storeSd_readJsonFile(SD_SESSIONS_RECENT_PATH, doc)) return;
    JsonArray arr = doc["sessions"].as<JsonArray>();
    if (arr.isNull()) return;

    recentCount = 0;
    recentHead  = 0;
    for (JsonObject s : arr) {
        if (recentCount >= SESSIONS_RECENT_MAX) break;
        recentSessions[recentCount++] = {
            s["id"]  | 0u,
            s["sts"] | 0u,
            s["ets"] | 0u,
            s["dur"] | 0u,
            s["vo"]  | 0.0f,
            s["vi"]  | 0.0f,
            s["po"]  | 0.0f,
            s["pi"]  | 0.0f
        };
    }
    Serial.printf("[SESSION] loaded %d recent sessions from SD\n", recentCount);
}

// ── MQTT publish ──────────────────────────────────────────────────────────────

static void publishRecentSessions() {
    // ~120 bytes per session × 10 + overhead — 1500 is ample
    static char buf[1500];
    JsonDocument doc;
    JsonArray arr = doc["sessions"].to<JsonArray>();
    // Newest-first for display
    for (int8_t i = (int8_t)recentCount - 1; i >= 0; i--) {
        uint8_t slot = (recentHead + (uint8_t)i) % SESSIONS_RECENT_MAX;
        JsonObject s = arr.add<JsonObject>();
        s["id"]       = recentSessions[slot].id;
        s["start_ts"] = recentSessions[slot].startTs;
        s["end_ts"]   = recentSessions[slot].endTs;
        s["dur_s"]    = recentSessions[slot].durationS;
        s["vol_out"]  = serialized(String(recentSessions[slot].volOut, 3));
        s["vol_in"]   = serialized(String(recentSessions[slot].volIn, 3));
        s["peak_out"] = serialized(String(recentSessions[slot].peakOut, 3));
        s["peak_in"]  = serialized(String(recentSessions[slot].peakIn, 3));
    }
    size_t written = serializeJson(doc, buf, sizeof(buf));
    if (written > 0 && written < sizeof(buf)) {
        netMqtt_publish(TOPIC_SESSIONS_RECENT, buf, true);
    }
}

void sessionFlow_republishRecentSessions() {
    publishRecentSessions();
}

// ── Session lifecycle ─────────────────────────────────────────────────────────

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

    // MQTT event — buffered so it survives offline periods
    JsonDocument doc;
    doc["session_id"]    = sessionId;
    doc["start_ts"]      = sessionStartTs;
    doc["end_ts"]        = endTs;
    doc["duration_s"]    = durationS;
    doc["volume_out_L"]  = serialized(String(volOut,          3));
    doc["volume_in_L"]   = serialized(String(volIn,           3));
    doc["peak_rate_out"] = serialized(String(sessionPeakOut,  3));
    doc["peak_rate_in"]  = serialized(String(sessionPeakIn,   3));
    char payload[256];
    if (serializeJson(doc, payload, sizeof(payload)) > 0) {
        netMqtt_publishSub(TOPIC_SESSION, payload);
    }

    // SD session log
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

    // Ring buffer + retained MQTT + SD snapshot
    pushRecentSession(sessionId, sessionStartTs, endTs, durationS,
                      volOut, volIn, sessionPeakOut, sessionPeakIn);
    publishRecentSessions();
    saveRecentSessionsToSd();

    // Persist incremented session_id
    sessionId++;
    Preferences prefs;
    prefs.begin("session", false);
    prefs.putUInt("sid", sessionId);
    prefs.end();
}

// ── Public API ────────────────────────────────────────────────────────────────

bool sessionFlow_begin() {
    Preferences prefs;
    prefs.begin("session", true);
    sessionId              = prefs.getUInt("sid",      0);
    sessionEnabled         = prefs.getBool("en",       true);
    sessionIdleTimeoutMs   = (uint32_t)prefs.getUInt("idle_s",  90) * 1000UL;
    sessionFlowThresholdLpm = prefs.getFloat("flow_thr", FLOW_ACTIVE_THRESHOLD_LPM);
    prefs.end();

    loadRecentSessionsFromSd();

    Serial.printf("[SESSION] started — next_id=%lu enabled=%s idle=%lus thr=%.2f\n",
                  (unsigned long)sessionId,
                  sessionEnabled ? "yes" : "no",
                  (unsigned long)(sessionIdleTimeoutMs / 1000UL),
                  sessionFlowThresholdLpm);
    return true;
}

void sessionFlow_loop() {
    if (!sessionEnabled) return;

    float rate1  = sensorFlow_getRateLpm(1);
    float rate2  = sensorFlow_getRateLpm(2);
    bool  anyFlow = (rate1 > sessionFlowThresholdLpm || rate2 > sessionFlowThresholdLpm);

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
            } else if (millis() - sessionEndingStartMs >= sessionIdleTimeoutMs) {
                finaliseSession();
                sessionState = SessionState::IDLE;
            }
            break;
    }
}

void sessionFlow_setIdleTimeout(uint32_t s) {
    sessionIdleTimeoutMs = s * 1000UL;
    Preferences prefs;
    prefs.begin("session", false);
    prefs.putUInt("idle_s", s);
    prefs.end();
    Serial.printf("[SESSION] idle timeout set to %lus\n", (unsigned long)s);
}

uint32_t sessionFlow_getIdleTimeoutS() {
    return sessionIdleTimeoutMs / 1000UL;
}

void sessionFlow_setFlowThreshold(float lpm) {
    sessionFlowThresholdLpm = lpm;
    Preferences prefs;
    prefs.begin("session", false);
    prefs.putFloat("flow_thr", lpm);
    prefs.end();
    Serial.printf("[SESSION] flow threshold set to %.2f L/min\n", lpm);
}

float sessionFlow_getFlowThreshold() {
    return sessionFlowThresholdLpm;
}

bool sessionFlow_getLeakSuspect(uint8_t ch) {
    if (sessionState != SessionState::IDLE) return false;
    float rate = sensorFlow_getRateLpm(ch);
    return (rate > 0.001f && rate < sessionFlowThresholdLpm);
}

bool sessionFlow_isEnabled() { return sessionEnabled; }

void sessionFlow_setEnabled(bool en) {
    sessionEnabled = en;
    if (!en) sessionState = SessionState::IDLE;
    Preferences prefs;
    prefs.begin("session", false);
    prefs.putBool("en", en);
    prefs.end();
    char msg[48];
    snprintf(msg, sizeof(msg), "[SESSION] tracking %s", en ? "enabled" : "disabled");
    storeSd_logEvent(msg);
    Serial.println(msg);
}

void sessionFlow_factoryReset() {
    sessionState         = SessionState::IDLE;
    sessionId            = 0;
    lastSessionId        = 0;
    lastSessionStartTs   = 0;
    lastSessionEndTs     = 0;
    lastSessionDurationS = 0;
    lastSessionVolumeOut = 0.0f;
    lastSessionVolumeIn  = 0.0f;
    lastSessionPeakOut   = 0.0f;
    lastSessionPeakIn    = 0.0f;
    recentCount          = 0;
    recentHead           = 0;
    Preferences prefs;
    prefs.begin("session", false);
    prefs.clear();
    prefs.end();
    sessionEnabled          = true;
    sessionIdleTimeoutMs    = SESSION_IDLE_TIMEOUT_MS;
    sessionFlowThresholdLpm = FLOW_ACTIVE_THRESHOLD_LPM;
    publishRecentSessions();   // publish empty array
    saveRecentSessionsToSd();
    storeSd_logEvent("[SESSION] factory reset — session data cleared");
    Serial.println("[SESSION] factory reset — session data cleared");
}

uint32_t sessionFlow_getLastId()        { return lastSessionId; }
uint32_t sessionFlow_getLastStartTs()   { return lastSessionStartTs; }
uint32_t sessionFlow_getLastEndTs()     { return lastSessionEndTs; }
uint32_t sessionFlow_getLastDurationS() { return lastSessionDurationS; }
float    sessionFlow_getLastVolumeOut() { return lastSessionVolumeOut; }
float    sessionFlow_getLastVolumeIn()  { return lastSessionVolumeIn; }
float    sessionFlow_getLastPeakOut()   { return lastSessionPeakOut; }
float    sessionFlow_getLastPeakIn()    { return lastSessionPeakIn; }
