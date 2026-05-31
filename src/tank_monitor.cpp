#include "tank_monitor.h"

#include <Preferences.h>
#include "config.h"
#include "store_sd.h"

static float   s_levelL      = 0.0f;
static float   s_capacityL   = TANK_CAPACITY_DEFAULT_L;
static bool    s_isFull      = false;

// Flow-stop timer: how long RO output (Ch2) has been below threshold
static unsigned long s_flowStopStartMs = 0;
static bool          s_flowStopped     = false;

// NVS persistence
static Preferences   s_prefs;
static unsigned long s_lastNvsSaveMs = 0;

static void saveToNvs() {
    s_prefs.begin("tank", false);
    s_prefs.putFloat(NVS_TANK_KEY,          s_levelL);
    s_prefs.putFloat(NVS_TANK_CAPACITY_KEY, s_capacityL);
    s_prefs.end();
}

void tankMonitor_begin() {
    s_prefs.begin("tank", true);
    float savedLevel    = s_prefs.getFloat(NVS_TANK_KEY,          -1.0f);
    float savedCapacity = s_prefs.getFloat(NVS_TANK_CAPACITY_KEY, -1.0f);
    s_prefs.end();

    if (savedCapacity >= 5.0f && savedCapacity <= 30.0f) {
        s_capacityL = savedCapacity;
    }
    if (savedLevel >= 0.0f && savedLevel <= s_capacityL) {
        s_levelL = savedLevel;
    }

    Serial.printf("[TANK] started — level=%.2fL capacity=%.2fL\n", s_levelL, s_capacityL);
}

void tankMonitor_loop() {
    // NVS save every TANK_NVS_SAVE_INTERVAL_MS
    unsigned long now = millis();
    if (now - s_lastNvsSaveMs >= TANK_NVS_SAVE_INTERVAL_MS) {
        s_lastNvsSaveMs = now;
        saveToNvs();
    }
}

void tankMonitor_feedFlow(float ch2LpmPureOut, float ch1LpmOut) {
    unsigned long now = millis();

    // Integrate one second of flow (called every 1s from main loop)
    float deltaL = (ch2LpmPureOut / 60.0f) - (ch1LpmOut / 60.0f);
    s_levelL += deltaL;

    // Clamp to [0, capacity]
    if (s_levelL < 0.0f) s_levelL = 0.0f;
    if (s_levelL > s_capacityL * 1.05f) s_levelL = s_capacityL * 1.05f;  // allow slight overrun before snap

    // Flow-stop tracking (Ch2 only — RO output stopping means the tank is no longer filling)
    bool pureOutLow = (ch2LpmPureOut < TANK_FULL_STOP_THRESHOLD_LPM);
    if (pureOutLow) {
        if (!s_flowStopped) {
            s_flowStopStartMs = now;
            s_flowStopped = true;
        }
    } else {
        s_flowStopped = false;
        s_flowStopStartMs = 0;
        s_isFull = false;
    }

    // Declare full: flow stopped long enough AND level ≥ 90% capacity
    if (s_flowStopped &&
        (now - s_flowStopStartMs >= TANK_FULL_STOP_DURATION_MS) &&
        (s_levelL >= s_capacityL * TANK_FULL_LEVEL_FRACTION)) {

        if (!s_isFull) {
            // Auto-correct: snap to capacity to cancel integration drift
            Serial.printf("[TANK] full detected — snapping level %.2f→%.2fL\n",
                          s_levelL, s_capacityL);
            s_levelL = s_capacityL;
            s_isFull = true;
            storeSd_logEvent("[TANK] tank full detected — level snapped to capacity");
        }
    }

    // If level drops very low while no inflow, treat as empty (tap draining)
    if (s_levelL < TANK_EMPTY_THRESHOLD_L && ch2LpmPureOut < TANK_FULL_STOP_THRESHOLD_LPM) {
        if (s_levelL > 0.0f) {
            Serial.println("[TANK] level near zero — resetting to 0");
            s_levelL = 0.0f;
            s_isFull = false;
        }
    }
}

float tankMonitor_getLevelL()   { return s_levelL; }

float tankMonitor_getLevelPct() {
    if (s_capacityL <= 0.0f) return 0.0f;
    float pct = (s_levelL / s_capacityL) * 100.0f;
    if (pct > 100.0f) pct = 100.0f;
    return pct;
}

bool  tankMonitor_isFull()      { return s_isFull; }

float tankMonitor_getCapacityL() { return s_capacityL; }

void  tankMonitor_setCapacityL(float l) {
    if (l < 5.0f || l > 30.0f) return;
    s_capacityL = l;
    saveToNvs();
    Serial.printf("[TANK] capacity set to %.2fL\n", l);
}

void tankMonitor_resetEmpty() {
    s_levelL = 0.0f;
    s_isFull = false;
    s_flowStopped = false;
    s_flowStopStartMs = 0;
    saveToNvs();
    storeSd_logEvent("[TANK] tank reset to empty — calibration fill starting");
    Serial.println("[TANK] reset to empty");
}
