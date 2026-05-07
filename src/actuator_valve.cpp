#include "actuator_valve.h"
#include "pins.h"
#include "config.h"
#include "sensor_flow.h"

static bool _isOpen   = false;
static bool _autoMode = true;

bool actuatorValve_begin() {
    pinMode(PIN_VALVE, OUTPUT);
    digitalWrite(PIN_VALVE, HIGH); // relay off — NO wiring: HIGH=coil de-energised=LED off
    _isOpen   = false;
    _autoMode = true;
    return true;
}

void actuatorValve_open() {
    digitalWrite(PIN_VALVE, LOW);
    _isOpen = true;
}

void actuatorValve_close() {
    digitalWrite(PIN_VALVE, HIGH);
    _isOpen = false;
}

bool actuatorValve_isOpen() {
    return _isOpen;
}

void actuatorValve_setAuto(bool enable) {
    _autoMode = enable;
}

bool actuatorValve_isAuto() {
    return _autoMode;
}

void actuatorValve_loop() {
    if (!_autoMode) return;

    float rate = sensorFlow_getRateLpm(1);
    if (rate > FLOW_ACTIVE_THRESHOLD_LPM && !_isOpen) {
        actuatorValve_open();
    } else if (rate <= FLOW_ACTIVE_THRESHOLD_LPM && _isOpen) {
        actuatorValve_close();
    }
}
