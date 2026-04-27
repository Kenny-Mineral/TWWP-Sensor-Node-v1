#include "sensor_flow.h"

#include <ArduinoJson.h>
#include <Preferences.h>

#include "config.h"
#include "pins.h"
#include "store_sd.h"
#include "time_rtc.h"

static const uint32_t CALC_INTERVAL_MS = 1000UL;

static float kFactor1 = 38.0f;
static float kFactor2 = 38.0f;

static volatile uint32_t rawPulses1 = 0;
static volatile uint32_t rawPulses2 = 0;

static float flowRate1  = 0.0f;
static float flowRate2  = 0.0f;
static float flowTotal1 = 0.0f;
static float flowTotal2 = 0.0f;
static float flowToday1 = 0.0f;
static float flowToday2 = 0.0f;
static float flowWeek1  = 0.0f;
static float flowWeek2  = 0.0f;
static float flowMonth1 = 0.0f;
static float flowMonth2 = 0.0f;
static float flowYear1  = 0.0f;
static float flowYear2  = 0.0f;

static unsigned long lastCalcMs    = 0;
static unsigned long lastSdSaveMs  = 0;
static unsigned long lastNvsSaveMs = 0;

static float lastSdSavedTotal1  = -1.0f;
static float lastSdSavedTotal2  = -1.0f;
static float lastNvsSavedTotal1 = -1.0f;
static float lastNvsSavedTotal2 = -1.0f;

static int32_t lastUnixDay   = -1;
static int32_t lastMondayDay = -1;
static int      lastMonth    = -1;
static int      lastYear     = -1;

static Preferences prefs;

static void IRAM_ATTR isrFlow1() { rawPulses1 = rawPulses1 + 1; }
static void IRAM_ATTR isrFlow2() { rawPulses2 = rawPulses2 + 1; }

// Returns the unix day number of the Monday containing unixDay.
// Day 0 = 1970-01-01 (Thursday). Monday offset: (day + 3) % 7.
static int32_t mondayOf(int32_t unixDay) {
    return unixDay - (int32_t)((unixDay + 3) % 7);
}

static void loadConfig() {
    JsonDocument doc;
    if (storeSd_readJsonFile(SD_CONFIG_PATH, doc)) {
        float k1 = doc["flow"]["k_factor_1"] | 38.0f;
        float k2 = doc["flow"]["k_factor_2"] | 38.0f;
        if (k1 >= 1.0f) kFactor1 = k1;
        if (k2 >= 1.0f) kFactor2 = k2;
    }
    Serial.printf("[FLOW] K1=%.0f K2=%.0f\n", kFactor1, kFactor2);
}

static void loadTotalsFromSd() {
    JsonDocument doc;
    if (!storeSd_readJsonFile(SD_FLOW_TOTAL_PATH, doc)) {
        Serial.println("[FLOW] no SD totals, starting at 0");
        return;
    }

    flowTotal1      = doc["t1"] | 0.0f;
    flowTotal2      = doc["t2"] | 0.0f;
    lastSdSavedTotal1 = flowTotal1;
    lastSdSavedTotal2 = flowTotal2;

    // Restore period subtotals only if saved date matches today
    const char* savedDate = doc["date"] | "";
    String today = timeRtc_getDateString();
    if (today.length() == 10 && strcmp(savedDate, today.c_str()) == 0) {
        flowToday1 = doc["today1"] | 0.0f;
        flowToday2 = doc["today2"] | 0.0f;
        flowWeek1  = doc["week1"]  | 0.0f;
        flowWeek2  = doc["week2"]  | 0.0f;
        flowMonth1 = doc["month1"] | 0.0f;
        flowMonth2 = doc["month2"] | 0.0f;
        flowYear1  = doc["year1"]  | 0.0f;
        flowYear2  = doc["year2"]  | 0.0f;
        Serial.println("[FLOW] SD subtotals restored for today");
    } else {
        Serial.println("[FLOW] SD subtotals reset (new day)");
    }

    Serial.printf("[FLOW] SD total1=%.3fL total2=%.3fL\n", flowTotal1, flowTotal2);
}

static void loadTotalsFromNvs() {
    prefs.begin("flow", true);  // read-only
    float nvsT1 = prefs.getFloat("t1", -1.0f);
    float nvsT2 = prefs.getFloat("t2", -1.0f);
    prefs.end();

    if (nvsT1 < 0.0f && nvsT2 < 0.0f) {
        Serial.println("[FLOW] NVS empty, using SD totals");
        return;
    }

    // NVS saves more frequently than SD — use whichever total is larger
    if (nvsT1 >= 0.0f && nvsT1 > flowTotal1) {
        Serial.printf("[FLOW] NVS total1 newer: %.3fL (was %.3fL)\n", nvsT1, flowTotal1);
        flowTotal1 = nvsT1;
    }
    if (nvsT2 >= 0.0f && nvsT2 > flowTotal2) {
        Serial.printf("[FLOW] NVS total2 newer: %.3fL (was %.3fL)\n", nvsT2, flowTotal2);
        flowTotal2 = nvsT2;
    }

    lastNvsSavedTotal1 = flowTotal1;
    lastNvsSavedTotal2 = flowTotal2;
}

static void saveToNvs() {
    prefs.begin("flow", false);  // read-write
    prefs.putFloat("t1", flowTotal1);
    prefs.putFloat("t2", flowTotal2);
    prefs.end();
    lastNvsSavedTotal1 = flowTotal1;
    lastNvsSavedTotal2 = flowTotal2;
}

static void saveToSd() {
    JsonDocument doc;
    doc["t1"]     = flowTotal1;
    doc["t2"]     = flowTotal2;
    doc["today1"] = flowToday1;
    doc["today2"] = flowToday2;
    doc["week1"]  = flowWeek1;
    doc["week2"]  = flowWeek2;
    doc["month1"] = flowMonth1;
    doc["month2"] = flowMonth2;
    doc["year1"]  = flowYear1;
    doc["year2"]  = flowYear2;
    doc["date"]   = timeRtc_getDateString();

    storeSd_writeJsonFile(SD_FLOW_TOTAL_PATH, doc);
    lastSdSavedTotal1 = flowTotal1;
    lastSdSavedTotal2 = flowTotal2;
}

bool sensorFlow_begin() {
    loadConfig();
    loadTotalsFromSd();   // SD first — has subtotals + date
    loadTotalsFromNvs();  // NVS second — may have a more recent total

    pinMode(PIN_FLOW_1, INPUT_PULLUP);
    pinMode(PIN_FLOW_2, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_FLOW_1), isrFlow1, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_FLOW_2), isrFlow2, FALLING);

    Serial.printf("[FLOW] started — total1=%.3fL total2=%.3fL\n", flowTotal1, flowTotal2);
    return true;
}

void sensorFlow_loop() {
    unsigned long now = millis();
    if (now - lastCalcMs < CALC_INTERVAL_MS) {
        return;
    }
    lastCalcMs = now;

    // Snapshot and clear pulse counters atomically
    uint32_t p1 = 0, p2 = 0;
    noInterrupts();
    p1 = rawPulses1; rawPulses1 = 0;
    p2 = rawPulses2; rawPulses2 = 0;
    interrupts();

    float litres1 = (float)p1 / kFactor1;  // pulses / (pulses/L) = L
    float litres2 = (float)p2 / kFactor2;

    flowRate1 = litres1 * 60.0f;  // L/s → L/min
    flowRate2 = litres2 * 60.0f;

    // Date boundary resets before accumulating so this interval's volume
    // falls into the correct period bucket.
    uint32_t ts = timeRtc_getUnixTime();
    if (ts > 0) {
        int32_t unixDay   = (int32_t)(ts / 86400UL);
        int32_t curMonday = mondayOf(unixDay);

        String dateStr = timeRtc_getDateString();
        int curYear  = dateStr.substring(0, 4).toInt();
        int curMonth = dateStr.substring(5, 7).toInt();

        if (lastUnixDay < 0) {
            lastUnixDay   = unixDay;
            lastMondayDay = curMonday;
            lastMonth     = curMonth;
            lastYear      = curYear;
        } else {
            if (unixDay != lastUnixDay) {
                flowToday1  = 0.0f;
                flowToday2  = 0.0f;
                lastUnixDay = unixDay;
            }
            if (curMonday != lastMondayDay) {
                flowWeek1     = 0.0f;
                flowWeek2     = 0.0f;
                lastMondayDay = curMonday;
            }
            if (curMonth != lastMonth) {
                flowMonth1 = 0.0f;
                flowMonth2 = 0.0f;
                lastMonth  = curMonth;
            }
            if (curYear != lastYear) {
                flowYear1 = 0.0f;
                flowYear2 = 0.0f;
                lastYear  = curYear;
            }
        }
    }

    flowTotal1 += litres1;
    flowTotal2 += litres2;
    flowToday1 += litres1;
    flowToday2 += litres2;
    flowWeek1  += litres1;
    flowWeek2  += litres2;
    flowMonth1 += litres1;
    flowMonth2 += litres2;
    flowYear1  += litres1;
    flowYear2  += litres2;

    // NVS save — frequent, survives power loss
    if (now - lastNvsSaveMs >= NVS_FLOW_SAVE_INTERVAL_MS) {
        lastNvsSaveMs = now;
        if (flowTotal1 != lastNvsSavedTotal1 || flowTotal2 != lastNvsSavedTotal2) {
            saveToNvs();
        }
    }

    // SD save — less frequent, keeps subtotals + date for next boot
    if (now - lastSdSaveMs >= DATA_LOG_INTERVAL_MS) {
        lastSdSaveMs = now;
        if (flowTotal1 != lastSdSavedTotal1 || flowTotal2 != lastSdSavedTotal2) {
            saveToSd();
        }
    }
}

bool sensorFlow_setKFactor(uint8_t ch, float k) {
    if (k < 1.0f || k > 9999.0f) {
        Serial.printf("[FLOW] setKFactor: invalid value %.1f\n", k);
        return false;
    }
    if (ch == 1)      kFactor1 = k;
    else if (ch == 2) kFactor2 = k;
    else return false;

    Serial.printf("[FLOW] K%d set to %.0f\n", ch, k);

    // Persist to node.json — read existing config, update flow section, write back
    JsonDocument doc;
    storeSd_readJsonFile(SD_CONFIG_PATH, doc);  // load existing keys (sd config etc.)
    doc["flow"]["k_factor_1"] = kFactor1;
    doc["flow"]["k_factor_2"] = kFactor2;
    storeSd_writeJsonFile(SD_CONFIG_PATH, doc);
    return true;
}

float sensorFlow_getRateLpm(uint8_t ch)  { return ch == 1 ? flowRate1  : flowRate2; }
float sensorFlow_getTotalL(uint8_t ch)   { return ch == 1 ? flowTotal1 : flowTotal2; }
float sensorFlow_getTodayL(uint8_t ch)   { return ch == 1 ? flowToday1 : flowToday2; }
float sensorFlow_getWeekL(uint8_t ch)    { return ch == 1 ? flowWeek1  : flowWeek2; }
float sensorFlow_getMonthL(uint8_t ch)   { return ch == 1 ? flowMonth1 : flowMonth2; }
float sensorFlow_getYearL(uint8_t ch)    { return ch == 1 ? flowYear1  : flowYear2; }
float sensorFlow_getKFactor(uint8_t ch)  { return ch == 1 ? kFactor1   : kFactor2; }
