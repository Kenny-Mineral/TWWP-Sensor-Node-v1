#include "sensor_leak.h"
#include "pins.h"

static bool s_isWet = false;
static bool s_changed = false;

bool sensorLeak_begin() {
    pinMode(PIN_LEAK_DO, INPUT_PULLUP);
    s_isWet = (digitalRead(PIN_LEAK_DO) == LOW);
    return true;
}

void sensorLeak_loop() {
    bool wet = (digitalRead(PIN_LEAK_DO) == LOW);
    if (wet != s_isWet) {
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
