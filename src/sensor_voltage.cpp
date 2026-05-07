#include "sensor_voltage.h"

#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <Preferences.h>

#include "store_sd.h"
#include "watchdog.h"

// Voltage divider: R1=100kΩ, R2=33kΩ → ratio = (R1+R2)/R2
static constexpr float DIVIDER_RATIO = (100000.0f + 33000.0f) / 33000.0f;

// NVS defaults
static constexpr float DEFAULT_V_MIN  = 11.8f;
static constexpr float DEFAULT_V_MAX  = 12.6f;
static constexpr float DEFAULT_CAL    = 1.0f;

// Smoothing: 5-sample moving average over readings taken every 5 s
static constexpr uint8_t  AVG_SAMPLES  = 5;
// State window: compare latest averaged reading to one 60 s ago
// At one reading per 5 s, 12 slots = 60 s of history
static constexpr uint8_t  STATE_SLOTS  = 12;
static constexpr float    STATE_THRESH = 0.05f; // V change over 60 s to flag as charging/discharging
static constexpr uint32_t READ_INTERVAL_MS = 5000;

static Adafruit_ADS1115 ads;
static Preferences      prefs;

static float s_vMin      = DEFAULT_V_MIN;
static float s_vMax      = DEFAULT_V_MAX;
static float s_calFactor = DEFAULT_CAL;

// Moving average buffer
static float    s_avgBuf[AVG_SAMPLES] = {};
static uint8_t  s_avgIdx              = 0;
static uint8_t  s_avgCount            = 0;

// State ring buffer (stores averaged voltage readings for trend detection)
static float    s_stateBuf[STATE_SLOTS] = {};
static uint8_t  s_stateIdx             = 0;
static uint8_t  s_stateCount           = 0;

static float s_voltageV   = 0.0f;
static float s_dividerVoltageV = 0.0f;
static float s_percentPct = 0.0f;
static const char* s_state = "Stable";

static unsigned long s_lastReadMs = 0;
static bool s_ready = false;

static void loadNvs() {
    prefs.begin("voltage", true);
    s_vMin      = prefs.getFloat("v_min", DEFAULT_V_MIN);
    s_vMax      = prefs.getFloat("v_max", DEFAULT_V_MAX);
    s_calFactor = prefs.getFloat("cal",   DEFAULT_CAL);
    prefs.end();
}

static void saveNvs() {
    prefs.begin("voltage", false);
    prefs.putFloat("v_min", s_vMin);
    prefs.putFloat("v_max", s_vMax);
    prefs.putFloat("cal",   s_calFactor);
    prefs.end();
}

static float computePercent(float v) {
    if (s_vMax <= s_vMin) return 0.0f;
    return constrain((v - s_vMin) / (s_vMax - s_vMin) * 100.0f, 0.0f, 100.0f);
}

static const char* computeState() {
    if (s_stateCount < STATE_SLOTS) return "Stable";
    // oldest entry is one slot ahead of current write index
    uint8_t oldestIdx = (s_stateIdx) % STATE_SLOTS;
    float oldest = s_stateBuf[oldestIdx];
    float delta  = s_voltageV - oldest;
    if (delta >  STATE_THRESH) return "Charging";
    if (delta < -STATE_THRESH) return "Discharging";
    return "Stable";
}

bool sensorVoltage_begin() {
    loadNvs();

    if (!ads.begin()) {
        storeSd_logEvent("[VOLTAGE] ADS1115 not found on I2C bus");
        Serial.println("[VOLTAGE] ADS1115 not found — check wiring");
        return false;
    }

    ads.setGain(GAIN_ONE); // ±4.096V — safe for up to ~16.5V battery input via divider

    s_ready = true;
    Serial.println("[VOLTAGE] begin ok");
    storeSd_logEvent("[VOLTAGE] begin ok");
    return true;
}

void sensorVoltage_loop() {
    if (!s_ready) return;

    unsigned long now = millis();
    if (now - s_lastReadMs < READ_INTERVAL_MS) return;
    s_lastReadMs = now;

    watchdog_feed();

    int16_t raw = ads.readADC_SingleEnded(0);
    float adcV  = ads.computeVolts(raw);
    s_dividerVoltageV = adcV;
    float batt  = adcV * DIVIDER_RATIO * s_calFactor;

    // Moving average
    s_avgBuf[s_avgIdx] = batt;
    s_avgIdx = (s_avgIdx + 1) % AVG_SAMPLES;
    if (s_avgCount < AVG_SAMPLES) s_avgCount++;

    float sum = 0.0f;
    for (uint8_t i = 0; i < s_avgCount; i++) sum += s_avgBuf[i];
    s_voltageV = sum / s_avgCount;

    // State ring buffer
    s_stateBuf[s_stateIdx] = s_voltageV;
    s_stateIdx = (s_stateIdx + 1) % STATE_SLOTS;
    if (s_stateCount < STATE_SLOTS) s_stateCount++;

    s_percentPct = computePercent(s_voltageV);
    s_state      = computeState();

    Serial.print("[VOLTAGE] raw_divider=");
    Serial.print(s_dividerVoltageV, 3);
    Serial.print("V battery=");
    Serial.print(s_voltageV, 3);
    Serial.print("V cal=");
    Serial.println(s_calFactor, 3);
}

float sensorVoltage_getVoltageV()   { return s_voltageV; }
float sensorVoltage_getDividerVoltageV() { return s_dividerVoltageV; }
float sensorVoltage_getPercentPct() { return s_percentPct; }
const char* sensorVoltage_getState() { return s_state; }

float sensorVoltage_getVMin()      { return s_vMin; }
float sensorVoltage_getVMax()      { return s_vMax; }
float sensorVoltage_getCalFactor() { return s_calFactor; }

bool sensorVoltage_setVMin(float v) {
    if (v < 9.0f || v >= s_vMax) return false;
    s_vMin = v;
    saveNvs();
    return true;
}

bool sensorVoltage_setVMax(float v) {
    if (v > 16.0f || v <= s_vMin) return false;
    s_vMax = v;
    saveNvs();
    return true;
}

bool sensorVoltage_setCalFactor(float f) {
    if (f < 0.8f || f > 1.2f) return false;
    s_calFactor = f;
    saveNvs();
    return true;
}
