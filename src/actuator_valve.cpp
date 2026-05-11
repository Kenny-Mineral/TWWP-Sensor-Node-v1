#include "actuator_valve.h"
#include "pins.h"
#include "config.h"
#include "sensor_flow.h"
#include <string.h>

static bool _isOpen   = false;
static bool _autoMode = true;

static char     _valveType[16]      = "test";
static char     _triggerSource[24]  = "flow";
static uint32_t _idleTimeoutS       = 0;
static uint32_t _maxOpenS           = 0;
static bool     _timeoutDisableAuto = false;
static bool     _timeoutAlert       = true;

void actuatorValve_setValveType(const char* type) {
    strncpy(_valveType, type, sizeof(_valveType) - 1);
    _valveType[sizeof(_valveType) - 1] = '\0';
}
void actuatorValve_setTriggerSource(const char* src) {
    strncpy(_triggerSource, src, sizeof(_triggerSource) - 1);
    _triggerSource[sizeof(_triggerSource) - 1] = '\0';
}
void actuatorValve_setIdleTimeoutS(uint32_t s)        { _idleTimeoutS = s; }
void actuatorValve_setMaxOpenS(uint32_t s)             { _maxOpenS = s; }
void actuatorValve_setTimeoutDisableAuto(bool v)       { _timeoutDisableAuto = v; }
void actuatorValve_setTimeoutAlert(bool v)             { _timeoutAlert = v; }

const char* actuatorValve_getValveType()               { return _valveType; }
const char* actuatorValve_getTriggerSource()           { return _triggerSource; }
uint32_t    actuatorValve_getIdleTimeoutS()            { return _idleTimeoutS; }
uint32_t    actuatorValve_getMaxOpenS()                { return _maxOpenS; }
bool        actuatorValve_getTimeoutDisableAuto()      { return _timeoutDisableAuto; }
bool        actuatorValve_getTimeoutAlert()            { return _timeoutAlert; }

bool actuatorValve_begin() {
    pinMode(PIN_VALVE, OUTPUT);
    digitalWrite(PIN_VALVE, HIGH); // relay off — NO wiring: HIGH=coil de-energised=LED off
    _isOpen   = false;
    _autoMode = true;
    strncpy(_valveType,     "test", sizeof(_valveType)     - 1);
    strncpy(_triggerSource, "flow", sizeof(_triggerSource) - 1);
    _idleTimeoutS       = 0;
    _maxOpenS           = 0;
    _timeoutDisableAuto = false;
    _timeoutAlert       = true;
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
