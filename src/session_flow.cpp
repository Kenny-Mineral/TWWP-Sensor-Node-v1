#include "session_flow.h"

#include <ArduinoJson.h>
#include <Preferences.h>

#include "config.h"
#include "net_mqtt.h"
#include "sensor_flow.h"
#include "sensor_yieryi.h"
#include "sensor_tds_meter.h"
#include "store_sd.h"
#include "time_rtc.h"

static const char* SESSION_LOG_HEADER =
    "session_id,start_ts,end_ts,duration_s,flow_duration_s,idle_time_s,"
    "volume_out_L,volume_in_L,peak_rate_out,peak_rate_in,"
    "wq_ph,wq_orp,wq_ec,wq_tds_ppm,tds_pre_ppm,tds_post_ppm,"
    "user_id";

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

// Flow-time tracking within a session
static unsigned long flowSegmentStartMs = 0; // when the current flow segment started
static unsigned long flowDurationMs     = 0; // accumulated flow time across all segments

// Last completed session
static uint32_t lastSessionId            = 0;
static uint32_t lastSessionStartTs       = 0;
static uint32_t lastSessionEndTs         = 0;
static uint32_t lastSessionDurationS     = 0;
static uint32_t lastSessionFlowDurationS = 0;
static uint32_t lastSessionIdleTimeS     = 0;
static float    lastSessionVolumeOut     = 0.0f;
static float    lastSessionVolumeIn      = 0.0f;
static float    lastSessionPeakOut       = 0.0f;
static float    lastSessionPeakIn        = 0.0f;

// Ring buffer of last SESSIONS_RECENT_MAX sessions (oldest-first, recentHead = oldest slot)
struct SessionRecord {
    uint32_t id;
    uint32_t startTs;
    uint32_t endTs;
    uint32_t durationS;
    uint32_t flowDurS;
    uint32_t idleTimeS;
    float    volOut;
    float    volIn;
    float    peakOut;
    float    peakIn;
    // Water quality snapshot at session end (NaN = sensor unavailable)
    float    wqPh;
    int16_t  wqOrpMv;
    float    wqEcUsCm;
    float    wqTdsPpm;
    float    tdsPrePpm;
    float    tdsPostPpm;
    uint32_t userId;   // 0 = anonymous; populated by TapLock / app in future
};

static SessionRecord recentSessions[SESSIONS_RECENT_MAX];
static uint8_t       recentCount = 0;
static uint8_t       recentHead  = 0;

// ── Ring buffer helpers ───────────────────────────────────────────────────────

static void pushRecentSession(uint32_t id, uint32_t startTs, uint32_t endTs,
                               uint32_t durationS, uint32_t flowDurS, uint32_t idleTimeS,
                               float volOut, float volIn, float peakOut, float peakIn,
                               float wqPh, int16_t wqOrpMv, float wqEcUsCm, float wqTdsPpm,
                               float tdsPrePpm, float tdsPostPpm, uint32_t userId) {
    uint8_t slot;
    if (recentCount < SESSIONS_RECENT_MAX) {
        slot = (recentHead + recentCount) % SESSIONS_RECENT_MAX;
        recentCount++;
    } else {
        slot       = recentHead;
        recentHead = (recentHead + 1) % SESSIONS_RECENT_MAX;
    }
    recentSessions[slot] = { id, startTs, endTs, durationS, flowDurS, idleTimeS,
                              volOut, volIn, peakOut, peakIn,
                              wqPh, wqOrpMv, wqEcUsCm, wqTdsPpm,
                              tdsPrePpm, tdsPostPpm, userId };
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
        s["fds"] = recentSessions[slot].flowDurS;
        s["its"] = recentSessions[slot].idleTimeS;
        s["vo"]  = recentSessions[slot].volOut;
        s["vi"]  = recentSessions[slot].volIn;
        s["po"]  = recentSessions[slot].peakOut;
        s["pi"]  = recentSessions[slot].peakIn;
        if (!isnan(recentSessions[slot].wqPh))      s["wq_ph"]  = recentSessions[slot].wqPh;
        if (recentSessions[slot].wqOrpMv != -32768) s["wq_orp"] = recentSessions[slot].wqOrpMv;
        if (!isnan(recentSessions[slot].wqEcUsCm))  s["wq_ec"]  = recentSessions[slot].wqEcUsCm;
        if (!isnan(recentSessions[slot].wqTdsPpm))  s["wq_tds"] = recentSessions[slot].wqTdsPpm;
        if (!isnan(recentSessions[slot].tdsPrePpm))  s["tds_pre"]  = recentSessions[slot].tdsPrePpm;
        if (!isnan(recentSessions[slot].tdsPostPpm)) s["tds_post"] = recentSessions[slot].tdsPostPpm;
        s["uid"] = recentSessions[slot].userId;
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
        float loadedPh  = s["wq_ph"].isNull()  ? NAN : s["wq_ph"].as<float>();
        int16_t loadedOrp = s["wq_orp"].isNull() ? -32768 : (int16_t)s["wq_orp"].as<int>();
        float loadedEc  = s["wq_ec"].isNull()  ? NAN : s["wq_ec"].as<float>();
        float loadedTds = s["wq_tds"].isNull() ? NAN : s["wq_tds"].as<float>();
        float loadedPre = s["tds_pre"].isNull()  ? NAN : s["tds_pre"].as<float>();
        float loadedPost= s["tds_post"].isNull() ? NAN : s["tds_post"].as<float>();
        recentSessions[recentCount++] = {
            s["id"]  | 0u,
            s["sts"] | 0u,
            s["ets"] | 0u,
            s["dur"] | 0u,
            s["fds"] | 0u,
            s["its"] | 0u,
            s["vo"]  | 0.0f,
            s["vi"]  | 0.0f,
            s["po"]  | 0.0f,
            s["pi"]  | 0.0f,
            loadedPh, loadedOrp, loadedEc, loadedTds,
            loadedPre, loadedPost,
            s["uid"] | 0u
        };
    }
    Serial.printf("[SESSION] loaded %d recent sessions from SD\n", recentCount);
}

// ── MQTT publish ──────────────────────────────────────────────────────────────

static void publishRecentSessions() {
    // ~250 bytes per session × 10 + overhead (with WQ fields)
    static char buf[4096];
    JsonDocument doc;
    JsonArray arr = doc["sessions"].to<JsonArray>();
    // Newest-first for display
    for (int8_t i = (int8_t)recentCount - 1; i >= 0; i--) {
        uint8_t slot = (recentHead + (uint8_t)i) % SESSIONS_RECENT_MAX;
        JsonObject s = arr.add<JsonObject>();
        s["id"]          = recentSessions[slot].id;
        s["start_ts"]    = recentSessions[slot].startTs;
        s["end_ts"]      = recentSessions[slot].endTs;
        s["dur_s"]       = recentSessions[slot].durationS;
        s["flow_dur_s"]  = recentSessions[slot].flowDurS;
        s["idle_s"]      = recentSessions[slot].idleTimeS;
        s["vol_out"]     = serialized(String(recentSessions[slot].volOut,  3));
        s["vol_in"]      = serialized(String(recentSessions[slot].volIn,   3));
        s["peak_out"]    = serialized(String(recentSessions[slot].peakOut, 3));
        s["peak_in"]     = serialized(String(recentSessions[slot].peakIn,  3));
        if (!isnan(recentSessions[slot].wqPh))      s["wq_ph"]   = serialized(String(recentSessions[slot].wqPh, 2));
        if (recentSessions[slot].wqOrpMv != -32768) s["wq_orp"]  = recentSessions[slot].wqOrpMv;
        if (!isnan(recentSessions[slot].wqEcUsCm))  s["wq_ec"]   = serialized(String(recentSessions[slot].wqEcUsCm, 0));
        if (!isnan(recentSessions[slot].wqTdsPpm))  s["wq_tds"]  = serialized(String(recentSessions[slot].wqTdsPpm, 0));
        if (!isnan(recentSessions[slot].tdsPrePpm))  s["tds_pre"]  = serialized(String(recentSessions[slot].tdsPrePpm, 0));
        if (!isnan(recentSessions[slot].tdsPostPpm)) s["tds_post"] = serialized(String(recentSessions[slot].tdsPostPpm, 0));
        s["user_id"]     = recentSessions[slot].userId;
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
    uint32_t endTs         = timeRtc_getUnixTime();
    uint32_t durationS     = endTs > sessionStartTs ? (endTs - sessionStartTs) : 0;
    uint32_t flowDurS      = (uint32_t)(flowDurationMs / 1000UL);
    uint32_t idleTimeS     = durationS > flowDurS ? durationS - flowDurS : 0;
    float    rawOut        = sensorFlow_getTotalL(1) - sessionStartTotal1;
    float    rawIn         = sensorFlow_getTotalL(2) - sessionStartTotal2;
    float    volOut        = rawOut > 0.0f ? rawOut : 0.0f;
    float    volIn         = rawIn  > 0.0f ? rawIn  : 0.0f;

    // Water quality snapshot at end of session (zone 3 = Remin = post-tap water)
    float   wqPh    = sensorYieryi_hasPh(YIERYI_ZONE_REMIN)   ? sensorYieryi_getPh(YIERYI_ZONE_REMIN)           : NAN;
    int16_t wqOrp   = sensorYieryi_hasOrp(YIERYI_ZONE_REMIN)  ? (int16_t)sensorYieryi_getOrpMv(YIERYI_ZONE_REMIN) : -32768;
    float   wqEc    = sensorYieryi_hasEc(YIERYI_ZONE_REMIN)    ? sensorYieryi_getEcUsCm(YIERYI_ZONE_REMIN)    : NAN;
    float   wqTds   = sensorYieryi_isOnline(YIERYI_ZONE_REMIN) ? sensorYieryi_getTdsPpm(YIERYI_ZONE_REMIN)    : NAN;
    float   tdsPre  = sensorTdsMeter_isOnline(TDS_ZONE_PRE_RO)  ? sensorTdsMeter_getTds(TDS_ZONE_PRE_RO)  : NAN;
    float   tdsPost = sensorTdsMeter_isOnline(TDS_ZONE_POST_RO) ? sensorTdsMeter_getTds(TDS_ZONE_POST_RO) : NAN;
    uint32_t userId = 0;  // 0 = anonymous; populated by TapLock / app integration

    lastSessionId            = sessionId;
    lastSessionStartTs       = sessionStartTs;
    lastSessionEndTs         = endTs;
    lastSessionDurationS     = durationS;
    lastSessionFlowDurationS = flowDurS;
    lastSessionIdleTimeS     = idleTimeS;
    lastSessionVolumeOut     = volOut;
    lastSessionVolumeIn      = volIn;
    lastSessionPeakOut       = sessionPeakOut;
    lastSessionPeakIn        = sessionPeakIn;

    // MQTT event — buffered so it survives offline periods
    JsonDocument doc;
    doc["session_id"]      = sessionId;
    doc["start_ts"]        = sessionStartTs;
    doc["end_ts"]          = endTs;
    doc["duration_s"]      = durationS;
    doc["flow_duration_s"] = flowDurS;
    doc["idle_time_s"]     = idleTimeS;
    doc["volume_out_L"]    = serialized(String(volOut,         3));
    doc["volume_in_L"]     = serialized(String(volIn,          3));
    doc["peak_rate_out"]   = serialized(String(sessionPeakOut, 3));
    doc["peak_rate_in"]    = serialized(String(sessionPeakIn,  3));
    doc["user_id"]         = userId;
    if (!isnan(wqPh))    doc["wq_ph"]   = serialized(String(wqPh, 2));
    if (wqOrp != -32768) doc["wq_orp"]  = wqOrp;
    if (!isnan(wqEc))    doc["wq_ec"]   = serialized(String(wqEc, 0));
    if (!isnan(wqTds))   doc["wq_tds"]  = serialized(String(wqTds, 0));
    if (!isnan(tdsPre))  doc["tds_pre"]  = serialized(String(tdsPre, 0));
    if (!isnan(tdsPost)) doc["tds_post"] = serialized(String(tdsPost, 0));
    char payload[512];
    if (serializeJson(doc, payload, sizeof(payload)) > 0) {
        netMqtt_publishSub(TOPIC_SESSION, payload);
    }

    // SD session log — WQ fields blank if sensor not available
    char wqPhStr[8], wqOrpStr[8], wqEcStr[8], wqTdsStr[8], tdsPreStr[8], tdsPostStr[8];
    auto fmtF = [](char* b, size_t n, float v, int dp) { if (isnan(v)) b[0]='\0'; else snprintf(b, n, "%.*f", dp, v); };
    auto fmtI = [](char* b, size_t n, int16_t v) { if (v == -32768) b[0]='\0'; else snprintf(b, n, "%d", v); };
    fmtF(wqPhStr,   sizeof(wqPhStr),   wqPh,   2);
    fmtI(wqOrpStr,  sizeof(wqOrpStr),  wqOrp);
    fmtF(wqEcStr,   sizeof(wqEcStr),   wqEc,   0);
    fmtF(wqTdsStr,  sizeof(wqTdsStr),  wqTds,  0);
    fmtF(tdsPreStr, sizeof(tdsPreStr), tdsPre,  0);
    fmtF(tdsPostStr,sizeof(tdsPostStr),tdsPost, 0);
    char row[256];
    snprintf(row, sizeof(row), "%lu,%lu,%lu,%lu,%lu,%lu,%.3f,%.3f,%.3f,%.3f,%s,%s,%s,%s,%s,%s,%lu",
             (unsigned long)sessionId,
             (unsigned long)sessionStartTs,
             (unsigned long)endTs,
             (unsigned long)durationS,
             (unsigned long)flowDurS,
             (unsigned long)idleTimeS,
             volOut, volIn,
             sessionPeakOut, sessionPeakIn,
             wqPhStr, wqOrpStr, wqEcStr, wqTdsStr,
             tdsPreStr, tdsPostStr,
             (unsigned long)userId);
    storeSd_appendCsvRow(SD_SESSION_LOG_PATH, row, SESSION_LOG_HEADER);

    Serial.printf("[SESSION] #%lu ended — out=%.3fL in=%.3fL dur=%lus flow=%lus idle=%lus\n",
                  (unsigned long)sessionId, volOut, volIn,
                  (unsigned long)durationS, (unsigned long)flowDurS, (unsigned long)idleTimeS);

    // Ring buffer + retained MQTT + SD snapshot
    pushRecentSession(sessionId, sessionStartTs, endTs, durationS, flowDurS, idleTimeS,
                      volOut, volIn, sessionPeakOut, sessionPeakIn,
                      wqPh, wqOrp, wqEc, wqTds, tdsPre, tdsPost, userId);
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
    sessionId               = prefs.getUInt("sid",      0);
    sessionEnabled          = prefs.getBool("en",       true);
    sessionIdleTimeoutMs    = (uint32_t)prefs.getUInt("idle_s",  90) * 1000UL;
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

    float rate1   = sensorFlow_getRateLpm(1);
    float rate2   = sensorFlow_getRateLpm(2);
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
                flowDurationMs     = 0;
                flowSegmentStartMs = millis();
                Serial.printf("[SESSION] #%lu started\n", (unsigned long)sessionId);
            }
            break;

        case SessionState::ACTIVE:
            if (rate1 > sessionPeakOut) sessionPeakOut = rate1;
            if (rate2 > sessionPeakIn)  sessionPeakIn  = rate2;
            if (!anyFlow) {
                // Accumulate this flow segment before starting idle wait
                flowDurationMs      += millis() - flowSegmentStartMs;
                sessionState         = SessionState::ENDING;
                sessionEndingStartMs = millis();
            }
            break;

        case SessionState::ENDING:
            if (anyFlow) {
                // Flow resumed — start a new segment
                flowSegmentStartMs = millis();
                sessionState       = SessionState::ACTIVE;
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
    sessionState             = SessionState::IDLE;
    sessionId                = 0;
    flowDurationMs           = 0;
    flowSegmentStartMs       = 0;
    lastSessionId            = 0;
    lastSessionStartTs       = 0;
    lastSessionEndTs         = 0;
    lastSessionDurationS     = 0;
    lastSessionFlowDurationS = 0;
    lastSessionIdleTimeS     = 0;
    lastSessionVolumeOut     = 0.0f;
    lastSessionVolumeIn      = 0.0f;
    lastSessionPeakOut       = 0.0f;
    lastSessionPeakIn        = 0.0f;
    recentCount              = 0;
    recentHead               = 0;
    Preferences prefs;
    prefs.begin("session", false);
    prefs.clear();
    prefs.end();
    sessionEnabled          = true;
    sessionIdleTimeoutMs    = SESSION_IDLE_TIMEOUT_MS;
    sessionFlowThresholdLpm = FLOW_ACTIVE_THRESHOLD_LPM;
    publishRecentSessions();
    saveRecentSessionsToSd();
    storeSd_logEvent("[SESSION] factory reset — session data cleared");
    Serial.println("[SESSION] factory reset — session data cleared");
}

uint32_t sessionFlow_getLastId()            { return lastSessionId; }
uint32_t sessionFlow_getLastStartTs()       { return lastSessionStartTs; }
uint32_t sessionFlow_getLastEndTs()         { return lastSessionEndTs; }
uint32_t sessionFlow_getLastDurationS()     { return lastSessionDurationS; }
uint32_t sessionFlow_getLastFlowDurationS() { return lastSessionFlowDurationS; }
uint32_t sessionFlow_getLastIdleTimeS()     { return lastSessionIdleTimeS; }
float    sessionFlow_getLastVolumeOut()     { return lastSessionVolumeOut; }
float    sessionFlow_getLastVolumeIn()      { return lastSessionVolumeIn; }
float    sessionFlow_getLastPeakOut()       { return lastSessionPeakOut; }
float    sessionFlow_getLastPeakIn()        { return lastSessionPeakIn; }

float sessionFlow_getCurrentVolumeOut() {
    if (sessionState == SessionState::IDLE) return 0.0f;
    float raw = sensorFlow_getTotalL(1) - sessionStartTotal1;
    return raw > 0.0f ? raw : 0.0f;
}
