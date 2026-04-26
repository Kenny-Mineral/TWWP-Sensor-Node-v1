#include "sensor_leak.h"
#include "pins.h"

static bool s_isWet = false;
static bool s_changed = false;
static bool s_rawWet = false;
static unsigned long s_rawChangedMs = 0;
static const unsigned long LEAK_DEBOUNCE_MS = 75;

bool sensorLeak_begin() {
    pinMode(PIN_LEAK_DO, INPUT_PULLUP);
    s_isWet = (digitalRead(PIN_LEAK_DO) == LOW);
    s_rawWet = s_isWet;
    s_rawChangedMs = millis();
    return true;
}

void sensorLeak_loop() {
    bool wet = (digitalRead(PIN_LEAK_DO) == LOW);
    unsigned long now = millis();

    if (wet != s_rawWet) {
        s_rawWet = wet;
        s_rawChangedMs = now;
        return;
    }

    if (wet != s_isWet && now - s_rawChangedMs >= LEAK_DEBOUNCE_MS) {
        s_isWet = wet;
        s_changed = true;
    }
}

bool sensorLeak_isWet() {
    return s_isWet;
}

bool sensorLeak_hasChanged() {
    bool changed = s_changed;
    s_changed = false;
    return changed;
}
