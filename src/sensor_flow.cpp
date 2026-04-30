#include "sensor_flow.h"

#include <ArduinoJson.h>
#include <Preferences.h>

#include "config.h"
#include "pins.h"
#include "store_sd.h"
#include "time_rtc.h"

static const uint32_t CALC_INTERVAL_MS = 1000UL;
static const uint32_t DEBOUNCE_US_CH1 = 1000UL;  // K=5500, safe to ~10.9 L/min
static const uint32_t DEBOUNCE_US_CH2 = 500UL;   // K=20700, safe to ~5.8 L/min
static const uint32_t MIN_PULSES_PER_INTERVAL_CH1 = 1U;  // USN-HS06PE on channel 1
static const uint32_t MIN_PULSES_PER_INTERVAL_CH2 = 2U;  // USN-HS06PS on channel 2

static float nominalK1 = FLOW_K_FACTOR_DEFAULT_CH1;
static float nominalK2 = FLOW_K_FACTOR_DEFAULT_CH2;
static float appliedK1 = FLOW_K_FACTOR_DEFAULT_CH1;
static float appliedK2 = FLOW_K_FACTOR_DEFAULT_CH2;
static FlowKPoint kTable1[FLOW_K_TABLE_MAX_POINTS];
static FlowKPoint kTable2[FLOW_K_TABLE_MAX_POINTS];
static int kTableLen1 = 0;
static int kTableLen2 = 0;

static volatile uint32_t rawPulses1 = 0;
static volatile uint32_t rawPulses2 = 0;
static volatile uint32_t lastPulseTimeUs1 = 0;
static volatile uint32_t lastPulseTimeUs2 = 0;

static uint64_t totalPulses1 = 0;
static uint64_t totalPulses2 = 0;

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
static float flowAvgSamples1[FLOW_AVG_WINDOW] = {0.0f};
static float flowAvgSamples2[FLOW_AVG_WINDOW] = {0.0f};
static uint8_t flowAvgHead1 = 0;
static uint8_t flowAvgHead2 = 0;
static uint8_t flowAvgCount1 = 0;
static uint8_t flowAvgCount2 = 0;

static unsigned long lastCalcMs    = 0;
static unsigned long lastSdSaveMs  = 0;
static unsigned long lastNvsSaveMs = 0;

static float lastSdSavedTotal1  = -1.0f;
static float lastSdSavedTotal2  = -1.0f;
static float lastNvsSavedTotal1 = -1.0f;
static float lastNvsSavedTotal2 = -1.0f;
static uint64_t lastSdSavedPulses1  = UINT64_MAX;
static uint64_t lastSdSavedPulses2  = UINT64_MAX;
static uint64_t lastNvsSavedPulses1 = UINT64_MAX;
static uint64_t lastNvsSavedPulses2 = UINT64_MAX;

static int32_t lastUnixDay   = -1;
static int32_t lastMondayDay = -1;
static int      lastMonth    = -1;
static int      lastYear     = -1;

static Preferences prefs;

static void setSinglePointTable(FlowKPoint* table, int& len, float kFactor) {
    table[0].flowLpm = 0.0f;
    table[0].kPulsesPerL = kFactor;
    len = 1;
}

static int loadKTableFromJson(JsonArrayConst jsonTable, FlowKPoint* table) {
    int len = 0;
    float lastFlowLpm = -1.0f;
    for (JsonObjectConst point : jsonTable) {
        if (len >= FLOW_K_TABLE_MAX_POINTS) {
            break;
        }

        float flowLpm = point["flow_lpm"] | -1.0f;
        float kFactor = point["k"] | 0.0f;
        if (flowLpm < 0.0f || kFactor < 1.0f) {
            continue;
        }
        if (len > 0 && flowLpm < lastFlowLpm) {
            continue;
        }

        table[len].flowLpm = flowLpm;
        table[len].kPulsesPerL = kFactor;
        lastFlowLpm = flowLpm;
        len++;
    }
    return len;
}

static void writeKTableToJson(JsonArray jsonTable, const FlowKPoint* table, int len) {
    jsonTable.clear();
    for (int i = 0; i < len; ++i) {
        JsonObject point = jsonTable.add<JsonObject>();
        point["flow_lpm"] = table[i].flowLpm;
        point["k"] = table[i].kPulsesPerL;
    }
}

static void persistKConfig() {
    JsonDocument doc;
    storeSd_readJsonFile(SD_CONFIG_PATH, doc);

    JsonObject flow = doc["flow"].to<JsonObject>();
    flow["k_factor_1"] = nominalK1;
    flow["k_factor_2"] = nominalK2;

    JsonArray table1Json = flow["k_table_1"].to<JsonArray>();
    JsonArray table2Json = flow["k_table_2"].to<JsonArray>();
    writeKTableToJson(table1Json, kTable1, kTableLen1);
    writeKTableToJson(table2Json, kTable2, kTableLen2);

    storeSd_writeJsonFile(SD_CONFIG_PATH, doc);
}

static float pulsesToLitres(uint64_t pulses, float kFactor) {
    if (kFactor < 1.0f || pulses == 0ULL) {
        return 0.0f;
    }
    return (float)((double)pulses / (double)kFactor);
}

static uint8_t pushFlowSample(float sample, float* samples, uint8_t& head, uint8_t& count) {
    uint8_t slot = head;
    samples[slot] = sample;
    head = (uint8_t)((head + 1U) % FLOW_AVG_WINDOW);
    if (count < FLOW_AVG_WINDOW) {
        count++;
    }
    return slot;
}

static float averageFlowSamples(const float* samples, uint8_t count) {
    if (count == 0) {
        return 0.0f;
    }

    float sum = 0.0f;
    for (uint8_t i = 0; i < count; ++i) {
        sum += samples[i];
    }
    return sum / (float)count;
}

static void recomputeLifetimeTotals() {
    flowTotal1 = pulsesToLitres(totalPulses1, appliedK1);
    flowTotal2 = pulsesToLitres(totalPulses2, appliedK2);
}

float interpolateK(float flowLpm, const FlowKPoint* table, int len) {
    if (table == nullptr || len <= 0) {
        return 1.0f;
    }
    if (len == 1 || flowLpm <= table[0].flowLpm) {
        return table[0].kPulsesPerL;
    }
    if (flowLpm >= table[len - 1].flowLpm) {
        return table[len - 1].kPulsesPerL;
    }

    for (int i = 1; i < len; ++i) {
        if (flowLpm <= table[i].flowLpm) {
            const FlowKPoint& lower = table[i - 1];
            const FlowKPoint& upper = table[i];
            float span = upper.flowLpm - lower.flowLpm;
            if (span <= 0.0f) {
                return upper.kPulsesPerL;
            }

            float position = (flowLpm - lower.flowLpm) / span;
            return lower.kPulsesPerL + ((upper.kPulsesPerL - lower.kPulsesPerL) * position);
        }
    }

    return table[len - 1].kPulsesPerL;
}

static uint64_t litresToPulses(float litres, float kFactor) {
    if (litres <= 0.0f || kFactor < 1.0f) {
        return 0;
    }
    double pulses = static_cast<double>(litres) * static_cast<double>(kFactor);
    if (pulses <= 0.0) {
        return 0;
    }
    return static_cast<uint64_t>(pulses + 0.5);
}

static void IRAM_ATTR isrFlow1() {
    uint32_t nowUs = micros();
    if ((uint32_t)(nowUs - lastPulseTimeUs1) < DEBOUNCE_US_CH1) {
        return;
    }
    lastPulseTimeUs1 = nowUs;
    rawPulses1 = rawPulses1 + 1;
}

static void IRAM_ATTR isrFlow2() {
    uint32_t nowUs = micros();
    if ((uint32_t)(nowUs - lastPulseTimeUs2) < DEBOUNCE_US_CH2) {
        return;
    }
    lastPulseTimeUs2 = nowUs;
    rawPulses2 = rawPulses2 + 1;
}

// Returns the unix day number of the Monday containing unixDay.
// Day 0 = 1970-01-01 (Thursday). Monday offset: (day + 3) % 7.
static int32_t mondayOf(int32_t unixDay) {
    return unixDay - (int32_t)((unixDay + 3) % 7);
}

static void loadConfig() {
    nominalK1 = FLOW_K_FACTOR_DEFAULT_CH1;
    nominalK2 = FLOW_K_FACTOR_DEFAULT_CH2;
    setSinglePointTable(kTable1, kTableLen1, nominalK1);
    setSinglePointTable(kTable2, kTableLen2, nominalK2);

    JsonDocument doc;
    if (storeSd_readJsonFile(SD_CONFIG_PATH, doc)) {
        JsonObjectConst flow = doc["flow"].as<JsonObjectConst>();
        float k1 = flow["k_factor_1"] | FLOW_K_FACTOR_DEFAULT_CH1;
        float k2 = flow["k_factor_2"] | FLOW_K_FACTOR_DEFAULT_CH2;

        if (k1 >= 1.0f) {
            nominalK1 = k1;
        }
        if (k2 >= 1.0f) {
            nominalK2 = k2;
        }

        if (flow["k_table_1"].is<JsonArrayConst>()) {
            kTableLen1 = loadKTableFromJson(flow["k_table_1"].as<JsonArrayConst>(), kTable1);
        }
        if (flow["k_table_2"].is<JsonArrayConst>()) {
            kTableLen2 = loadKTableFromJson(flow["k_table_2"].as<JsonArrayConst>(), kTable2);
        }
    }

    if (kTableLen1 <= 0) {
        setSinglePointTable(kTable1, kTableLen1, nominalK1);
    }
    if (kTableLen2 <= 0) {
        setSinglePointTable(kTable2, kTableLen2, nominalK2);
    }

    appliedK1 = interpolateK(0.0f, kTable1, kTableLen1);
    appliedK2 = interpolateK(0.0f, kTable2, kTableLen2);

    Serial.printf("[FLOW] K1 nominal=%.0f points=%d K2 nominal=%.0f points=%d\n",
                  nominalK1,
                  kTableLen1,
                  nominalK2,
                  kTableLen2);
}

static void loadTotalsFromSd() {
    JsonDocument doc;
    if (!storeSd_readJsonFile(SD_FLOW_TOTAL_PATH, doc)) {
        Serial.println("[FLOW] no SD totals, starting at 0");
        return;
    }

    flowTotal1      = doc["t1"] | 0.0f;
    flowTotal2      = doc["t2"] | 0.0f;
    totalPulses1    = doc["p1"].isNull() ? litresToPulses(flowTotal1, nominalK1) : (uint64_t)(doc["p1"] | 0ULL);
    totalPulses2    = doc["p2"].isNull() ? litresToPulses(flowTotal2, nominalK2) : (uint64_t)(doc["p2"] | 0ULL);
    lastSdSavedTotal1 = flowTotal1;
    lastSdSavedTotal2 = flowTotal2;
    lastSdSavedPulses1 = totalPulses1;
    lastSdSavedPulses2 = totalPulses2;

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

    Serial.printf("[FLOW] SD total1=%.3fL total2=%.3fL pulses1=%llu pulses2=%llu\n",
                  flowTotal1,
                  flowTotal2,
                  totalPulses1,
                  totalPulses2);
}

static void loadTotalsFromNvs() {
    prefs.begin("flow", true);  // read-only
    float nvsT1 = prefs.getFloat("t1", -1.0f);
    float nvsT2 = prefs.getFloat("t2", -1.0f);
    uint64_t nvsP1 = prefs.isKey("p1") ? prefs.getULong64("p1", 0ULL) : litresToPulses(nvsT1, nominalK1);
    uint64_t nvsP2 = prefs.isKey("p2") ? prefs.getULong64("p2", 0ULL) : litresToPulses(nvsT2, nominalK2);
    prefs.end();

    if (nvsT1 < 0.0f && nvsT2 < 0.0f) {
        Serial.println("[FLOW] NVS empty, using SD totals");
        return;
    }

    // NVS saves more frequently than SD — use whichever total is larger
    if (nvsT1 >= 0.0f && nvsT1 > flowTotal1) {
        Serial.printf("[FLOW] NVS total1 newer: %.3fL (was %.3fL)\n", nvsT1, flowTotal1);
        flowTotal1 = nvsT1;
        totalPulses1 = nvsP1;
    }
    if (nvsT2 >= 0.0f && nvsT2 > flowTotal2) {
        Serial.printf("[FLOW] NVS total2 newer: %.3fL (was %.3fL)\n", nvsT2, flowTotal2);
        flowTotal2 = nvsT2;
        totalPulses2 = nvsP2;
    }

    lastNvsSavedTotal1 = flowTotal1;
    lastNvsSavedTotal2 = flowTotal2;
    lastNvsSavedPulses1 = totalPulses1;
    lastNvsSavedPulses2 = totalPulses2;
}

static void saveToNvs() {
    prefs.begin("flow", false);  // read-write
    prefs.putFloat("t1", flowTotal1);
    prefs.putFloat("t2", flowTotal2);
    prefs.putULong64("p1", totalPulses1);
    prefs.putULong64("p2", totalPulses2);
    prefs.end();
    lastNvsSavedTotal1 = flowTotal1;
    lastNvsSavedTotal2 = flowTotal2;
    lastNvsSavedPulses1 = totalPulses1;
    lastNvsSavedPulses2 = totalPulses2;
}

static void saveToSd() {
    JsonDocument doc;
    doc["t1"]     = flowTotal1;
    doc["t2"]     = flowTotal2;
    doc["p1"]     = totalPulses1;
    doc["p2"]     = totalPulses2;
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
    lastSdSavedPulses1 = totalPulses1;
    lastSdSavedPulses2 = totalPulses2;
}

bool sensorFlow_begin() {
    loadConfig();
    loadTotalsFromSd();   // SD first — has subtotals + date
    loadTotalsFromNvs();  // NVS second — may have a more recent total
    recomputeLifetimeTotals();

    pinMode(PIN_FLOW_1, INPUT_PULLUP);
    pinMode(PIN_FLOW_2, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_FLOW_1), isrFlow1, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_FLOW_2), isrFlow2, FALLING);

    Serial.printf("[FLOW] started — total1=%.3fL total2=%.3fL pulses1=%llu pulses2=%llu\n",
                  flowTotal1,
                  flowTotal2,
                  totalPulses1,
                  totalPulses2);
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

    totalPulses1 += p1;
    totalPulses2 += p2;

    uint32_t meteredP1 = (p1 >= MIN_PULSES_PER_INTERVAL_CH1) ? p1 : 0U;
    uint32_t meteredP2 = (p2 >= MIN_PULSES_PER_INTERVAL_CH2) ? p2 : 0U;

    uint8_t sampleSlot1 = pushFlowSample(((float)meteredP1 * 60.0f) / appliedK1,
                                         flowAvgSamples1,
                                         flowAvgHead1,
                                         flowAvgCount1);
    uint8_t sampleSlot2 = pushFlowSample(((float)meteredP2 * 60.0f) / appliedK2,
                                         flowAvgSamples2,
                                         flowAvgHead2,
                                         flowAvgCount2);

    float smoothedFlow1 = averageFlowSamples(flowAvgSamples1, flowAvgCount1);
    float smoothedFlow2 = averageFlowSamples(flowAvgSamples2, flowAvgCount2);
    appliedK1 = interpolateK(smoothedFlow1, kTable1, kTableLen1);
    appliedK2 = interpolateK(smoothedFlow2, kTable2, kTableLen2);

    float litres1 = pulsesToLitres(meteredP1, appliedK1);  // pulses / (pulses/L) = L
    float litres2 = pulsesToLitres(meteredP2, appliedK2);

    flowAvgSamples1[sampleSlot1] = litres1 * 60.0f;
    flowAvgSamples2[sampleSlot2] = litres2 * 60.0f;
    flowRate1 = averageFlowSamples(flowAvgSamples1, flowAvgCount1);
    flowRate2 = averageFlowSamples(flowAvgSamples2, flowAvgCount2);
    appliedK1 = interpolateK(flowRate1, kTable1, kTableLen1);
    appliedK2 = interpolateK(flowRate2, kTable2, kTableLen2);
    litres1 = pulsesToLitres(meteredP1, appliedK1);
    litres2 = pulsesToLitres(meteredP2, appliedK2);
    flowAvgSamples1[sampleSlot1] = litres1 * 60.0f;
    flowAvgSamples2[sampleSlot2] = litres2 * 60.0f;
    flowRate1 = averageFlowSamples(flowAvgSamples1, flowAvgCount1);
    flowRate2 = averageFlowSamples(flowAvgSamples2, flowAvgCount2);

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

    recomputeLifetimeTotals();
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
        if (flowTotal1 != lastNvsSavedTotal1 || flowTotal2 != lastNvsSavedTotal2 ||
            totalPulses1 != lastNvsSavedPulses1 || totalPulses2 != lastNvsSavedPulses2) {
            saveToNvs();
        }
    }

    // SD save — less frequent, keeps subtotals + date for next boot
    if (now - lastSdSaveMs >= DATA_LOG_INTERVAL_MS) {
        lastSdSaveMs = now;
        if (flowTotal1 != lastSdSavedTotal1 || flowTotal2 != lastSdSavedTotal2 ||
            totalPulses1 != lastSdSavedPulses1 || totalPulses2 != lastSdSavedPulses2) {
            saveToSd();
        }
    }
}

bool sensorFlow_setKFactor(uint8_t ch, float k) {
    if (k < 1.0f || k > 99999.0f) {
        Serial.printf("[FLOW] setKFactor: invalid value %.1f\n", k);
        return false;
    }
    if (ch == 1) {
        nominalK1 = k;
        setSinglePointTable(kTable1, kTableLen1, k);
        appliedK1 = interpolateK(flowRate1, kTable1, kTableLen1);
        recomputeLifetimeTotals();
    } else if (ch == 2) {
        nominalK2 = k;
        setSinglePointTable(kTable2, kTableLen2, k);
        appliedK2 = interpolateK(flowRate2, kTable2, kTableLen2);
        recomputeLifetimeTotals();
    }
    else return false;

    Serial.printf("[FLOW] K%d set to %.0f\n", ch, k);

    persistKConfig();
    return true;
}

static void forceSdSave() {
    lastSdSavedTotal1 = -1.0f;
    lastSdSavedTotal2 = -1.0f;
    lastSdSavedPulses1 = UINT64_MAX;
    lastSdSavedPulses2 = UINT64_MAX;
    saveToSd();
}

static void logAndPrint(const char* msg) {
    storeSd_logEvent(msg);
    Serial.println(msg);
}

void sensorFlow_resetToday(uint8_t ch) {
    if (ch == 0 || ch == 1) flowToday1 = 0.0f;
    if (ch == 0 || ch == 2) flowToday2 = 0.0f;
    forceSdSave();
    char msg[48]; snprintf(msg, sizeof(msg), "[FLOW] reset today ch=%d", ch);
    logAndPrint(msg);
}

void sensorFlow_resetWeek(uint8_t ch) {
    if (ch == 0 || ch == 1) flowWeek1 = 0.0f;
    if (ch == 0 || ch == 2) flowWeek2 = 0.0f;
    forceSdSave();
    char msg[48]; snprintf(msg, sizeof(msg), "[FLOW] reset week ch=%d", ch);
    logAndPrint(msg);
}

void sensorFlow_resetMonth(uint8_t ch) {
    if (ch == 0 || ch == 1) flowMonth1 = 0.0f;
    if (ch == 0 || ch == 2) flowMonth2 = 0.0f;
    forceSdSave();
    char msg[48]; snprintf(msg, sizeof(msg), "[FLOW] reset month ch=%d", ch);
    logAndPrint(msg);
}

void sensorFlow_resetYear(uint8_t ch) {
    if (ch == 0 || ch == 1) flowYear1 = 0.0f;
    if (ch == 0 || ch == 2) flowYear2 = 0.0f;
    forceSdSave();
    char msg[48]; snprintf(msg, sizeof(msg), "[FLOW] reset year ch=%d", ch);
    logAndPrint(msg);
}

void sensorFlow_resetTotals(uint8_t ch) {
    if (ch == 0 || ch == 1) {
        flowTotal1 = flowToday1 = flowWeek1 = flowMonth1 = flowYear1 = 0.0f;
        totalPulses1 = 0;
    }
    if (ch == 0 || ch == 2) {
        flowTotal2 = flowToday2 = flowWeek2 = flowMonth2 = flowYear2 = 0.0f;
        totalPulses2 = 0;
    }
    prefs.begin("flow", false);
    if (ch == 0 || ch == 1) {
        prefs.putFloat("t1", 0.0f);
        prefs.putULong64("p1", 0ULL);
        lastNvsSavedTotal1 = 0.0f;
        lastNvsSavedPulses1 = 0;
    }
    if (ch == 0 || ch == 2) {
        prefs.putFloat("t2", 0.0f);
        prefs.putULong64("p2", 0ULL);
        lastNvsSavedTotal2 = 0.0f;
        lastNvsSavedPulses2 = 0;
    }
    prefs.end();
    forceSdSave();
    char msg[48]; snprintf(msg, sizeof(msg), "[FLOW] reset totals ch=%d", ch);
    logAndPrint(msg);
}

void sensorFlow_factoryReset() {
    flowRate1 = flowRate2 = 0.0f;
    flowTotal1 = flowToday1 = flowWeek1 = flowMonth1 = flowYear1 = 0.0f;
    flowTotal2 = flowToday2 = flowWeek2 = flowMonth2 = flowYear2 = 0.0f;
    totalPulses1 = totalPulses2 = 0;
    for (uint8_t i = 0; i < FLOW_AVG_WINDOW; ++i) {
        flowAvgSamples1[i] = 0.0f;
        flowAvgSamples2[i] = 0.0f;
    }
    flowAvgHead1 = flowAvgHead2 = 0;
    flowAvgCount1 = flowAvgCount2 = 0;
    appliedK1 = interpolateK(0.0f, kTable1, kTableLen1);
    appliedK2 = interpolateK(0.0f, kTable2, kTableLen2);
    prefs.begin("flow", false);
    prefs.clear();
    prefs.end();
    lastNvsSavedTotal1 = lastNvsSavedTotal2 = 0.0f;
    lastNvsSavedPulses1 = lastNvsSavedPulses2 = 0;
    forceSdSave();
    logAndPrint("[FLOW] factory reset — all flow data cleared");
}

float sensorFlow_getRateLpm(uint8_t ch)  { return ch == 1 ? flowRate1  : flowRate2; }
float sensorFlow_getTotalL(uint8_t ch)   { return ch == 1 ? flowTotal1 : flowTotal2; }
uint64_t sensorFlow_getTotalPulses(uint8_t ch) { return ch == 1 ? totalPulses1 : totalPulses2; }
float sensorFlow_getTodayL(uint8_t ch)   { return ch == 1 ? flowToday1 : flowToday2; }
float sensorFlow_getWeekL(uint8_t ch)    { return ch == 1 ? flowWeek1  : flowWeek2; }
float sensorFlow_getMonthL(uint8_t ch)   { return ch == 1 ? flowMonth1 : flowMonth2; }
float sensorFlow_getYearL(uint8_t ch)    { return ch == 1 ? flowYear1  : flowYear2; }
float sensorFlow_getKFactor(uint8_t ch)  { return ch == 1 ? nominalK1  : nominalK2; }
