# Session Tracking, HA Sub-Device Cards, Flow Reset — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add 90-second session tracking for the RO tap, reorganise HA entities into separate device cards per sensor group, and expose per-channel and combined flow reset commands via MQTT and serial.

**Architecture:** New `session_flow` driver module (state machine + NVS session_id + SD CSV + MQTT publish) follows existing `_begin()`/`_loop()` pattern. HA sub-devices created via `via_device` links in discovery payloads — no HA config file changes needed, only updated MQTT payloads. Reset commands added to existing `handleCmd()` and the serial console parser. Session volume calculated from total-at-start subtraction, not a separate pulse accumulator.

**Tech Stack:** PlatformIO + Arduino framework, ESP32-S3, ArduinoJson v7, Preferences (NVS), SdFat, MQTT TLS port 8883.

**Spec:** `docs/superpowers/specs/2026-04-27-session-tracking-ha-cards-reset-design.md`

---

## Task 1: Foundation — config constants + SD append helper

**Files:**
- Modify: `include/config.h`
- Modify: `src/store_sd.h`
- Modify: `src/store_sd.cpp`

- [ ] **Step 1: Add constants to config.h**

Add after the existing `#define` block (after `NVS_FLOW_SAVE_INTERVAL_MS`):

```cpp
#define TOPIC_SESSION               "twwp/" NODE_ID "/session"
#define SD_SESSION_LOG_PATH         "/log/sessions.csv"
#define SESSION_IDLE_TIMEOUT_MS     90000UL
#define FLOW_ACTIVE_THRESHOLD_LPM   0.05f
```

- [ ] **Step 2: Declare storeSd_appendCsvRow in store_sd.h**

Add after the `storeSd_logDataRow` line:

```cpp
bool storeSd_appendCsvRow(const char* path, const char* row, const char* header);
```

- [ ] **Step 3: Implement storeSd_appendCsvRow in store_sd.cpp**

Add directly after the closing `}` of `storeSd_logDataRow` (around line 597):

```cpp
bool storeSd_appendCsvRow(const char* path, const char* row, const char* header) {
    if (!sdReady) return false;
    bool isNew = !sd.exists(path);
    FsFile file;
    if (!file.open(path, FILE_WRITE)) {
        reportSdFailure("session");
        return false;
    }
    if (isNew && header && header[0]) {
        file.println(header);
    }
    file.println(row);
    file.close();
    return true;
}
```

- [ ] **Step 4: Build to confirm no errors**

```bash
cd "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1"
/home/kenny/.platformio/penv/bin/pio run 2>&1 | tail -5
```

Expected: `SUCCESS`

- [ ] **Step 5: Commit**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add include/config.h src/store_sd.h src/store_sd.cpp
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "feat: add session/reset config constants and storeSd_appendCsvRow helper"
```

---

## Task 2: Flow reset API

**Files:**
- Modify: `src/sensor_flow.h`
- Modify: `src/sensor_flow.cpp`

- [ ] **Step 1: Add declarations to sensor_flow.h**

Add after the `sensorFlow_setKFactor` line:

```cpp
void sensorFlow_resetToday(uint8_t ch);   // ch=1, ch=2, or ch=0 for both
void sensorFlow_resetTotals(uint8_t ch);  // ch=1, ch=2, or ch=0 for both — clears NVS too
```

- [ ] **Step 2: Implement sensorFlow_resetToday in sensor_flow.cpp**

Add after `sensorFlow_setKFactor` (after line 272):

```cpp
void sensorFlow_resetToday(uint8_t ch) {
    if (ch == 0 || ch == 1) {
        flowToday1 = 0.0f; flowWeek1 = 0.0f; flowMonth1 = 0.0f; flowYear1 = 0.0f;
    }
    if (ch == 0 || ch == 2) {
        flowToday2 = 0.0f; flowWeek2 = 0.0f; flowMonth2 = 0.0f; flowYear2 = 0.0f;
    }
    lastSdSavedTotal1 = -1.0f;  // force SD save on next interval
    lastSdSavedTotal2 = -1.0f;
    saveToSd();
    char msg[48];
    snprintf(msg, sizeof(msg), "[FLOW] reset today ch=%d", ch);
    storeSd_logEvent(msg);
    Serial.println(msg);
}
```

- [ ] **Step 3: Implement sensorFlow_resetTotals in sensor_flow.cpp**

Add directly after `sensorFlow_resetToday`:

```cpp
void sensorFlow_resetTotals(uint8_t ch) {
    if (ch == 0 || ch == 1) {
        flowTotal1 = 0.0f; flowToday1 = 0.0f; flowWeek1 = 0.0f;
        flowMonth1 = 0.0f; flowYear1  = 0.0f;
    }
    if (ch == 0 || ch == 2) {
        flowTotal2 = 0.0f; flowToday2 = 0.0f; flowWeek2 = 0.0f;
        flowMonth2 = 0.0f; flowYear2  = 0.0f;
    }
    // Clear NVS for affected channels
    prefs.begin("flow", false);
    if (ch == 0 || ch == 1) prefs.putFloat("t1", 0.0f);
    if (ch == 0 || ch == 2) prefs.putFloat("t2", 0.0f);
    prefs.end();
    lastNvsSavedTotal1 = 0.0f;
    lastNvsSavedTotal2 = 0.0f;
    lastSdSavedTotal1  = -1.0f;  // force SD save
    lastSdSavedTotal2  = -1.0f;
    saveToSd();
    char msg[48];
    snprintf(msg, sizeof(msg), "[FLOW] reset totals ch=%d", ch);
    storeSd_logEvent(msg);
    Serial.println(msg);
}
```

- [ ] **Step 4: Build**

```bash
/home/kenny/.platformio/penv/bin/pio run 2>&1 | tail -5
```

Expected: `SUCCESS`

- [ ] **Step 5: Commit**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add src/sensor_flow.h src/sensor_flow.cpp
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "feat: add sensorFlow_resetToday and sensorFlow_resetTotals"
```

---

## Task 3: session_flow module

**Files:**
- Create: `src/session_flow.h`
- Create: `src/session_flow.cpp`

- [ ] **Step 1: Create session_flow.h**

```cpp
#pragma once
#include <Arduino.h>

bool     sessionFlow_begin();
void     sessionFlow_loop();

// Getters for last completed session — used in status payload and HA discovery
uint32_t sessionFlow_getLastId();
uint32_t sessionFlow_getLastStartTs();
uint32_t sessionFlow_getLastEndTs();
uint32_t sessionFlow_getLastDurationS();
float    sessionFlow_getLastVolumeOut();   // sensor 1 — RO purified output (L)
float    sessionFlow_getLastVolumeIn();    // sensor 2 — RO total input (L)
float    sessionFlow_getLastPeakOut();     // peak L/min on channel 1
float    sessionFlow_getLastPeakIn();      // peak L/min on channel 2
```

- [ ] **Step 2: Create session_flow.cpp**

```cpp
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
```

- [ ] **Step 3: Build**

```bash
/home/kenny/.platformio/penv/bin/pio run 2>&1 | tail -5
```

Expected: `SUCCESS` (session_flow is not wired into main.cpp yet — that's Task 7)

- [ ] **Step 4: Commit**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add src/session_flow.h src/session_flow.cpp
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "feat: add session_flow module with 90s idle timeout, MQTT publish, SD log"
```

---

## Task 4: HA sub-device card reorganisation

**Files:**
- Modify: `src/main.cpp`

This task adds `fillHaSubDevice()`, updates `publishHaFlowSensor()` to accept sub-device parameters, updates `publishHaDiscovery()` (leak) and `publishHaDiscoveryFlow()` to use sub-devices. Diagnostics stay on the main node card.

- [ ] **Step 1: Add fillHaSubDevice helper in main.cpp**

Add directly after the closing `}` of `fillHaDevice` (after line 185):

```cpp
static void fillHaSubDevice(JsonDocument& doc, const char* subId, const char* subName) {
    JsonObject device = doc["device"].to<JsonObject>();
    JsonArray identifiers = device["identifiers"].to<JsonArray>();
    identifiers.add(subId);
    device["name"]         = subName;
    device["manufacturer"] = "TWWP";
    device["model"]        = "Waveshare ESP32-S3 RS485 CAN";
    device["sw_version"]   = NODE_FIRMWARE_VERSION;
    device["via_device"]   = "twwp_" NODE_ID;
}
```

- [ ] **Step 2: Update publishHaFlowSensor signature to accept sub-device**

Replace the existing `publishHaFlowSensor` function (lines 187–216) with:

```cpp
static bool publishHaFlowSensor(const char* uid, const char* name, const char* valueKey,
                                 const char* unit, const char* deviceClass,
                                 const char* stateClass,
                                 const char* subId, const char* subName) {
    JsonDocument doc;
    doc["name"]       = name;
    doc["unique_id"]  = uid;
    doc["object_id"]  = uid;
    doc["state_topic"] = TOPIC_STATUS;
    char tmpl[64];
    snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", valueKey);
    doc["value_template"]      = tmpl;
    doc["unit_of_measurement"] = unit;
    if (deviceClass && deviceClass[0]) {
        doc["device_class"] = deviceClass;
    }
    doc["state_class"]          = stateClass;
    doc["availability_topic"]   = TOPIC_LWT;
    doc["payload_available"]    = "online";
    doc["payload_not_available"] = "offline";
    fillHaSubDevice(doc, subId, subName);

    char payload[768];
    if (!serializeDoc(doc, payload, sizeof(payload))) {
        Serial.println("[MQTT] flow HA discovery JSON too large");
        return false;
    }
    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/config", uid);
    return netMqtt_publish(topic, payload, true);
}
```

- [ ] **Step 3: Update publishHaDiscoveryFlow to pass sub-device ids and update K factor lambda**

Replace the entire `publishHaDiscoveryFlow` function body with:

```cpp
static bool publishHaDiscoveryFlow() {
    bool ok = true;

    // Channel 1 — RO Output
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_rate_1",  "Flow Rate",   "flow_rate_1",  "L/min", "volume_flow_rate", "measurement",    "twwp_" NODE_ID "_flow1", "RO Output");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_total_1", "Flow Total",  "flow_total_1", "L",     "water",            "total_increasing","twwp_" NODE_ID "_flow1", "RO Output");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_today_1", "Flow Today",  "flow_today_1", "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow1", "RO Output");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_week_1",  "Flow Week",   "flow_week_1",  "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow1", "RO Output");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_month_1", "Flow Month",  "flow_month_1", "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow1", "RO Output");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_year_1",  "Flow Year",   "flow_year_1",  "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow1", "RO Output");

    // Channel 2 — RO Input
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_rate_2",  "Flow Rate",   "flow_rate_2",  "L/min", "volume_flow_rate", "measurement",    "twwp_" NODE_ID "_flow2", "RO Input");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_total_2", "Flow Total",  "flow_total_2", "L",     "water",            "total_increasing","twwp_" NODE_ID "_flow2", "RO Input");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_today_2", "Flow Today",  "flow_today_2", "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow2", "RO Input");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_week_2",  "Flow Week",   "flow_week_2",  "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow2", "RO Input");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_month_2", "Flow Month",  "flow_month_2", "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow2", "RO Input");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_year_2",  "Flow Year",   "flow_year_2",  "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow2", "RO Input");

    // Clear any old sensor discovery entries for K factors (published before number entities existed)
    netMqtt_publish("homeassistant/sensor/twwp_" NODE_ID "_k_factor_1/config", "", true);
    netMqtt_publish("homeassistant/sensor/twwp_" NODE_ID "_k_factor_2/config", "", true);

    // K factor writable number entities
    auto publishKFactorNumber = [&ok](const char* uid, const char* name,
                                      const char* valueKey, const char* cmdKey,
                                      const char* subId, const char* subName) {
        JsonDocument doc;
        doc["name"]       = name;
        doc["unique_id"]  = uid;
        doc["object_id"]  = uid;
        doc["state_topic"] = TOPIC_STATUS;
        char tmpl[64];
        snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s | int }}", valueKey);
        doc["value_template"]      = tmpl;
        doc["command_topic"]       = TOPIC_CMD;
        char cmdTmpl[64];
        snprintf(cmdTmpl, sizeof(cmdTmpl), "{\"%s\": {{ value | int }}}", cmdKey);
        doc["command_template"]    = cmdTmpl;
        doc["unit_of_measurement"] = "pulses/L";
        doc["min"]                 = 1;
        doc["max"]                 = 9999;
        doc["step"]                = 1;
        doc["mode"]                = "box";
        doc["entity_category"]     = "config";
        doc["availability_topic"]   = TOPIC_LWT;
        doc["payload_available"]    = "online";
        doc["payload_not_available"] = "offline";
        fillHaSubDevice(doc, subId, subName);

        char payload[768];
        if (!serializeDoc(doc, payload, sizeof(payload))) {
            Serial.println("[MQTT] K factor number JSON too large");
            ok = false;
            return;
        }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/number/%s/config", uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    };

    publishKFactorNumber("twwp_" NODE_ID "_k_factor_1", "K Factor",
                         "k_factor_1", "set_k_factor_1",
                         "twwp_" NODE_ID "_flow1", "RO Output");
    publishKFactorNumber("twwp_" NODE_ID "_k_factor_2", "K Factor",
                         "k_factor_2", "set_k_factor_2",
                         "twwp_" NODE_ID "_flow2", "RO Input");

    Serial.print("[MQTT] HA flow discovery ");
    Serial.println(ok ? "published" : "partial");
    return ok;
}
```

- [ ] **Step 4: Update publishHaDiscovery (leak) to use its own sub-device**

Replace `fillHaDevice(doc);` in `publishHaDiscovery()` with:

```cpp
    fillHaSubDevice(doc, "twwp_" NODE_ID "_leak", "Leak Sensor");
```

- [ ] **Step 5: Build**

```bash
/home/kenny/.platformio/penv/bin/pio run 2>&1 | tail -5
```

Expected: `SUCCESS`

- [ ] **Step 6: Commit**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add src/main.cpp
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "feat: reorganise HA discovery into sub-device cards per sensor group"
```

---

## Task 5: Session status fields + session HA discovery

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Add session_flow include and last-session fields to publishM0Status**

At the top of main.cpp, add after the `#include "sensor_flow.h"` line:

```cpp
#include "session_flow.h"
```

In `publishM0Status()`, add these lines after the `k_factor_2` line and before the `char payload[768]` line:

```cpp
    doc["session_last_id"]       = sessionFlow_getLastId();
    doc["session_last_start_ts"] = sessionFlow_getLastStartTs();
    doc["session_last_end_ts"]   = sessionFlow_getLastEndTs();
    doc["session_last_dur_s"]    = sessionFlow_getLastDurationS();
    doc["session_last_vol_out"]  = sessionFlow_getLastVolumeOut();
    doc["session_last_vol_in"]   = sessionFlow_getLastVolumeIn();
```

Change the payload buffer size from 768 to 1024:

```cpp
    char payload[1024];
```

- [ ] **Step 2: Add publishHaDiscoverySession function**

Add after the closing `}` of `publishHaDiscoveryDiagnostics`:

```cpp
static bool publishHaDiscoverySession() {
    bool ok = true;

    auto pub = [&ok](const char* uid, const char* name, const char* valueKey,
                     const char* unit, const char* stateClass) {
        JsonDocument doc;
        doc["name"]             = name;
        doc["unique_id"]        = uid;
        doc["object_id"]        = uid;
        doc["entity_category"]  = "diagnostic";
        doc["state_topic"]      = TOPIC_STATUS;
        char tmpl[80];
        snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", valueKey);
        doc["value_template"]      = tmpl;
        if (unit && unit[0])        doc["unit_of_measurement"] = unit;
        if (stateClass && stateClass[0]) doc["state_class"] = stateClass;
        doc["availability_topic"]   = TOPIC_LWT;
        doc["payload_available"]    = "online";
        doc["payload_not_available"] = "offline";
        fillHaDevice(doc);
        char payload[512];
        if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; return; }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/config", uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    };

    pub("twwp_" NODE_ID "_session_last_id",      "Last Session ID",          "session_last_id",       "",  "");
    pub("twwp_" NODE_ID "_session_last_dur_s",   "Last Session Duration",    "session_last_dur_s",    "s", "measurement");
    pub("twwp_" NODE_ID "_session_last_vol_out", "Last Session Volume Out",  "session_last_vol_out",  "L", "measurement");
    pub("twwp_" NODE_ID "_session_last_vol_in",  "Last Session Volume In",   "session_last_vol_in",   "L", "measurement");

    Serial.print("[MQTT] HA session discovery ");
    Serial.println(ok ? "published" : "partial");
    return ok;
}
```

- [ ] **Step 3: Build**

```bash
/home/kenny/.platformio/penv/bin/pio run 2>&1 | tail -5
```

Expected: `SUCCESS`

- [ ] **Step 4: Commit**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add src/main.cpp
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "feat: add last session fields to status payload and HA session discovery"
```

---

## Task 6: Reset button HA discovery + cmd handling + serial commands + wiring

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Add publishHaDiscoveryResetButtons function**

Add after the closing `}` of `publishHaDiscoverySession`:

```cpp
static bool publishHaDiscoveryResetButtons() {
    bool ok = true;

    auto pubButton = [&ok](const char* uid, const char* name, const char* payload_press,
                            const char* subId, const char* subName) {
        JsonDocument doc;
        doc["name"]              = name;
        doc["unique_id"]         = uid;
        doc["object_id"]         = uid;
        doc["entity_category"]   = "config";
        doc["command_topic"]     = TOPIC_CMD;
        doc["payload_press"]     = payload_press;
        doc["availability_topic"]  = TOPIC_LWT;
        doc["payload_available"]   = "online";
        doc["payload_not_available"] = "offline";
        if (subId && subId[0]) {
            fillHaSubDevice(doc, subId, subName);
        } else {
            fillHaDevice(doc);
        }
        char payload[512];
        if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; return; }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/button/%s/config", uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    };

    // Per-channel reset today — on sub-device cards
    pubButton("twwp_" NODE_ID "_reset_today_1", "Reset Today",
              "{\"reset_flow_today_1\": true}",
              "twwp_" NODE_ID "_flow1", "RO Output");
    pubButton("twwp_" NODE_ID "_reset_today_2", "Reset Today",
              "{\"reset_flow_today_2\": true}",
              "twwp_" NODE_ID "_flow2", "RO Input");

    // Per-channel reset totals — on sub-device cards
    pubButton("twwp_" NODE_ID "_reset_totals_1", "Reset Totals",
              "{\"reset_flow_totals_1\": true}",
              "twwp_" NODE_ID "_flow1", "RO Output");
    pubButton("twwp_" NODE_ID "_reset_totals_2", "Reset Totals",
              "{\"reset_flow_totals_2\": true}",
              "twwp_" NODE_ID "_flow2", "RO Input");

    // Both-channel resets — on main node card
    pubButton("twwp_" NODE_ID "_reset_today_all",  "Reset Today (Both)",
              "{\"reset_flow_today\": true}", nullptr, nullptr);
    pubButton("twwp_" NODE_ID "_reset_totals_all", "Reset Totals (Both)",
              "{\"reset_flow_totals\": true}", nullptr, nullptr);

    Serial.print("[MQTT] HA reset button discovery ");
    Serial.println(ok ? "published" : "partial");
    return ok;
}
```

- [ ] **Step 2: Extend handleCmd with reset commands**

Replace the entire `handleCmd` function with:

```cpp
static void handleCmd(const char* payload) {
    JsonDocument doc;
    if (deserializeJson(doc, payload)) {
        Serial.println("[CMD] invalid JSON");
        return;
    }

    if (!doc["set_k_factor_1"].isNull()) {
        sensorFlow_setKFactor(1, doc["set_k_factor_1"].as<float>());
    }
    if (!doc["set_k_factor_2"].isNull()) {
        sensorFlow_setKFactor(2, doc["set_k_factor_2"].as<float>());
    }
    if (doc["restart_wifi"] | false) {
        storeSd_logEvent("[CMD] restart_wifi received");
        netWifi_reconnect();
    }
    if (doc["reset_flow_today_1"] | false) sensorFlow_resetToday(1);
    if (doc["reset_flow_today_2"] | false) sensorFlow_resetToday(2);
    if (doc["reset_flow_today"]   | false) sensorFlow_resetToday(0);
    if (doc["reset_flow_totals_1"] | false) sensorFlow_resetTotals(1);
    if (doc["reset_flow_totals_2"] | false) sensorFlow_resetTotals(2);
    if (doc["reset_flow_totals"]   | false) sensorFlow_resetTotals(0);
}
```

- [ ] **Step 3: Add reset serial commands to serviceSerialConsole**

In `serviceSerialConsole()`, replace the `} else if (strcmp(cmd, "help") == 0) {` block with:

```cpp
                } else if (strcmp(cmd, "help") == 0) {
                    Serial.println("[SERIAL] SD commands: sdls [path], sdcat <path>, sdrm <path>, sdinfo, sdprune");
                    Serial.println("[SERIAL] Flow reset: reset_flow_today, reset_flow_today_1, reset_flow_today_2");
                    Serial.println("[SERIAL]             reset_flow_totals, reset_flow_totals_1, reset_flow_totals_2");
                } else if (strcmp(cmd, "reset_flow_today") == 0) {
                    sensorFlow_resetToday(0);
                } else if (strcmp(cmd, "reset_flow_today_1") == 0) {
                    sensorFlow_resetToday(1);
                } else if (strcmp(cmd, "reset_flow_today_2") == 0) {
                    sensorFlow_resetToday(2);
                } else if (strcmp(cmd, "reset_flow_totals") == 0) {
                    sensorFlow_resetTotals(0);
                } else if (strcmp(cmd, "reset_flow_totals_1") == 0) {
                    sensorFlow_resetTotals(1);
                } else if (strcmp(cmd, "reset_flow_totals_2") == 0) {
                    sensorFlow_resetTotals(2);
```

Also update the inline help string on the `wasConnected` branch (line 103) to match:

```cpp
        Serial.println("[SERIAL] commands: sdls [path], sdcat <path>, sdrm <path>, sdinfo, sdprune, reset_flow_today[_1/_2], reset_flow_totals[_1/_2]");
```

- [ ] **Step 4: Wire session_flow into setup(), loop(), and publishOnlineState()**

In `setup()`, add after `sensorFlow_begin();`:

```cpp
    sessionFlow_begin();
```

In `loop()`, add after `sensorFlow_loop();`:

```cpp
    sessionFlow_loop();
```

In `publishOnlineState()`, add after the existing `publishHaDiscoveryDiagnostics();` call:

```cpp
    publishHaDiscoverySession();
    publishHaDiscoveryResetButtons();
```

- [ ] **Step 5: Build**

```bash
/home/kenny/.platformio/penv/bin/pio run 2>&1 | tail -5
```

Expected: `SUCCESS`

- [ ] **Step 6: Commit**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add src/main.cpp
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "feat: add reset buttons/cmds/serial and wire session_flow into main loop"
```

---

## Task 7: Documentation

**Files:**
- Modify: `docs/MQTT_TOPIC_MAP.md`
- Modify: `docs/USER_OPERATIONS.md`

- [ ] **Step 1: Add session topic to MQTT_TOPIC_MAP.md**

In the Data topics table, add after the `twwp/<id>/lwt` row:

```markdown
| `twwp/<id>/session` | no | node → broker | Session-end event. Published when the 90 s idle timeout expires. JSON payload with session_id, start_ts, end_ts, duration_s, volume_out_L, volume_in_L, peak_rate_out, peak_rate_in. |
```

Add reset commands to the Command topic payload format table:

```markdown
| `reset_flow_today_1` | bool | Zero today/week/month/year subtotals for channel 1. Saved to SD. |
| `reset_flow_today_2` | bool | Zero today/week/month/year subtotals for channel 2. Saved to SD. |
| `reset_flow_today` | bool | Zero today/week/month/year subtotals for both channels. |
| `reset_flow_totals_1` | bool | Zero lifetime total + all subtotals for channel 1. Clears NVS + SD. |
| `reset_flow_totals_2` | bool | Zero lifetime total + all subtotals for channel 2. Clears NVS + SD. |
| `reset_flow_totals` | bool | Zero all flow data for both channels. Clears NVS + SD. |
```

- [ ] **Step 2: Update USER_OPERATIONS.md**

Add a **Session Tracking** section after the Flow Sensor Total Persistence section:

```markdown
## Session Tracking

A "session" is a continuous usage event at the RO tap. A session starts when flow on either sensor exceeds 0.05 L/min and ends 90 seconds after flow stops on both sensors. If flow resumes within the 90-second window, it extends the same session.

When a session ends, the node publishes to `twwp/<node_id>/session` and appends a row to:

\```
/log/sessions.csv
\```

Session log columns:

\```
session_id,start_ts,end_ts,duration_s,volume_out_L,volume_in_L,peak_rate_out,peak_rate_in
\```

View via serial:

\```
sdcat /log/sessions.csv
\```

The last session fields also appear in the heartbeat status (`session_last_id`, `session_last_dur_s`, `session_last_vol_out`, `session_last_vol_in`) and are visible as diagnostic sensors on the main TWWP device card in HA.

Session IDs survive reboots (persisted in NVS).
```

Add a **Flow Reset Commands** section after Session Tracking:

```markdown
## Flow Reset Commands

Reset commands are available via MQTT (`twwp/<node_id>/cmd`), HA device card buttons, and serial console.

| Command | Effect |
|---|---|
| `reset_flow_today_1` | Zero today/week/month/year for RO Output (channel 1). Saved to SD. |
| `reset_flow_today_2` | Zero today/week/month/year for RO Input (channel 2). Saved to SD. |
| `reset_flow_today` | Zero period subtotals for both channels. Saved to SD. |
| `reset_flow_totals_1` | Zero lifetime total + all subtotals for channel 1. Clears NVS + SD. |
| `reset_flow_totals_2` | Zero lifetime total + all subtotals for channel 2. Clears NVS + SD. |
| `reset_flow_totals` | Zero all flow data for both channels. Clears NVS + SD. |

Via serial console:

\```
reset_flow_today
reset_flow_today_1
reset_flow_today_2
reset_flow_totals
reset_flow_totals_1
reset_flow_totals_2
\```

Via MQTT (example):

\```json
{"reset_flow_today_1": true}
\```

HA buttons: "Reset Today" and "Reset Totals" appear on the RO Output and RO Input device cards. "Reset Today (Both)" and "Reset Totals (Both)" appear on the main TWWP node card.
```

- [ ] **Step 3: Commit docs**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add docs/MQTT_TOPIC_MAP.md docs/USER_OPERATIONS.md
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "docs: document session tracking, reset commands, and updated MQTT topics"
```

---

## Self-Review Checklist

- [x] All spec sections have corresponding tasks (session tracking → Task 3+5, HA cards → Task 4, resets → Task 2+6)
- [x] No TBDs or placeholders in code steps
- [x] `fillHaSubDevice` defined before its first use in `publishHaFlowSensor`
- [x] `session_flow.h` include added before its getters are called in `publishM0Status`
- [x] `sensorFlow_resetToday`/`sensorFlow_resetTotals` ch=0 path resets both channels
- [x] `lastNvsSavedTotal` and `lastSdSavedTotal` cleared in `resetTotals` to force persistence save
- [x] `TOPIC_SESSION` added to config.h (Task 1) before used in session_flow.cpp (Task 3)
- [x] Status payload buffer expanded from 768 → 1024 to accommodate session fields
- [x] `publishHaDiscoverySession` and `publishHaDiscoveryResetButtons` called from `publishOnlineState`
- [x] Session SD log path `/log/sessions.csv` is inside `/log` — protected from `sdrm /log` but not `sdrm /log/sessions.csv` (correct)
