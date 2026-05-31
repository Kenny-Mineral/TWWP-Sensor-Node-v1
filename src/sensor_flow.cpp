#include "sensor_flow.h"

#include <ArduinoJson.h>
#include <Preferences.h>

#include "config.h"
#include "pins.h"
#include "store_sd.h"
#include "time_rtc.h"

static const uint32_t CALC_INTERVAL_MS = 1000UL;
static const uint32_t DEBOUNCE_US_DEFAULT_CH1 = 1000UL;
static const uint32_t DEBOUNCE_US_DEFAULT_CH2 = 500UL;
static const uint32_t DEBOUNCE_US_DEFAULT_CH3 = 1000UL;

// Sensor model registry — add new sensors here
struct FlowSensorModel {
    const char* name;
    float       nominalK;
    uint32_t    debounceUs;
    uint32_t    minPulses;
    FlowKPoint  kTable[FLOW_K_TABLE_MAX_POINTS];
    int         kTableLen;
};

// DWS-MH-02: F(Hz) = 15Q − 2, K(pulses/L) = F/Q*60
// K-table derived from formula at representative RO flow rates (0.3–5.0 L/min)
static const FlowSensorModel s_sensorModels[] = {
    {"USN-HS06PE", 5500.0f,  1000, 1, {{0.0f, 5500.0f},  {}, {}, {}, {}}, 1},
    {"USN-HS06PS", 20700.0f,  500, 2, {{0.0f, 20700.0f}, {}, {}, {}, {}}, 1},
    {"DWS-MH-02",   780.0f, 1000, 1,
        {{0.3f, 500.0f}, {0.5f, 660.0f}, {1.0f, 780.0f}, {2.0f, 840.0f}, {5.0f, 876.0f}}, 5},
};
static const int s_sensorModelCount = (int)(sizeof(s_sensorModels) / sizeof(s_sensorModels[0]));

static const FlowSensorModel* findModel(const char* name) {
    for (int i = 0; i < s_sensorModelCount; ++i) {
        if (strcmp(s_sensorModels[i].name, name) == 0) return &s_sensorModels[i];
    }
    return nullptr;
}

// Runtime-mutable per-channel state (model-derived defaults, overridable from node.json)
static uint32_t debounceUsCh1 = DEBOUNCE_US_DEFAULT_CH1;
static uint32_t debounceUsCh2 = DEBOUNCE_US_DEFAULT_CH2;
static uint32_t debounceUsCh3 = DEBOUNCE_US_DEFAULT_CH3;
static uint32_t minPulsesPerIntervalCh1 = 1U;
static uint32_t minPulsesPerIntervalCh2 = 2U;
static uint32_t minPulsesPerIntervalCh3 = 1U;
static char s_modelName1[32] = FLOW_SENSOR_MODEL_DEFAULT_CH1;
static char s_modelName2[32] = FLOW_SENSOR_MODEL_DEFAULT_CH2;
static char s_modelName3[32] = FLOW_SENSOR_MODEL_DEFAULT_CH3;

// Runtime-mutable moving average window size (default = 5, max = FLOW_AVG_WINDOW_MAX)
static uint8_t flowAvgWindow = FLOW_AVG_WINDOW_DEFAULT;

static float nominalK1 = FLOW_K_FACTOR_DEFAULT_CH1;
static float nominalK2 = FLOW_K_FACTOR_DEFAULT_CH2;
static float nominalK3 = FLOW_K_FACTOR_DEFAULT_CH3;
static float appliedK1 = FLOW_K_FACTOR_DEFAULT_CH1;
static float appliedK2 = FLOW_K_FACTOR_DEFAULT_CH2;
static float appliedK3 = FLOW_K_FACTOR_DEFAULT_CH3;
static FlowKPoint kTable1[FLOW_K_TABLE_MAX_POINTS];
static FlowKPoint kTable2[FLOW_K_TABLE_MAX_POINTS];
static FlowKPoint kTable3[FLOW_K_TABLE_MAX_POINTS];
static int kTableLen1 = 0;
static int kTableLen2 = 0;
static int kTableLen3 = 0;

static volatile uint32_t rawPulses1 = 0;
static volatile uint32_t rawPulses2 = 0;
static volatile uint32_t rawPulses3 = 0;
static volatile uint32_t lastPulseTimeUs1 = 0;
static volatile uint32_t lastPulseTimeUs2 = 0;
static volatile uint32_t lastPulseTimeUs3 = 0;

static uint64_t totalPulses1 = 0;
static uint64_t totalPulses2 = 0;
static uint64_t totalPulses3 = 0;

static float flowRate1  = 0.0f;
static float flowRate2  = 0.0f;
static float flowRate3  = 0.0f;
static float flowTotal1 = 0.0f;
static float flowTotal2 = 0.0f;
static float flowTotal3 = 0.0f;
static float flowToday1 = 0.0f;
static float flowToday2 = 0.0f;
static float flowToday3 = 0.0f;
static float flowWeek1  = 0.0f;
static float flowWeek2  = 0.0f;
static float flowWeek3  = 0.0f;
static float flowMonth1 = 0.0f;
static float flowMonth2 = 0.0f;
static float flowMonth3 = 0.0f;
static float flowYear1  = 0.0f;
static float flowYear2  = 0.0f;
static float flowYear3  = 0.0f;
static float flowAvgSamples1[FLOW_AVG_WINDOW_MAX] = {0.0f};
static float flowAvgSamples2[FLOW_AVG_WINDOW_MAX] = {0.0f};
static float flowAvgSamples3[FLOW_AVG_WINDOW_MAX] = {0.0f};
static uint8_t flowAvgHead1 = 0;
static uint8_t flowAvgHead2 = 0;
static uint8_t flowAvgHead3 = 0;
static uint8_t flowAvgCount1 = 0;
static uint8_t flowAvgCount2 = 0;
static uint8_t flowAvgCount3 = 0;

static unsigned long lastCalcMs    = 0;

static void checkCalTimeout(uint8_t ch);  // defined in cal section below
static unsigned long lastSdSaveMs  = 0;
static unsigned long lastNvsSaveMs = 0;

static float lastSdSavedTotal1  = -1.0f;
static float lastSdSavedTotal2  = -1.0f;
static float lastSdSavedTotal3  = -1.0f;
static float lastNvsSavedTotal1 = -1.0f;
static float lastNvsSavedTotal2 = -1.0f;
static float lastNvsSavedTotal3 = -1.0f;
static uint64_t lastSdSavedPulses1  = UINT64_MAX;
static uint64_t lastSdSavedPulses2  = UINT64_MAX;
static uint64_t lastSdSavedPulses3  = UINT64_MAX;
static uint64_t lastNvsSavedPulses1 = UINT64_MAX;
static uint64_t lastNvsSavedPulses2 = UINT64_MAX;
static uint64_t lastNvsSavedPulses3 = UINT64_MAX;

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

    JsonObject flow = doc["flow"].as<JsonObject>();
    if (flow.isNull()) flow = doc["flow"].to<JsonObject>();
    flow["sensor_model_1"] = s_modelName1;
    flow["sensor_model_2"] = s_modelName2;
    flow["sensor_model_3"] = s_modelName3;
    flow["k_factor_1"] = nominalK1;
    flow["k_factor_2"] = nominalK2;
    flow["k_factor_3"] = nominalK3;

    flow.remove("k_table_1");
    flow.remove("k_table_2");
    flow.remove("k_table_3");
    JsonArray table1Json = flow["k_table_1"].to<JsonArray>();
    JsonArray table2Json = flow["k_table_2"].to<JsonArray>();
    JsonArray table3Json = flow["k_table_3"].to<JsonArray>();
    writeKTableToJson(table1Json, kTable1, kTableLen1);
    writeKTableToJson(table2Json, kTable2, kTableLen2);
    writeKTableToJson(table3Json, kTable3, kTableLen3);

    storeSd_writeJsonFile(SD_CONFIG_PATH, doc);
}

static float pulsesToLitres(uint64_t pulses, float kFactor) {
    if (kFactor < 1.0f || pulses == 0ULL) {
        return 0.0f;
    }
    return (float)((double)pulses / (double)kFactor);
}

static uint8_t pushFlowSample(float sample, float* samples, uint8_t& head, uint8_t& count, uint8_t windowSize) {
    uint8_t slot = head;
    samples[slot] = sample;
    head = (uint8_t)((head + 1U) % windowSize);
    if (count < windowSize) {
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

float interpolateK(float flowLpm, const FlowKPoint* table, int len) {
    if (table == nullptr || len <= 0) {
        Serial.printf("[FLOW] interpolateK: empty K-table, returning 1.0 (flowLpm=%.2f)\n", flowLpm);
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
    if ((uint32_t)(nowUs - lastPulseTimeUs1) < debounceUsCh1) {
        return;
    }
    lastPulseTimeUs1 = nowUs;
    rawPulses1 = rawPulses1 + 1;
}

static void IRAM_ATTR isrFlow2() {
    uint32_t nowUs = micros();
    if ((uint32_t)(nowUs - lastPulseTimeUs2) < debounceUsCh2) {
        return;
    }
    lastPulseTimeUs2 = nowUs;
    rawPulses2 = rawPulses2 + 1;
}

static void IRAM_ATTR isrFlow3() {
    uint32_t nowUs = micros();
    if ((uint32_t)(nowUs - lastPulseTimeUs3) < debounceUsCh3) {
        return;
    }
    lastPulseTimeUs3 = nowUs;
    rawPulses3 = rawPulses3 + 1;
}

// Returns the unix day number of the Monday containing unixDay.
// Day 0 = 1970-01-01 (Thursday). Monday offset: (day + 3) % 7.
static int32_t mondayOf(int32_t unixDay) {
    return unixDay - (int32_t)((unixDay + 3) % 7);
}

static void loadConfig() {
    // Step 1: compile-time defaults
    strlcpy(s_modelName1, FLOW_SENSOR_MODEL_DEFAULT_CH1, sizeof(s_modelName1));
    strlcpy(s_modelName2, FLOW_SENSOR_MODEL_DEFAULT_CH2, sizeof(s_modelName2));
    strlcpy(s_modelName3, FLOW_SENSOR_MODEL_DEFAULT_CH3, sizeof(s_modelName3));
    nominalK1 = FLOW_K_FACTOR_DEFAULT_CH1;
    nominalK2 = FLOW_K_FACTOR_DEFAULT_CH2;
    nominalK3 = FLOW_K_FACTOR_DEFAULT_CH3;
    debounceUsCh1 = DEBOUNCE_US_DEFAULT_CH1;
    debounceUsCh2 = DEBOUNCE_US_DEFAULT_CH2;
    debounceUsCh3 = DEBOUNCE_US_DEFAULT_CH3;
    minPulsesPerIntervalCh1 = 1U;
    minPulsesPerIntervalCh2 = 2U;
    minPulsesPerIntervalCh3 = 1U;
    flowAvgWindow = FLOW_AVG_WINDOW_DEFAULT;
    setSinglePointTable(kTable1, kTableLen1, nominalK1);
    setSinglePointTable(kTable2, kTableLen2, nominalK2);
    setSinglePointTable(kTable3, kTableLen3, nominalK3);

    JsonDocument doc;
    if (storeSd_readJsonFile(SD_CONFIG_PATH, doc)) {
        JsonObjectConst flow = doc["flow"].as<JsonObjectConst>();

        // Step 2: apply model defaults (before explicit overrides)
        const char* m1str = flow["sensor_model_1"] | FLOW_SENSOR_MODEL_DEFAULT_CH1;
        const char* m2str = flow["sensor_model_2"] | FLOW_SENSOR_MODEL_DEFAULT_CH2;
        const char* m3str = flow["sensor_model_3"] | FLOW_SENSOR_MODEL_DEFAULT_CH3;
        strlcpy(s_modelName1, m1str, sizeof(s_modelName1));
        strlcpy(s_modelName2, m2str, sizeof(s_modelName2));
        strlcpy(s_modelName3, m3str, sizeof(s_modelName3));

        const FlowSensorModel* m1 = findModel(s_modelName1);
        const FlowSensorModel* m2 = findModel(s_modelName2);
        const FlowSensorModel* m3 = findModel(s_modelName3);

        if (m1) {
            nominalK1 = m1->nominalK;
            debounceUsCh1 = m1->debounceUs;
            minPulsesPerIntervalCh1 = m1->minPulses;
            memcpy(kTable1, m1->kTable, m1->kTableLen * sizeof(FlowKPoint));
            kTableLen1 = m1->kTableLen;
        }
        if (m2) {
            nominalK2 = m2->nominalK;
            debounceUsCh2 = m2->debounceUs;
            minPulsesPerIntervalCh2 = m2->minPulses;
            memcpy(kTable2, m2->kTable, m2->kTableLen * sizeof(FlowKPoint));
            kTableLen2 = m2->kTableLen;
        }
        if (m3) {
            nominalK3 = m3->nominalK;
            debounceUsCh3 = m3->debounceUs;
            minPulsesPerIntervalCh3 = m3->minPulses;
            memcpy(kTable3, m3->kTable, m3->kTableLen * sizeof(FlowKPoint));
            kTableLen3 = m3->kTableLen;
        }

        // Step 3: explicit overrides from node.json take priority over model defaults
        if (!flow["k_factor_1"].isNull()) {
            float k1 = flow["k_factor_1"].as<float>();
            if (k1 >= 1.0f) nominalK1 = k1;
        }
        if (!flow["k_factor_2"].isNull()) {
            float k2 = flow["k_factor_2"].as<float>();
            if (k2 >= 1.0f) nominalK2 = k2;
        }
        if (!flow["k_factor_3"].isNull()) {
            float k3 = flow["k_factor_3"].as<float>();
            if (k3 >= 1.0f) nominalK3 = k3;
        }

        if (flow["k_table_1"].is<JsonArrayConst>()) {
            kTableLen1 = loadKTableFromJson(flow["k_table_1"].as<JsonArrayConst>(), kTable1);
        }
        if (flow["k_table_2"].is<JsonArrayConst>()) {
            kTableLen2 = loadKTableFromJson(flow["k_table_2"].as<JsonArrayConst>(), kTable2);
        }
        if (flow["k_table_3"].is<JsonArrayConst>()) {
            kTableLen3 = loadKTableFromJson(flow["k_table_3"].as<JsonArrayConst>(), kTable3);
        }

        if (!flow["debounce_us_1"].isNull()) {
            uint32_t db1 = (uint32_t)flow["debounce_us_1"].as<int>();
            if (db1 >= 100 && db1 <= 10000) debounceUsCh1 = db1;
        }
        if (!flow["debounce_us_2"].isNull()) {
            uint32_t db2 = (uint32_t)flow["debounce_us_2"].as<int>();
            if (db2 >= 100 && db2 <= 10000) debounceUsCh2 = db2;
        }
        if (!flow["debounce_us_3"].isNull()) {
            uint32_t db3 = (uint32_t)flow["debounce_us_3"].as<int>();
            if (db3 >= 100 && db3 <= 10000) debounceUsCh3 = db3;
        }

        if (!flow["flow_avg_window"].isNull()) {
            uint8_t aw = (uint8_t)flow["flow_avg_window"].as<int>();
            if (aw >= 1 && aw <= FLOW_AVG_WINDOW_MAX) flowAvgWindow = aw;
        }
    }

    if (kTableLen1 <= 0) setSinglePointTable(kTable1, kTableLen1, nominalK1);
    if (kTableLen2 <= 0) setSinglePointTable(kTable2, kTableLen2, nominalK2);
    if (kTableLen3 <= 0) setSinglePointTable(kTable3, kTableLen3, nominalK3);

    appliedK1 = interpolateK(0.0f, kTable1, kTableLen1);
    appliedK2 = interpolateK(0.0f, kTable2, kTableLen2);
    appliedK3 = interpolateK(0.0f, kTable3, kTableLen3);

    Serial.printf("[FLOW] ch1 model=%s K=%.0f pts=%d debounce=%lu minP=%lu\n",
                  s_modelName1, nominalK1, kTableLen1,
                  (unsigned long)debounceUsCh1, (unsigned long)minPulsesPerIntervalCh1);
    Serial.printf("[FLOW] ch2 model=%s K=%.0f pts=%d debounce=%lu minP=%lu avgWin=%u\n",
                  s_modelName2, nominalK2, kTableLen2,
                  (unsigned long)debounceUsCh2, (unsigned long)minPulsesPerIntervalCh2, flowAvgWindow);
    Serial.printf("[FLOW] ch3 model=%s K=%.0f pts=%d debounce=%lu minP=%lu\n",
                  s_modelName3, nominalK3, kTableLen3,
                  (unsigned long)debounceUsCh3, (unsigned long)minPulsesPerIntervalCh3);
}

static void loadTotalsFromSd() {
    JsonDocument doc;
    if (!storeSd_readJsonFile(SD_FLOW_TOTAL_PATH, doc)) {
        Serial.println("[FLOW] no SD totals, starting at 0");
        return;
    }

    flowTotal1      = doc["t1"] | 0.0f;
    flowTotal2      = doc["t2"] | 0.0f;
    flowTotal3      = doc["t3"] | 0.0f;
    totalPulses1    = doc["p1"].isNull() ? litresToPulses(flowTotal1, nominalK1) : (uint64_t)(doc["p1"] | 0ULL);
    totalPulses2    = doc["p2"].isNull() ? litresToPulses(flowTotal2, nominalK2) : (uint64_t)(doc["p2"] | 0ULL);
    totalPulses3    = doc["p3"].isNull() ? litresToPulses(flowTotal3, nominalK3) : (uint64_t)(doc["p3"] | 0ULL);
    lastSdSavedTotal1 = flowTotal1;
    lastSdSavedTotal2 = flowTotal2;
    lastSdSavedTotal3 = flowTotal3;
    lastSdSavedPulses1 = totalPulses1;
    lastSdSavedPulses2 = totalPulses2;
    lastSdSavedPulses3 = totalPulses3;

    // Restore period subtotals only if saved date matches today
    const char* savedDate = doc["date"] | "";
    String today = timeRtc_getDateString();
    if (today.length() == 10 && strcmp(savedDate, today.c_str()) == 0) {
        flowToday1 = doc["today1"] | 0.0f;
        flowToday2 = doc["today2"] | 0.0f;
        flowToday3 = doc["today3"] | 0.0f;
        flowWeek1  = doc["week1"]  | 0.0f;
        flowWeek2  = doc["week2"]  | 0.0f;
        flowWeek3  = doc["week3"]  | 0.0f;
        flowMonth1 = doc["month1"] | 0.0f;
        flowMonth2 = doc["month2"] | 0.0f;
        flowMonth3 = doc["month3"] | 0.0f;
        flowYear1  = doc["year1"]  | 0.0f;
        flowYear2  = doc["year2"]  | 0.0f;
        flowYear3  = doc["year3"]  | 0.0f;
        Serial.println("[FLOW] SD subtotals restored for today");
    } else {
        Serial.println("[FLOW] SD subtotals reset (new day)");
    }

    Serial.printf("[FLOW] SD total1=%.3fL total2=%.3fL total3=%.3fL\n",
                  flowTotal1, flowTotal2, flowTotal3);
}

static void loadTotalsFromNvs() {
    prefs.begin("flow", true);  // read-only
    float nvsT1 = prefs.getFloat("t1", -1.0f);
    float nvsT2 = prefs.getFloat("t2", -1.0f);
    float nvsT3 = prefs.getFloat("t3", -1.0f);
    uint64_t nvsP1 = prefs.isKey("p1") ? prefs.getULong64("p1", 0ULL) : litresToPulses(nvsT1, nominalK1);
    uint64_t nvsP2 = prefs.isKey("p2") ? prefs.getULong64("p2", 0ULL) : litresToPulses(nvsT2, nominalK2);
    uint64_t nvsP3 = prefs.isKey("p3") ? prefs.getULong64("p3", 0ULL) : litresToPulses(nvsT3, nominalK3);
    prefs.end();

    if (nvsT1 < 0.0f && nvsT2 < 0.0f && nvsT3 < 0.0f) {
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
    if (nvsT3 >= 0.0f && nvsT3 > flowTotal3) {
        Serial.printf("[FLOW] NVS total3 newer: %.3fL (was %.3fL)\n", nvsT3, flowTotal3);
        flowTotal3 = nvsT3;
        totalPulses3 = nvsP3;
    }

    lastNvsSavedTotal1 = flowTotal1;
    lastNvsSavedTotal2 = flowTotal2;
    lastNvsSavedTotal3 = flowTotal3;
    lastNvsSavedPulses1 = totalPulses1;
    lastNvsSavedPulses2 = totalPulses2;
    lastNvsSavedPulses3 = totalPulses3;
}

static void saveToNvs() {
    prefs.begin("flow", false);  // read-write
    prefs.putFloat("t1", flowTotal1);
    prefs.putFloat("t2", flowTotal2);
    prefs.putFloat("t3", flowTotal3);
    prefs.putULong64("p1", totalPulses1);
    prefs.putULong64("p2", totalPulses2);
    prefs.putULong64("p3", totalPulses3);
    prefs.end();
    lastNvsSavedTotal1 = flowTotal1;
    lastNvsSavedTotal2 = flowTotal2;
    lastNvsSavedTotal3 = flowTotal3;
    lastNvsSavedPulses1 = totalPulses1;
    lastNvsSavedPulses2 = totalPulses2;
    lastNvsSavedPulses3 = totalPulses3;
}

static void saveToSd() {
    JsonDocument doc;
    doc["t1"]     = flowTotal1;
    doc["t2"]     = flowTotal2;
    doc["t3"]     = flowTotal3;
    doc["p1"]     = totalPulses1;
    doc["p2"]     = totalPulses2;
    doc["p3"]     = totalPulses3;
    doc["today1"] = flowToday1;
    doc["today2"] = flowToday2;
    doc["today3"] = flowToday3;
    doc["week1"]  = flowWeek1;
    doc["week2"]  = flowWeek2;
    doc["week3"]  = flowWeek3;
    doc["month1"] = flowMonth1;
    doc["month2"] = flowMonth2;
    doc["month3"] = flowMonth3;
    doc["year1"]  = flowYear1;
    doc["year2"]  = flowYear2;
    doc["year3"]  = flowYear3;
    doc["date"]   = timeRtc_getDateString();

    storeSd_writeJsonFile(SD_FLOW_TOTAL_PATH, doc);
    lastSdSavedTotal1 = flowTotal1;
    lastSdSavedTotal2 = flowTotal2;
    lastSdSavedTotal3 = flowTotal3;
    lastSdSavedPulses1 = totalPulses1;
    lastSdSavedPulses2 = totalPulses2;
    lastSdSavedPulses3 = totalPulses3;
}

bool sensorFlow_begin() {
    loadConfig();
    loadTotalsFromSd();   // SD first — has subtotals + date
    loadTotalsFromNvs();  // NVS second — may have a more recent total

    pinMode(PIN_FLOW_1, INPUT_PULLUP);
    pinMode(PIN_FLOW_2, INPUT_PULLUP);
    pinMode(PIN_FLOW_3, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_FLOW_1), isrFlow1, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_FLOW_2), isrFlow2, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_FLOW_3), isrFlow3, FALLING);

    Serial.printf("[FLOW] started — total1=%.3fL total2=%.3fL total3=%.3fL\n",
                  flowTotal1, flowTotal2, flowTotal3);
    return true;
}

void sensorFlow_loop() {
    unsigned long now = millis();
    if (now - lastCalcMs < CALC_INTERVAL_MS) {
        return;
    }
    lastCalcMs = now;

    // Calibration safety: auto-abort if no flow detected within idle timeout
    checkCalTimeout(1);
    checkCalTimeout(2);
    checkCalTimeout(3);

    // Snapshot and clear pulse counters atomically
    uint32_t p1 = 0, p2 = 0, p3 = 0;
    noInterrupts();
    p1 = rawPulses1; rawPulses1 = 0;
    p2 = rawPulses2; rawPulses2 = 0;
    p3 = rawPulses3; rawPulses3 = 0;
    interrupts();

    totalPulses1 += p1;
    totalPulses2 += p2;
    totalPulses3 += p3;

    uint32_t meteredP1 = (p1 >= minPulsesPerIntervalCh1) ? p1 : 0U;
    uint32_t meteredP2 = (p2 >= minPulsesPerIntervalCh2) ? p2 : 0U;
    uint32_t meteredP3 = (p3 >= minPulsesPerIntervalCh3) ? p3 : 0U;

    // 1) Push raw flow rate using last cycle's appliedK
    uint8_t sampleSlot1 = pushFlowSample(((float)meteredP1 * 60.0f) / appliedK1,
                                         flowAvgSamples1,
                                         flowAvgHead1,
                                         flowAvgCount1,
                                         flowAvgWindow);
    uint8_t sampleSlot2 = pushFlowSample(((float)meteredP2 * 60.0f) / appliedK2,
                                         flowAvgSamples2,
                                         flowAvgHead2,
                                         flowAvgCount2,
                                         flowAvgWindow);
    uint8_t sampleSlot3 = pushFlowSample(((float)meteredP3 * 60.0f) / appliedK3,
                                         flowAvgSamples3,
                                         flowAvgHead3,
                                         flowAvgCount3,
                                         flowAvgWindow);

    // 2) Smooth → interpolateK() → new appliedK
    float smoothedFlow1 = averageFlowSamples(flowAvgSamples1, flowAvgCount1);
    float smoothedFlow2 = averageFlowSamples(flowAvgSamples2, flowAvgCount2);
    float smoothedFlow3 = averageFlowSamples(flowAvgSamples3, flowAvgCount3);
    appliedK1 = interpolateK(smoothedFlow1, kTable1, kTableLen1);
    appliedK2 = interpolateK(smoothedFlow2, kTable2, kTableLen2);
    appliedK3 = interpolateK(smoothedFlow3, kTable3, kTableLen3);

    // 3) Recompute litres with new appliedK, update ring buffer slot once
    float litres1 = pulsesToLitres(meteredP1, appliedK1);  // pulses / (pulses/L) = L
    float litres2 = pulsesToLitres(meteredP2, appliedK2);
    float litres3 = pulsesToLitres(meteredP3, appliedK3);

    flowAvgSamples1[sampleSlot1] = litres1 * 60.0f;
    flowAvgSamples2[sampleSlot2] = litres2 * 60.0f;
    flowAvgSamples3[sampleSlot3] = litres3 * 60.0f;

    // 4) Compute final flowRate
    flowRate1 = averageFlowSamples(flowAvgSamples1, flowAvgCount1);
    flowRate2 = averageFlowSamples(flowAvgSamples2, flowAvgCount2);
    flowRate3 = averageFlowSamples(flowAvgSamples3, flowAvgCount3);

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
                flowToday3  = 0.0f;
                lastUnixDay = unixDay;
            }
            if (curMonday != lastMondayDay) {
                flowWeek1     = 0.0f;
                flowWeek2     = 0.0f;
                flowWeek3     = 0.0f;
                lastMondayDay = curMonday;
            }
            if (curMonth != lastMonth) {
                flowMonth1 = 0.0f;
                flowMonth2 = 0.0f;
                flowMonth3 = 0.0f;
                lastMonth  = curMonth;
            }
            if (curYear != lastYear) {
                flowYear1 = 0.0f;
                flowYear2 = 0.0f;
                flowYear3 = 0.0f;
                lastYear  = curYear;
            }
        }
    }

    flowTotal1 += litres1;
    flowTotal2 += litres2;
    flowTotal3 += litres3;
    flowToday1 += litres1;
    flowToday2 += litres2;
    flowToday3 += litres3;
    flowWeek1  += litres1;
    flowWeek2  += litres2;
    flowWeek3  += litres3;
    flowMonth1 += litres1;
    flowMonth2 += litres2;
    flowMonth3 += litres3;
    flowYear1  += litres1;
    flowYear2  += litres2;
    flowYear3  += litres3;

    // NVS save — frequent, survives power loss
    if (now - lastNvsSaveMs >= NVS_FLOW_SAVE_INTERVAL_MS) {
        lastNvsSaveMs = now;
        if (flowTotal1 != lastNvsSavedTotal1 || flowTotal2 != lastNvsSavedTotal2 ||
            flowTotal3 != lastNvsSavedTotal3 ||
            totalPulses1 != lastNvsSavedPulses1 || totalPulses2 != lastNvsSavedPulses2 ||
            totalPulses3 != lastNvsSavedPulses3) {
            saveToNvs();
        }
    }

    // SD save — less frequent, keeps subtotals + date for next boot
    if (now - lastSdSaveMs >= DATA_LOG_INTERVAL_MS) {
        lastSdSaveMs = now;
        if (flowTotal1 != lastSdSavedTotal1 || flowTotal2 != lastSdSavedTotal2 ||
            flowTotal3 != lastSdSavedTotal3 ||
            totalPulses1 != lastSdSavedPulses1 || totalPulses2 != lastSdSavedPulses2 ||
            totalPulses3 != lastSdSavedPulses3) {
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
    } else if (ch == 2) {
        nominalK2 = k;
        setSinglePointTable(kTable2, kTableLen2, k);
        appliedK2 = interpolateK(flowRate2, kTable2, kTableLen2);
    } else if (ch == 3) {
        nominalK3 = k;
        setSinglePointTable(kTable3, kTableLen3, k);
        appliedK3 = interpolateK(flowRate3, kTable3, kTableLen3);
    } else return false;

    Serial.printf("[FLOW] K%d set to %.0f\n", ch, k);

    persistKConfig();
    return true;
}

static void forceSdSave() {
    lastSdSavedTotal1 = -1.0f;
    lastSdSavedTotal2 = -1.0f;
    lastSdSavedTotal3 = -1.0f;
    lastSdSavedPulses1 = UINT64_MAX;
    lastSdSavedPulses2 = UINT64_MAX;
    lastSdSavedPulses3 = UINT64_MAX;
    saveToSd();
}

static void logAndPrint(const char* msg) {
    storeSd_logEvent(msg);
    Serial.println(msg);
}

void sensorFlow_resetToday(uint8_t ch) {
    if (ch == 0 || ch == 1) flowToday1 = 0.0f;
    if (ch == 0 || ch == 2) flowToday2 = 0.0f;
    if (ch == 0 || ch == 3) flowToday3 = 0.0f;
    forceSdSave();
    char msg[48]; snprintf(msg, sizeof(msg), "[FLOW] reset today ch=%d", ch);
    logAndPrint(msg);
}

void sensorFlow_resetWeek(uint8_t ch) {
    if (ch == 0 || ch == 1) flowWeek1 = 0.0f;
    if (ch == 0 || ch == 2) flowWeek2 = 0.0f;
    if (ch == 0 || ch == 3) flowWeek3 = 0.0f;
    forceSdSave();
    char msg[48]; snprintf(msg, sizeof(msg), "[FLOW] reset week ch=%d", ch);
    logAndPrint(msg);
}

void sensorFlow_resetMonth(uint8_t ch) {
    if (ch == 0 || ch == 1) flowMonth1 = 0.0f;
    if (ch == 0 || ch == 2) flowMonth2 = 0.0f;
    if (ch == 0 || ch == 3) flowMonth3 = 0.0f;
    forceSdSave();
    char msg[48]; snprintf(msg, sizeof(msg), "[FLOW] reset month ch=%d", ch);
    logAndPrint(msg);
}

void sensorFlow_resetYear(uint8_t ch) {
    if (ch == 0 || ch == 1) flowYear1 = 0.0f;
    if (ch == 0 || ch == 2) flowYear2 = 0.0f;
    if (ch == 0 || ch == 3) flowYear3 = 0.0f;
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
    if (ch == 0 || ch == 3) {
        flowTotal3 = flowToday3 = flowWeek3 = flowMonth3 = flowYear3 = 0.0f;
        totalPulses3 = 0;
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
    if (ch == 0 || ch == 3) {
        prefs.putFloat("t3", 0.0f);
        prefs.putULong64("p3", 0ULL);
        lastNvsSavedTotal3 = 0.0f;
        lastNvsSavedPulses3 = 0;
    }
    prefs.end();
    forceSdSave();
    char msg[48]; snprintf(msg, sizeof(msg), "[FLOW] reset totals ch=%d", ch);
    logAndPrint(msg);
}

void sensorFlow_factoryReset() {
    flowRate1 = flowRate2 = flowRate3 = 0.0f;
    flowTotal1 = flowToday1 = flowWeek1 = flowMonth1 = flowYear1 = 0.0f;
    flowTotal2 = flowToday2 = flowWeek2 = flowMonth2 = flowYear2 = 0.0f;
    flowTotal3 = flowToday3 = flowWeek3 = flowMonth3 = flowYear3 = 0.0f;
    totalPulses1 = totalPulses2 = totalPulses3 = 0;
    for (uint8_t i = 0; i < FLOW_AVG_WINDOW_MAX; ++i) {
        flowAvgSamples1[i] = 0.0f;
        flowAvgSamples2[i] = 0.0f;
        flowAvgSamples3[i] = 0.0f;
    }
    flowAvgHead1 = flowAvgHead2 = flowAvgHead3 = 0;
    flowAvgCount1 = flowAvgCount2 = flowAvgCount3 = 0;
    strlcpy(s_modelName1, FLOW_SENSOR_MODEL_DEFAULT_CH1, sizeof(s_modelName1));
    strlcpy(s_modelName2, FLOW_SENSOR_MODEL_DEFAULT_CH2, sizeof(s_modelName2));
    strlcpy(s_modelName3, FLOW_SENSOR_MODEL_DEFAULT_CH3, sizeof(s_modelName3));
    debounceUsCh1 = DEBOUNCE_US_DEFAULT_CH1;
    debounceUsCh2 = DEBOUNCE_US_DEFAULT_CH2;
    debounceUsCh3 = DEBOUNCE_US_DEFAULT_CH3;
    minPulsesPerIntervalCh1 = 1U;
    minPulsesPerIntervalCh2 = 2U;
    minPulsesPerIntervalCh3 = 1U;
    flowAvgWindow = FLOW_AVG_WINDOW_DEFAULT;
    appliedK1 = interpolateK(0.0f, kTable1, kTableLen1);
    appliedK2 = interpolateK(0.0f, kTable2, kTableLen2);
    appliedK3 = interpolateK(0.0f, kTable3, kTableLen3);
    prefs.begin("flow", false);
    prefs.clear();
    prefs.end();
    lastNvsSavedTotal1 = lastNvsSavedTotal2 = lastNvsSavedTotal3 = 0.0f;
    lastNvsSavedPulses1 = lastNvsSavedPulses2 = lastNvsSavedPulses3 = 0;
    forceSdSave();
    logAndPrint("[FLOW] factory reset — all flow data cleared");
}

// ── Per-channel calibration ─────────────────────────────────────────────────

enum class FlowCalState : uint8_t { IDLE, COLLECTING, DONE };

static FlowCalState   calState1        = FlowCalState::IDLE;
static FlowCalState   calState2        = FlowCalState::IDLE;
static FlowCalState   calState3        = FlowCalState::IDLE;
static uint64_t       calBasePulses1   = 0, calBasePulses2   = 0, calBasePulses3   = 0;
static float          calRefVol1       = 0.0f, calRefVol2    = 0.0f, calRefVol3    = 0.0f;
static float          calSuggestedK1   = 0.0f, calSuggestedK2 = 0.0f, calSuggestedK3 = 0.0f;
static unsigned long  calStartMs1      = 0, calStartMs2      = 0, calStartMs3      = 0;

// Brief error display — set on timeout/bad-commit; getCalState() returns these for FLOW_CAL_ERROR_HOLD_MS
static char          calErrMsg1[24]   = "";
static char          calErrMsg2[24]   = "";
static char          calErrMsg3[24]   = "";
static unsigned long calErrUntilMs1   = 0;
static unsigned long calErrUntilMs2   = 0;
static unsigned long calErrUntilMs3   = 0;

static void setCalError(uint8_t ch, const char* msg) {
    if      (ch == 1) { strlcpy(calErrMsg1, msg, sizeof(calErrMsg1)); calErrUntilMs1 = millis() + FLOW_CAL_ERROR_HOLD_MS; }
    else if (ch == 2) { strlcpy(calErrMsg2, msg, sizeof(calErrMsg2)); calErrUntilMs2 = millis() + FLOW_CAL_ERROR_HOLD_MS; }
    else              { strlcpy(calErrMsg3, msg, sizeof(calErrMsg3)); calErrUntilMs3 = millis() + FLOW_CAL_ERROR_HOLD_MS; }
}

void sensorFlow_calBegin(uint8_t ch) {
    if (ch == 1) {
        calBasePulses1 = totalPulses1;
        calStartMs1    = millis();
        calErrMsg1[0]  = '\0';
        calState1      = FlowCalState::COLLECTING;
    } else if (ch == 2) {
        calBasePulses2 = totalPulses2;
        calStartMs2    = millis();
        calErrMsg2[0]  = '\0';
        calState2      = FlowCalState::COLLECTING;
    } else {
        calBasePulses3 = totalPulses3;
        calStartMs3    = millis();
        calErrMsg3[0]  = '\0';
        calState3      = FlowCalState::COLLECTING;
    }
}

bool sensorFlow_calCommit(uint8_t ch) {
    FlowCalState& st  = (ch == 1) ? calState1      : (ch == 2 ? calState2      : calState3);
    uint64_t&     bp  = (ch == 1) ? calBasePulses1  : (ch == 2 ? calBasePulses2  : calBasePulses3);
    float&        ref = (ch == 1) ? calRefVol1      : (ch == 2 ? calRefVol2      : calRefVol3);
    float&        sug = (ch == 1) ? calSuggestedK1  : (ch == 2 ? calSuggestedK2  : calSuggestedK3);
    uint64_t      cur = (ch == 1) ? totalPulses1    : (ch == 2 ? totalPulses2    : totalPulses3);

    if (st != FlowCalState::COLLECTING) {
        Serial.printf("[FLOW] calCommit ch=%d: not collecting\n", ch);
        return false;
    }
    if (ref <= 0.0f) {
        Serial.printf("[FLOW] calCommit ch=%d: ref vol is zero\n", ch);
        return false;
    }
    uint64_t pulses = cur - bp;
    if (pulses < FLOW_CAL_MIN_PULSES) {
        Serial.printf("[FLOW] calCommit ch=%d: only %llu pulses — need %lu min\n",
                      ch, (unsigned long long)pulses, (unsigned long)FLOW_CAL_MIN_PULSES);
        setCalError(ch, "too_few_pulses");
        // Stay COLLECTING — user can let more water through then commit again
        return false;
    }
    sug = (float)pulses / ref;
    st  = FlowCalState::DONE;
    return true;
}

bool sensorFlow_calAccept(uint8_t ch) {
    FlowCalState& st  = (ch == 1) ? calState1     : (ch == 2 ? calState2     : calState3);
    float&        sug = (ch == 1) ? calSuggestedK1 : (ch == 2 ? calSuggestedK2 : calSuggestedK3);
    if (st != FlowCalState::DONE) return false;
    sensorFlow_setKFactor(ch, sug);
    st = FlowCalState::IDLE;
    return true;
}

void sensorFlow_calAbort(uint8_t ch) {
    if      (ch == 1) { calState1 = FlowCalState::IDLE; calErrMsg1[0] = '\0'; }
    else if (ch == 2) { calState2 = FlowCalState::IDLE; calErrMsg2[0] = '\0'; }
    else              { calState3 = FlowCalState::IDLE; calErrMsg3[0] = '\0'; }
}

void sensorFlow_setCalRefVol(uint8_t ch, float volL) {
    if      (ch == 1) calRefVol1 = volL;
    else if (ch == 2) calRefVol2 = volL;
    else              calRefVol3 = volL;
}

// Called from sensorFlow_loop() on each 1s tick to enforce the idle timeout
static void checkCalTimeout(uint8_t ch) {
    FlowCalState& st = (ch == 1) ? calState1 : (ch == 2 ? calState2 : calState3);
    if (st != FlowCalState::COLLECTING) return;

    uint64_t basePulses = (ch == 1) ? calBasePulses1 : (ch == 2 ? calBasePulses2 : calBasePulses3);
    uint64_t curPulses  = (ch == 1) ? totalPulses1   : (ch == 2 ? totalPulses2   : totalPulses3);
    unsigned long startMs = (ch == 1) ? calStartMs1  : (ch == 2 ? calStartMs2  : calStartMs3);

    // No pulses at all and timeout expired → auto-abort
    if ((curPulses - basePulses) < FLOW_CAL_MIN_PULSES &&
        (millis() - startMs) > FLOW_CAL_IDLE_TIMEOUT_MS) {
        Serial.printf("[FLOW] cal ch%d: auto-abort — no flow detected in %lus\n",
                      ch, (unsigned long)(FLOW_CAL_IDLE_TIMEOUT_MS / 1000));
        storeSd_logEvent("[CAL] flow cal auto-aborted: no flow timeout");
        setCalError(ch, "timed_out");
        st = FlowCalState::IDLE;
    }
}

const char* sensorFlow_getCalState(uint8_t ch) {
    // Brief error display overrides real state so HA/OLED can show what went wrong
    const char* err    = (ch == 1) ? calErrMsg1 : (ch == 2 ? calErrMsg2 : calErrMsg3);
    unsigned long until = (ch == 1) ? calErrUntilMs1 : (ch == 2 ? calErrUntilMs2 : calErrUntilMs3);
    if (err[0] && millis() < until) return err;

    FlowCalState st = (ch == 1) ? calState1 : (ch == 2 ? calState2 : calState3);
    switch (st) {
        case FlowCalState::COLLECTING: return "collecting";
        case FlowCalState::DONE:       return "done";
        default:                       return "idle";
    }
}

// Seconds until auto-abort fires; -1 if not applicable (has pulses or not collecting)
int sensorFlow_getCalSecsUntilTimeout(uint8_t ch) {
    FlowCalState st = (ch == 1) ? calState1 : (ch == 2 ? calState2 : calState3);
    if (st != FlowCalState::COLLECTING) return -1;
    uint64_t pulses = (ch == 1) ? (totalPulses1 - calBasePulses1)
                    : (ch == 2) ? (totalPulses2 - calBasePulses2)
                                : (totalPulses3 - calBasePulses3);
    if (pulses >= FLOW_CAL_MIN_PULSES) return -1;  // enough pulses, no timeout risk
    unsigned long startMs = (ch == 1) ? calStartMs1 : (ch == 2 ? calStartMs2 : calStartMs3);
    long elapsed = (long)(millis() - startMs);
    long remaining = (long)FLOW_CAL_IDLE_TIMEOUT_MS - elapsed;
    return (remaining > 0) ? (int)(remaining / 1000) : 0;
}

float    sensorFlow_getCalSuggestedK(uint8_t ch) {
    return ch == 1 ? calSuggestedK1 : (ch == 2 ? calSuggestedK2 : calSuggestedK3);
}
uint64_t sensorFlow_getCalPulsesSinceStart(uint8_t ch) {
    if (ch == 1) return (calState1 == FlowCalState::IDLE) ? 0 : (totalPulses1 - calBasePulses1);
    if (ch == 2) return (calState2 == FlowCalState::IDLE) ? 0 : (totalPulses2 - calBasePulses2);
    return (calState3 == FlowCalState::IDLE) ? 0 : (totalPulses3 - calBasePulses3);
}
float    sensorFlow_getCalRefVol(uint8_t ch) {
    return ch == 1 ? calRefVol1 : (ch == 2 ? calRefVol2 : calRefVol3);
}

// ────────────────────────────────────────────────────────────────────────────

float sensorFlow_getRateLpm(uint8_t ch)  {
    return ch == 1 ? flowRate1  : (ch == 2 ? flowRate2  : flowRate3);
}
float sensorFlow_getTotalL(uint8_t ch)   {
    return ch == 1 ? flowTotal1 : (ch == 2 ? flowTotal2 : flowTotal3);
}
uint64_t sensorFlow_getTotalPulses(uint8_t ch) {
    return ch == 1 ? totalPulses1 : (ch == 2 ? totalPulses2 : totalPulses3);
}
float sensorFlow_getTodayL(uint8_t ch)   {
    return ch == 1 ? flowToday1 : (ch == 2 ? flowToday2 : flowToday3);
}
float sensorFlow_getWeekL(uint8_t ch)    {
    return ch == 1 ? flowWeek1  : (ch == 2 ? flowWeek2  : flowWeek3);
}
float sensorFlow_getMonthL(uint8_t ch)   {
    return ch == 1 ? flowMonth1 : (ch == 2 ? flowMonth2 : flowMonth3);
}
float sensorFlow_getYearL(uint8_t ch)    {
    return ch == 1 ? flowYear1  : (ch == 2 ? flowYear2  : flowYear3);
}
float sensorFlow_getKFactor(uint8_t ch)  {
    return ch == 1 ? nominalK1  : (ch == 2 ? nominalK2  : nominalK3);
}
float sensorFlow_getAppliedKFactor(uint8_t ch) {
    return ch == 1 ? appliedK1  : (ch == 2 ? appliedK2  : appliedK3);
}
uint32_t sensorFlow_getDebounceUs(uint8_t ch) {
    return ch == 1 ? debounceUsCh1 : (ch == 2 ? debounceUsCh2 : debounceUsCh3);
}
float sensorFlow_getFlowAvgWindowRate(uint8_t ch) {
    if (ch == 1) return averageFlowSamples(flowAvgSamples1, flowAvgCount1);
    if (ch == 2) return averageFlowSamples(flowAvgSamples2, flowAvgCount2);
    return averageFlowSamples(flowAvgSamples3, flowAvgCount3);
}
uint8_t sensorFlow_getFlowAvgWindow() { return flowAvgWindow; }

const char* sensorFlow_getKTableJson(uint8_t ch) {
    static char buf[512]; // enough for 5 points with ~50 chars each
    const FlowKPoint* table = (ch == 1) ? kTable1 : (ch == 2 ? kTable2 : kTable3);
    int len = (ch == 1) ? kTableLen1 : (ch == 2 ? kTableLen2 : kTableLen3);

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < len; ++i) {
        JsonObject pt = arr.add<JsonObject>();
        pt["flow_lpm"] = table[i].flowLpm;
        pt["k"] = table[i].kPulsesPerL;
    }
    size_t written = serializeJson(doc, buf, sizeof(buf));
    if (written == 0 || written >= sizeof(buf)) {
        buf[0] = '\0';
    }
    return buf;
}

bool sensorFlow_setKTable(uint8_t ch, const char* json) {
    // Parse JSON array of {flow_lpm, k} objects
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[FLOW] setKTable ch=%d JSON parse error: %s\n", ch, err.c_str());
        return false;
    }
    JsonArrayConst arr = doc.as<JsonArrayConst>();
    if (!arr) {
        Serial.printf("[FLOW] setKTable ch=%d: expected JSON array\n", ch);
        return false;
    }

    FlowKPoint* table = (ch == 1) ? kTable1 : (ch == 2 ? kTable2 : kTable3);
    int& len = (ch == 1) ? kTableLen1 : (ch == 2 ? kTableLen2 : kTableLen3);
    float& nomK = (ch == 1) ? nominalK1 : (ch == 2 ? nominalK2 : nominalK3);
    float& appK = (ch == 1) ? appliedK1 : (ch == 2 ? appliedK2 : appliedK3);
    float& rate = (ch == 1) ? flowRate1 : (ch == 2 ? flowRate2 : flowRate3);

    int loaded = loadKTableFromJson(arr, table);
    if (loaded <= 0) {
        Serial.printf("[FLOW] setKTable ch=%d: no valid points in JSON\n", ch);
        return false;
    }
    len = loaded;

    // Update nominal K from the first point's K (backward compat)
    nomK = table[0].kPulsesPerL;
    appK = interpolateK(rate, table, len);
    Serial.printf("[FLOW] setKTable ch=%d: loaded %d points\n", ch, loaded);
    persistKConfig();
    return true;
}

bool sensorFlow_setDebounceUs(uint8_t ch, uint32_t us) {
    if (us < 100 || us > 10000) {
        Serial.printf("[FLOW] setDebounceUs ch=%d: %lu out of range (100-10000)\n", ch, (unsigned long)us);
        return false;
    }
    if      (ch == 1) debounceUsCh1 = us;
    else if (ch == 2) debounceUsCh2 = us;
    else if (ch == 3) debounceUsCh3 = us;
    else return false;

    Serial.printf("[FLOW] debounce ch%d set to %lu us\n", ch, (unsigned long)us);

    // Persist to node.json
    JsonDocument doc;
    storeSd_readJsonFile(SD_CONFIG_PATH, doc);
    JsonObject flow = doc["flow"].as<JsonObject>();
    if (flow.isNull()) flow = doc["flow"].to<JsonObject>();
    if (ch == 1) flow["debounce_us_1"] = (unsigned long)us;
    else if (ch == 2) flow["debounce_us_2"] = (unsigned long)us;
    else flow["debounce_us_3"] = (unsigned long)us;
    storeSd_writeJsonFile(SD_CONFIG_PATH, doc);
    return true;
}

bool sensorFlow_setModel(uint8_t ch, const char* model) {
    const FlowSensorModel* m = findModel(model);
    if (!m) {
        Serial.printf("[FLOW] setModel ch=%d: unknown model '%s'\n", ch, model);
        return false;
    }
    if (ch == 1) {
        strlcpy(s_modelName1, model, sizeof(s_modelName1));
        nominalK1 = m->nominalK;
        debounceUsCh1 = m->debounceUs;
        minPulsesPerIntervalCh1 = m->minPulses;
        memcpy(kTable1, m->kTable, m->kTableLen * sizeof(FlowKPoint));
        kTableLen1 = m->kTableLen;
        appliedK1 = interpolateK(flowRate1, kTable1, kTableLen1);
    } else if (ch == 2) {
        strlcpy(s_modelName2, model, sizeof(s_modelName2));
        nominalK2 = m->nominalK;
        debounceUsCh2 = m->debounceUs;
        minPulsesPerIntervalCh2 = m->minPulses;
        memcpy(kTable2, m->kTable, m->kTableLen * sizeof(FlowKPoint));
        kTableLen2 = m->kTableLen;
        appliedK2 = interpolateK(flowRate2, kTable2, kTableLen2);
    } else if (ch == 3) {
        strlcpy(s_modelName3, model, sizeof(s_modelName3));
        nominalK3 = m->nominalK;
        debounceUsCh3 = m->debounceUs;
        minPulsesPerIntervalCh3 = m->minPulses;
        memcpy(kTable3, m->kTable, m->kTableLen * sizeof(FlowKPoint));
        kTableLen3 = m->kTableLen;
        appliedK3 = interpolateK(flowRate3, kTable3, kTableLen3);
    } else {
        return false;
    }
    Serial.printf("[FLOW] ch%d model→'%s' K=%.0f debounce=%lu minP=%lu\n",
                  ch, model,
                  ch == 1 ? nominalK1 : nominalK2,
                  (unsigned long)(ch == 1 ? debounceUsCh1 : debounceUsCh2),
                  (unsigned long)(ch == 1 ? minPulsesPerIntervalCh1 : minPulsesPerIntervalCh2));
    persistKConfig();
    return true;
}

const char* sensorFlow_getModel(uint8_t ch) {
    return ch == 1 ? s_modelName1 : (ch == 2 ? s_modelName2 : s_modelName3);
}

void sensorFlow_getModelList(char* buf, size_t len) {
    buf[0] = '\0';
    for (int i = 0; i < s_sensorModelCount; ++i) {
        if (i > 0) strlcat(buf, ",", len);
        strlcat(buf, s_sensorModels[i].name, len);
    }
}

bool sensorFlow_setFlowAvgWindow(uint8_t windowSize) {
    if (windowSize < 1 || windowSize > FLOW_AVG_WINDOW_MAX) {
        Serial.printf("[FLOW] setFlowAvgWindow: %u out of range (1-%d)\n", windowSize, FLOW_AVG_WINDOW_MAX);
        return false;
    }
    flowAvgWindow = windowSize;

    // Clamp ring buffer counts to new window size
    if (flowAvgCount1 > windowSize) flowAvgCount1 = windowSize;
    if (flowAvgCount2 > windowSize) flowAvgCount2 = windowSize;
    if (flowAvgCount3 > windowSize) flowAvgCount3 = windowSize;

    Serial.printf("[FLOW] flow avg window set to %u\n", windowSize);

    // Persist to node.json
    JsonDocument doc;
    storeSd_readJsonFile(SD_CONFIG_PATH, doc);
    JsonObject flow = doc["flow"].as<JsonObject>();
    if (flow.isNull()) flow = doc["flow"].to<JsonObject>();
    flow["flow_avg_window"] = windowSize;
    storeSd_writeJsonFile(SD_CONFIG_PATH, doc);
    return true;
}
