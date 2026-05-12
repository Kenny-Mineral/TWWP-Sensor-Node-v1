# Valve Configuration System (M3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add valve_type, trigger_source, idle_timeout_s, max_open_s, timeout_disable_auto, and timeout_alert config fields with safety timers, NVS persistence, MQTT cmd handling, and HA discovery entities.

**Architecture:** All config state lives in `actuator_valve.cpp` behind the new getter/setter API. `main.cpp` grows a new `publishHaDiscoveryValveConfig()` function, six new MQTT cmd keys, and six new heartbeat fields. Boot loading reads the `"valve"` block from `node.json` via `storeSd_readJsonFile`; MQTT writes go to NVS (Preferences namespace `"valve"`).

**Tech Stack:** Arduino/ESP-IDF, PlatformIO, ArduinoJson v7, Preferences (NVS), Unity (native tests)

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `src/actuator_valve.h` | Modify | Declare 12 new getter/setter functions |
| `src/actuator_valve.cpp` | Modify | Config state, trigger dispatch, two safety timers, safety close sequence |
| `src/main.cpp` | Modify | Boot load (valve block), 6 new cmd keys, `publishHaDiscoveryValveConfig()`, 6 heartbeat fields, call new discovery fn |
| `test/test_valve_config/test_valve_config.cpp` | Create | Unit tests for config state, trigger logic, both timers, safety close sequence |
| `docs/MQTT_TOPIC_MAP.md` | Modify | Add 6 new cmd keys |
| `docs/USER_OPERATIONS.md` | Modify | Add valve config section |

---

## Task 1: Test scaffold — stubs and empty test suite

**Files:**
- Create: `test/test_valve_config/test_valve_config.cpp`

The test file includes the driver under test (`actuator_valve.cpp`) directly, the same way `test_rs485_mux.cpp` does. It needs stubs for all external dependencies the driver calls: `sensor_flow.h`, `store_sd.h`, `net_mqtt.h`, and `Preferences.h`. `pins.h` and `config.h` are already header-only — no stubs needed for those.

- [ ] **Step 1: Create the test file with stubs and empty runner**

```cpp
// test/test_valve_config/test_valve_config.cpp
#include <unity.h>
#include <Arduino.h>   // test/stubs/Arduino.h — millis(), setMillis(), Serial

// ── Stubs ────────────────────────────────────────────────────────────────────

#include "sensor_flow.h"
static float g_flowRate = 0.0f;
float sensorFlow_getRateLpm(int) { return g_flowRate; }

#include "store_sd.h"
static char g_lastLogEvent[256] = "";
void storeSd_logEvent(const char* msg) {
    strncpy(g_lastLogEvent, msg, sizeof(g_lastLogEvent) - 1);
}

#include "net_mqtt.h"
static char g_lastAlertPayload[512] = "";
static char g_lastAlertTopic[128]   = "";
bool netMqtt_publishSub(const char* topic, const char* payload) {
    strncpy(g_lastAlertTopic,   topic,   sizeof(g_lastAlertTopic)   - 1);
    strncpy(g_lastAlertPayload, payload, sizeof(g_lastAlertPayload) - 1);
    return true;
}

// Preferences stub
#include <map>
#include <string>
static std::map<std::string, std::string> g_nvsStore;
static std::string g_nvsNamespace;
class Preferences {
public:
    void begin(const char* ns, bool) { g_nvsNamespace = ns; }
    void end() {}
    bool isKey(const char* k)       { return g_nvsStore.count(std::string(g_nvsNamespace) + "/" + k) > 0; }
    void putString(const char* k, const char* v) {
        g_nvsStore[std::string(g_nvsNamespace) + "/" + k] = v;
    }
    void putUInt(const char* k, uint32_t v) {
        g_nvsStore[std::string(g_nvsNamespace) + "/" + k] = std::to_string(v);
    }
    void putBool(const char* k, bool v) {
        g_nvsStore[std::string(g_nvsNamespace) + "/" + k] = v ? "1" : "0";
    }
    std::string getString(const char* k, const char* def) {
        auto it = g_nvsStore.find(std::string(g_nvsNamespace) + "/" + k);
        return it != g_nvsStore.end() ? it->second : def;
    }
    uint32_t getUInt(const char* k, uint32_t def) {
        auto it = g_nvsStore.find(std::string(g_nvsNamespace) + "/" + k);
        if (it == g_nvsStore.end()) return def;
        return (uint32_t)std::stoul(it->second);
    }
    bool getBool(const char* k, bool def) {
        auto it = g_nvsStore.find(std::string(g_nvsNamespace) + "/" + k);
        if (it == g_nvsStore.end()) return def;
        return it->second == "1";
    }
};

// GPIO stub
#include "pins.h"
static int g_pinState[48] = {};
void pinMode(int, int) {}
void digitalWrite(int pin, int val) { g_pinState[pin] = val; }
int  digitalRead(int pin)           { return g_pinState[pin]; }

// ── Driver under test ─────────────────────────────────────────────────────────
#include "../../src/actuator_valve.cpp"

// ── Helpers ───────────────────────────────────────────────────────────────────
static void resetAll() {
    setMillis(0);
    g_flowRate = 0.0f;
    g_lastLogEvent[0]   = '\0';
    g_lastAlertPayload[0] = '\0';
    g_lastAlertTopic[0]   = '\0';
    g_nvsStore.clear();
    actuatorValve_begin();
}

// ── Tests ─────────────────────────────────────────────────────────────────────

void test_placeholder() {
    TEST_ASSERT_TRUE(true);
}

void setUp()    { resetAll(); }
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_placeholder);
    return UNITY_END();
}
```

- [ ] **Step 2: Run the test to confirm it compiles and passes**

```bash
cd "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1"
pio test -e native -f test_valve_config
```

Expected: `1 Tests 0 Failures 0 Ignored — OK`

- [ ] **Step 3: Commit**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add test/test_valve_config/test_valve_config.cpp
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "test(valve): add test scaffold with stubs"
```

---

## Task 2: Config state + getter/setter API

**Files:**
- Modify: `src/actuator_valve.h`
- Modify: `src/actuator_valve.cpp`

Add the six config fields as static variables with defaults matching the spec. The setters just store the value; no side effects yet. All strings are stored as `char[]` arrays (not `String`) to avoid heap fragmentation.

- [ ] **Step 1: Write the failing tests**

Add these tests to `test_valve_config.cpp` (replace `test_placeholder`):

```cpp
void test_defaults_after_begin() {
    TEST_ASSERT_EQUAL_STRING("test",  actuatorValve_getValveType());
    TEST_ASSERT_EQUAL_STRING("flow",  actuatorValve_getTriggerSource());
    TEST_ASSERT_EQUAL_UINT32(0,       actuatorValve_getIdleTimeoutS());
    TEST_ASSERT_EQUAL_UINT32(0,       actuatorValve_getMaxOpenS());
    TEST_ASSERT_FALSE(actuatorValve_getTimeoutDisableAuto());
    TEST_ASSERT_TRUE(actuatorValve_getTimeoutAlert());
}

void test_setters_persist_values() {
    actuatorValve_setValveType("solenoid");
    actuatorValve_setTriggerSource("manual");
    actuatorValve_setIdleTimeoutS(300);
    actuatorValve_setMaxOpenS(600);
    actuatorValve_setTimeoutDisableAuto(true);
    actuatorValve_setTimeoutAlert(false);

    TEST_ASSERT_EQUAL_STRING("solenoid", actuatorValve_getValveType());
    TEST_ASSERT_EQUAL_STRING("manual",   actuatorValve_getTriggerSource());
    TEST_ASSERT_EQUAL_UINT32(300,        actuatorValve_getIdleTimeoutS());
    TEST_ASSERT_EQUAL_UINT32(600,        actuatorValve_getMaxOpenS());
    TEST_ASSERT_TRUE(actuatorValve_getTimeoutDisableAuto());
    TEST_ASSERT_FALSE(actuatorValve_getTimeoutAlert());
}

void test_unknown_trigger_source_accepted() {
    actuatorValve_setTriggerSource("qr");
    TEST_ASSERT_EQUAL_STRING("qr", actuatorValve_getTriggerSource());
}
```

Update `main()`:

```cpp
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_after_begin);
    RUN_TEST(test_setters_persist_values);
    RUN_TEST(test_unknown_trigger_source_accepted);
    return UNITY_END();
}
```

- [ ] **Step 2: Run — confirm FAIL**

```bash
pio test -e native -f test_valve_config
```

Expected: compile error — functions not declared.

- [ ] **Step 3: Declare new API in `src/actuator_valve.h`**

```cpp
#pragma once
#include <Arduino.h>

bool actuatorValve_begin();
void actuatorValve_loop();
void actuatorValve_open();
void actuatorValve_close();
bool actuatorValve_isOpen();
void actuatorValve_setAuto(bool enable);
bool actuatorValve_isAuto();

void        actuatorValve_setValveType(const char* type);
void        actuatorValve_setTriggerSource(const char* src);
void        actuatorValve_setIdleTimeoutS(uint32_t s);
void        actuatorValve_setMaxOpenS(uint32_t s);
void        actuatorValve_setTimeoutDisableAuto(bool v);
void        actuatorValve_setTimeoutAlert(bool v);
const char* actuatorValve_getValveType();
const char* actuatorValve_getTriggerSource();
uint32_t    actuatorValve_getIdleTimeoutS();
uint32_t    actuatorValve_getMaxOpenS();
bool        actuatorValve_getTimeoutDisableAuto();
bool        actuatorValve_getTimeoutAlert();
```

- [ ] **Step 4: Add config state to `src/actuator_valve.cpp`**

Add after the existing `#include` block and before `_isOpen`:

```cpp
#ifndef UNIT_TEST
#include <Preferences.h>
#include "store_sd.h"
#include "net_mqtt.h"
#endif

static bool _isOpen   = false;
static bool _autoMode = true;

static char     _valveType[16]     = "test";
static char     _triggerSource[24] = "flow";
static uint32_t _idleTimeoutS      = 0;
static uint32_t _maxOpenS          = 0;
static bool     _timeoutDisableAuto = false;
static bool     _timeoutAlert       = true;
```

Add the getter/setter implementations before `actuatorValve_begin()`:

```cpp
void actuatorValve_setValveType(const char* type) {
    strncpy(_valveType, type, sizeof(_valveType) - 1);
    _valveType[sizeof(_valveType) - 1] = '\0';
}
void actuatorValve_setTriggerSource(const char* src) {
    strncpy(_triggerSource, src, sizeof(_triggerSource) - 1);
    _triggerSource[sizeof(_triggerSource) - 1] = '\0';
}
void actuatorValve_setIdleTimeoutS(uint32_t s)       { _idleTimeoutS = s; }
void actuatorValve_setMaxOpenS(uint32_t s)            { _maxOpenS = s; }
void actuatorValve_setTimeoutDisableAuto(bool v)      { _timeoutDisableAuto = v; }
void actuatorValve_setTimeoutAlert(bool v)            { _timeoutAlert = v; }

const char* actuatorValve_getValveType()       { return _valveType; }
const char* actuatorValve_getTriggerSource()   { return _triggerSource; }
uint32_t    actuatorValve_getIdleTimeoutS()    { return _idleTimeoutS; }
uint32_t    actuatorValve_getMaxOpenS()        { return _maxOpenS; }
bool        actuatorValve_getTimeoutDisableAuto() { return _timeoutDisableAuto; }
bool        actuatorValve_getTimeoutAlert()    { return _timeoutAlert; }
```

Reset config fields inside `actuatorValve_begin()`:

```cpp
bool actuatorValve_begin() {
    pinMode(PIN_VALVE, OUTPUT);
    digitalWrite(PIN_VALVE, HIGH);
    _isOpen   = false;
    _autoMode = true;
    strncpy(_valveType,      "test", sizeof(_valveType)     - 1);
    strncpy(_triggerSource,  "flow", sizeof(_triggerSource) - 1);
    _idleTimeoutS       = 0;
    _maxOpenS           = 0;
    _timeoutDisableAuto = false;
    _timeoutAlert       = true;
    return true;
}
```

- [ ] **Step 5: Run — confirm PASS**

```bash
pio test -e native -f test_valve_config
```

Expected: `3 Tests 0 Failures 0 Ignored — OK`

- [ ] **Step 6: Commit**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add src/actuator_valve.h src/actuator_valve.cpp test/test_valve_config/test_valve_config.cpp
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "feat(valve): add config state and getter/setter API"
```

---

## Task 3: Trigger source dispatch in `actuatorValve_loop()`

**Files:**
- Modify: `src/actuator_valve.cpp`

The loop currently always runs auto-close logic. It should dispatch based on `_triggerSource`: `"flow"` = current behaviour, anything else = do nothing (manual/future). Unknown sources log a warning once at boot — not in the loop.

- [ ] **Step 1: Write the failing tests**

Add to `test_valve_config.cpp`:

```cpp
void test_flow_trigger_opens_on_flow() {
    actuatorValve_setTriggerSource("flow");
    actuatorValve_setAuto(true);
    g_flowRate = 0.1f;
    actuatorValve_loop();
    TEST_ASSERT_TRUE(actuatorValve_isOpen());
}

void test_flow_trigger_closes_on_no_flow() {
    actuatorValve_setTriggerSource("flow");
    actuatorValve_setAuto(true);
    g_flowRate = 0.1f;
    actuatorValve_loop();
    TEST_ASSERT_TRUE(actuatorValve_isOpen());
    g_flowRate = 0.0f;
    actuatorValve_loop();
    TEST_ASSERT_FALSE(actuatorValve_isOpen());
}

void test_manual_trigger_does_not_open_on_flow() {
    actuatorValve_setTriggerSource("manual");
    actuatorValve_setAuto(true);
    g_flowRate = 1.0f;
    actuatorValve_loop();
    TEST_ASSERT_FALSE(actuatorValve_isOpen());
}

void test_unknown_trigger_treated_as_manual() {
    actuatorValve_setTriggerSource("qr");
    actuatorValve_setAuto(true);
    g_flowRate = 1.0f;
    actuatorValve_loop();
    TEST_ASSERT_FALSE(actuatorValve_isOpen());
}
```

Add all four to `main()`.

- [ ] **Step 2: Run — confirm FAIL**

```bash
pio test -e native -f test_valve_config
```

Expected: `test_manual_trigger_does_not_open_on_flow` fails (valve opens when it shouldn't).

- [ ] **Step 3: Update `actuatorValve_loop()` in `src/actuator_valve.cpp`**

```cpp
void actuatorValve_loop() {
    if (!_autoMode) return;

    if (strcmp(_triggerSource, "flow") == 0) {
        float rate = sensorFlow_getRateLpm(1);
        if (rate > FLOW_ACTIVE_THRESHOLD_LPM && !_isOpen) {
            actuatorValve_open();
        } else if (rate <= FLOW_ACTIVE_THRESHOLD_LPM && _isOpen) {
            actuatorValve_close();
        }
    }
    // "manual" and future sources: loop does nothing — valve responds to direct API calls only
}
```

- [ ] **Step 4: Run — confirm PASS**

```bash
pio test -e native -f test_valve_config
```

Expected: `7 Tests 0 Failures 0 Ignored — OK`

- [ ] **Step 5: Commit**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add src/actuator_valve.cpp test/test_valve_config/test_valve_config.cpp
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "feat(valve): dispatch loop by trigger_source"
```

---

## Task 4: Safety timers — idle_timeout_s and max_open_s

**Files:**
- Modify: `src/actuator_valve.cpp`

Two independent timers tracked by static `unsigned long` timestamps. Both timers reset when the valve closes for any reason — hook this into `actuatorValve_close()`.

- [ ] **Step 1: Write the failing tests**

Add to `test_valve_config.cpp`:

```cpp
void test_idle_timeout_fires_at_configured_seconds() {
    actuatorValve_setIdleTimeoutS(10);
    actuatorValve_setTriggerSource("manual");
    setMillis(0);
    actuatorValve_open();
    TEST_ASSERT_TRUE(actuatorValve_isOpen());

    g_flowRate = 0.0f;         // no flow — idle timer counts
    setMillis(9999);
    actuatorValve_loop();
    TEST_ASSERT_TRUE(actuatorValve_isOpen());   // not yet

    setMillis(10001);
    actuatorValve_loop();
    TEST_ASSERT_FALSE(actuatorValve_isOpen());  // closed by idle timer
}

void test_idle_timeout_resets_while_flow_present() {
    actuatorValve_setIdleTimeoutS(10);
    actuatorValve_setTriggerSource("manual");
    setMillis(0);
    actuatorValve_open();

    g_flowRate = 0.1f;         // flow present — timer stays reset
    setMillis(15000);
    actuatorValve_loop();
    TEST_ASSERT_TRUE(actuatorValve_isOpen());   // still open
}

void test_max_open_fires_at_configured_seconds() {
    actuatorValve_setMaxOpenS(10);
    actuatorValve_setTriggerSource("manual");
    setMillis(0);
    actuatorValve_open();

    g_flowRate = 0.5f;         // flow present — max_open doesn't care
    setMillis(9999);
    actuatorValve_loop();
    TEST_ASSERT_TRUE(actuatorValve_isOpen());

    setMillis(10001);
    actuatorValve_loop();
    TEST_ASSERT_FALSE(actuatorValve_isOpen());  // closed by max_open timer
}

void test_max_open_resets_on_valve_close() {
    actuatorValve_setMaxOpenS(10);
    actuatorValve_setTriggerSource("manual");
    setMillis(0);
    actuatorValve_open();
    actuatorValve_close();     // close before timer fires
    actuatorValve_open();      // reopen — timer restarts from now
    setMillis(9999);
    actuatorValve_loop();
    TEST_ASSERT_TRUE(actuatorValve_isOpen());   // 9s elapsed since reopen — still open
}

void test_idle_timeout_zero_means_disabled() {
    actuatorValve_setIdleTimeoutS(0);
    actuatorValve_setTriggerSource("manual");
    setMillis(0);
    actuatorValve_open();
    g_flowRate = 0.0f;
    setMillis(99999);
    actuatorValve_loop();
    TEST_ASSERT_TRUE(actuatorValve_isOpen());   // never closes — disabled
}

void test_max_open_zero_means_disabled() {
    actuatorValve_setMaxOpenS(0);
    actuatorValve_setTriggerSource("manual");
    setMillis(0);
    actuatorValve_open();
    setMillis(99999);
    actuatorValve_loop();
    TEST_ASSERT_TRUE(actuatorValve_isOpen());
}
```

Add all six to `main()`.

- [ ] **Step 2: Run — confirm FAIL**

```bash
pio test -e native -f test_valve_config
```

Expected: timer tests fail (valve stays open).

- [ ] **Step 3: Add timer state and logic to `src/actuator_valve.cpp`**

Add two new static variables after the config state block:

```cpp
static unsigned long _valveOpenedAtMs = 0;
static unsigned long _lastFlowSeenMs  = 0;
```

Update `actuatorValve_open()` to record open timestamp:

```cpp
void actuatorValve_open() {
    if (!_isOpen) {
        _valveOpenedAtMs = millis();
        _lastFlowSeenMs  = millis();
    }
    digitalWrite(PIN_VALVE, LOW);
    _isOpen = true;
}
```

Update `actuatorValve_close()` to reset timers:

```cpp
void actuatorValve_close() {
    digitalWrite(PIN_VALVE, HIGH);
    _isOpen          = false;
    _valveOpenedAtMs = 0;
    _lastFlowSeenMs  = 0;
}
```

Add `runSafetyTimers()` helper and call it from `actuatorValve_loop()`:

```cpp
static void runSafetyTimers() {
    if (!_isOpen) return;

    unsigned long now = millis();

    // Update last-flow timestamp while flow is present
    if (sensorFlow_getRateLpm(1) > FLOW_ACTIVE_THRESHOLD_LPM) {
        _lastFlowSeenMs = now;
    }

    // Idle timer: fires when valve is open and flow has been absent >= idle_timeout_s
    if (_idleTimeoutS > 0) {
        unsigned long idleElapsed = now - _lastFlowSeenMs;
        if (idleElapsed >= (unsigned long)_idleTimeoutS * 1000UL) {
            actuatorValve_close();
            if (_timeoutDisableAuto) actuatorValve_setAuto(false);
            if (_timeoutAlert) {
                char msg[128];
                snprintf(msg, sizeof(msg),
                    "{\"type\":\"VALVE_SAFETY_CLOSE\",\"reason\":\"idle_timeout\",\"timeout_s\":%lu}",
                    (unsigned long)_idleTimeoutS);
                netMqtt_publishSub(TOPIC_ALERT, msg);
            }
            char log[128];
            snprintf(log, sizeof(log),
                "[VALVE] safety close: reason=idle_timeout, timeout_s=%lu",
                (unsigned long)_idleTimeoutS);
            storeSd_logEvent(log);
            return;
        }
    }

    // Max-open timer: fires N seconds after valve opened, regardless of flow
    if (_maxOpenS > 0) {
        unsigned long openElapsed = now - _valveOpenedAtMs;
        if (openElapsed >= (unsigned long)_maxOpenS * 1000UL) {
            actuatorValve_close();
            if (_timeoutDisableAuto) actuatorValve_setAuto(false);
            if (_timeoutAlert) {
                char msg[128];
                snprintf(msg, sizeof(msg),
                    "{\"type\":\"VALVE_SAFETY_CLOSE\",\"reason\":\"max_open\",\"timeout_s\":%lu}",
                    (unsigned long)_maxOpenS);
                netMqtt_publishSub(TOPIC_ALERT, msg);
            }
            char log[128];
            snprintf(log, sizeof(log),
                "[VALVE] safety close: reason=max_open, timeout_s=%lu",
                (unsigned long)_maxOpenS);
            storeSd_logEvent(log);
        }
    }
}
```

Update `actuatorValve_loop()` to call `runSafetyTimers()` at the end:

```cpp
void actuatorValve_loop() {
    if (!_autoMode) return;   // NOTE: safety timers still run even in manual mode

    if (strcmp(_triggerSource, "flow") == 0) {
        float rate = sensorFlow_getRateLpm(1);
        if (rate > FLOW_ACTIVE_THRESHOLD_LPM && !_isOpen) {
            actuatorValve_open();
        } else if (rate <= FLOW_ACTIVE_THRESHOLD_LPM && _isOpen) {
            actuatorValve_close();
        }
    }

    runSafetyTimers();
}
```

**Important:** Safety timers run regardless of trigger source. The guard `if (!_autoMode) return` is **wrong** for safety — remove it from the top of `actuatorValve_loop()` and instead guard only the trigger dispatch:

```cpp
void actuatorValve_loop() {
    if (_autoMode && strcmp(_triggerSource, "flow") == 0) {
        float rate = sensorFlow_getRateLpm(1);
        if (rate > FLOW_ACTIVE_THRESHOLD_LPM && !_isOpen) {
            actuatorValve_open();
        } else if (rate <= FLOW_ACTIVE_THRESHOLD_LPM && _isOpen) {
            actuatorValve_close();
        }
    }

    runSafetyTimers();
}
```

- [ ] **Step 4: Run — confirm PASS**

```bash
pio test -e native -f test_valve_config
```

Expected: `13 Tests 0 Failures 0 Ignored — OK`

- [ ] **Step 5: Commit**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add src/actuator_valve.cpp test/test_valve_config/test_valve_config.cpp
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "feat(valve): safety timers idle_timeout_s and max_open_s"
```

---

## Task 5: Safety close sequence — timeout_disable_auto and timeout_alert

**Files:**
- Modify: `test/test_valve_config/test_valve_config.cpp`

The safety close sequence is already implemented in `runSafetyTimers()`. These tests verify the `_timeoutDisableAuto` and `_timeoutAlert` branches and confirm the SD log is always written.

- [ ] **Step 1: Write the tests**

Add to `test_valve_config.cpp`:

```cpp
void test_safety_close_logs_to_sd() {
    actuatorValve_setIdleTimeoutS(5);
    actuatorValve_setTriggerSource("manual");
    setMillis(0);
    actuatorValve_open();
    g_flowRate = 0.0f;
    setMillis(6000);
    actuatorValve_loop();
    TEST_ASSERT_NOT_NULL(strstr(g_lastLogEvent, "[VALVE] safety close"));
    TEST_ASSERT_NOT_NULL(strstr(g_lastLogEvent, "idle_timeout"));
}

void test_safety_close_publishes_alert_when_enabled() {
    actuatorValve_setIdleTimeoutS(5);
    actuatorValve_setTimeoutAlert(true);
    actuatorValve_setTriggerSource("manual");
    setMillis(0);
    actuatorValve_open();
    g_flowRate = 0.0f;
    setMillis(6000);
    actuatorValve_loop();
    TEST_ASSERT_NOT_NULL(strstr(g_lastAlertPayload, "VALVE_SAFETY_CLOSE"));
    TEST_ASSERT_NOT_NULL(strstr(g_lastAlertPayload, "idle_timeout"));
}

void test_safety_close_no_alert_when_disabled() {
    actuatorValve_setIdleTimeoutS(5);
    actuatorValve_setTimeoutAlert(false);
    actuatorValve_setTriggerSource("manual");
    setMillis(0);
    actuatorValve_open();
    g_flowRate = 0.0f;
    setMillis(6000);
    actuatorValve_loop();
    TEST_ASSERT_EQUAL(0, g_lastAlertPayload[0]);  // no publish
}

void test_safety_close_disables_auto_when_flag_set() {
    actuatorValve_setIdleTimeoutS(5);
    actuatorValve_setTimeoutDisableAuto(true);
    actuatorValve_setAuto(true);
    actuatorValve_setTriggerSource("manual");
    setMillis(0);
    actuatorValve_open();
    g_flowRate = 0.0f;
    setMillis(6000);
    actuatorValve_loop();
    TEST_ASSERT_FALSE(actuatorValve_isAuto());
}

void test_safety_close_leaves_auto_on_when_flag_not_set() {
    actuatorValve_setIdleTimeoutS(5);
    actuatorValve_setTimeoutDisableAuto(false);
    actuatorValve_setAuto(true);
    actuatorValve_setTriggerSource("manual");
    setMillis(0);
    actuatorValve_open();
    g_flowRate = 0.0f;
    setMillis(6000);
    actuatorValve_loop();
    TEST_ASSERT_TRUE(actuatorValve_isAuto());
}
```

Add all five to `main()`.

- [ ] **Step 2: Run — confirm PASS** (logic already implemented in Task 4)

```bash
pio test -e native -f test_valve_config
```

Expected: `18 Tests 0 Failures 0 Ignored — OK`

- [ ] **Step 3: Commit**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add test/test_valve_config/test_valve_config.cpp
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "test(valve): safety close sequence tests"
```

---

## Task 6: Boot loading from node.json + NVS overlay

**Files:**
- Modify: `src/actuator_valve.cpp`

Add `actuatorValve_loadConfig()` — reads the `"valve"` block from `node.json` first, then overlays any NVS values on top. Called from `main.cpp` during boot, after `actuatorValve_begin()` and after the SD card is ready.

- [ ] **Step 1: Write the failing tests**

Add to `test_valve_config.cpp`. The test needs a fake `storeSd_readJsonFile` that can load canned JSON:

```cpp
// Add after the existing storeSd stub:
#include <ArduinoJson.h>
static JsonDocument g_fakeNodeJson;
static bool         g_sdJsonAvail = false;
bool storeSd_readJsonFile(const char* path, JsonDocument& doc) {
    if (!g_sdJsonAvail) return false;
    doc = g_fakeNodeJson;
    return true;
}
```

Now the tests:

```cpp
void test_load_config_reads_valve_block_from_sd() {
    g_sdJsonAvail = true;
    g_fakeNodeJson["valve"]["valve_type"]     = "solenoid";
    g_fakeNodeJson["valve"]["trigger_source"] = "manual";
    g_fakeNodeJson["valve"]["idle_timeout_s"] = 120;
    g_fakeNodeJson["valve"]["max_open_s"]     = 300;
    g_fakeNodeJson["valve"]["timeout_disable_auto"] = true;
    g_fakeNodeJson["valve"]["timeout_alert"]  = false;

    actuatorValve_loadConfig();

    TEST_ASSERT_EQUAL_STRING("solenoid", actuatorValve_getValveType());
    TEST_ASSERT_EQUAL_STRING("manual",   actuatorValve_getTriggerSource());
    TEST_ASSERT_EQUAL_UINT32(120,        actuatorValve_getIdleTimeoutS());
    TEST_ASSERT_EQUAL_UINT32(300,        actuatorValve_getMaxOpenS());
    TEST_ASSERT_TRUE(actuatorValve_getTimeoutDisableAuto());
    TEST_ASSERT_FALSE(actuatorValve_getTimeoutAlert());
}

void test_load_config_nvs_overlays_sd() {
    g_sdJsonAvail = true;
    g_fakeNodeJson["valve"]["valve_type"]     = "solenoid";
    g_fakeNodeJson["valve"]["trigger_source"] = "flow";

    // NVS overrides valve_type
    g_nvsStore["valve/valve_type"] = "ball_valve";

    actuatorValve_loadConfig();

    TEST_ASSERT_EQUAL_STRING("ball_valve", actuatorValve_getValveType());
    TEST_ASSERT_EQUAL_STRING("flow",       actuatorValve_getTriggerSource());
}

void test_load_config_sd_absent_uses_defaults() {
    g_sdJsonAvail = false;
    actuatorValve_loadConfig();
    TEST_ASSERT_EQUAL_STRING("test", actuatorValve_getValveType());
    TEST_ASSERT_EQUAL_STRING("flow", actuatorValve_getTriggerSource());
    TEST_ASSERT_EQUAL_UINT32(0,      actuatorValve_getIdleTimeoutS());
}
```

Update `resetAll()` to clear SD state:

```cpp
static void resetAll() {
    setMillis(0);
    g_flowRate = 0.0f;
    g_lastLogEvent[0]     = '\0';
    g_lastAlertPayload[0] = '\0';
    g_lastAlertTopic[0]   = '\0';
    g_nvsStore.clear();
    g_sdJsonAvail = false;
    g_fakeNodeJson.clear();
    actuatorValve_begin();
}
```

Declare `actuatorValve_loadConfig()` in `actuator_valve.h`:

```cpp
void actuatorValve_loadConfig();
```

Add three new tests to `main()`.

- [ ] **Step 2: Run — confirm FAIL**

```bash
pio test -e native -f test_valve_config
```

Expected: link error — `actuatorValve_loadConfig` not defined.

- [ ] **Step 3: Implement `actuatorValve_loadConfig()` in `src/actuator_valve.cpp`**

Add before `actuatorValve_begin()`:

```cpp
void actuatorValve_loadConfig() {
    // Layer 1: node.json defaults
    JsonDocument doc;
    if (storeSd_readJsonFile(SD_CONFIG_PATH, doc)) {
        JsonObjectConst v = doc["valve"].as<JsonObjectConst>();
        if (!v["valve_type"].isNull())
            actuatorValve_setValveType(v["valve_type"].as<const char*>());
        if (!v["trigger_source"].isNull())
            actuatorValve_setTriggerSource(v["trigger_source"].as<const char*>());
        if (!v["idle_timeout_s"].isNull())
            actuatorValve_setIdleTimeoutS(v["idle_timeout_s"].as<uint32_t>());
        if (!v["max_open_s"].isNull())
            actuatorValve_setMaxOpenS(v["max_open_s"].as<uint32_t>());
        if (!v["timeout_disable_auto"].isNull())
            actuatorValve_setTimeoutDisableAuto(v["timeout_disable_auto"].as<bool>());
        if (!v["timeout_alert"].isNull())
            actuatorValve_setTimeoutAlert(v["timeout_alert"].as<bool>());
    }

    // Layer 2: NVS overlay (MQTT writes — survive reboots without touching SD)
    Preferences prefs;
    prefs.begin("valve", true);
    if (prefs.isKey("valve_type"))
        actuatorValve_setValveType(prefs.getString("valve_type", _valveType).c_str());
    if (prefs.isKey("trigger_source"))
        actuatorValve_setTriggerSource(prefs.getString("trigger_source", _triggerSource).c_str());
    if (prefs.isKey("idle_timeout_s"))
        actuatorValve_setIdleTimeoutS(prefs.getUInt("idle_timeout_s", _idleTimeoutS));
    if (prefs.isKey("max_open_s"))
        actuatorValve_setMaxOpenS(prefs.getUInt("max_open_s", _maxOpenS));
    if (prefs.isKey("timeout_disable_auto"))
        actuatorValve_setTimeoutDisableAuto(prefs.getBool("timeout_disable_auto", _timeoutDisableAuto));
    if (prefs.isKey("timeout_alert"))
        actuatorValve_setTimeoutAlert(prefs.getBool("timeout_alert", _timeoutAlert));
    prefs.end();

    Serial.printf("[VALVE] config loaded: type=%s trigger=%s idle=%lus max=%lus\n",
        _valveType, _triggerSource,
        (unsigned long)_idleTimeoutS, (unsigned long)_maxOpenS);
}
```

- [ ] **Step 4: Run — confirm PASS**

```bash
pio test -e native -f test_valve_config
```

Expected: `21 Tests 0 Failures 0 Ignored — OK`

- [ ] **Step 5: Wire `actuatorValve_loadConfig()` into `main.cpp` boot sequence**

In `main.cpp`, find `actuatorValve_begin()` (in `setup()`) and add the load call immediately after:

```cpp
actuatorValve_begin();
actuatorValve_loadConfig();
```

- [ ] **Step 6: Confirm firmware builds**

```bash
cd "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1"
pio run -e waveshare-esp32-s3-rs485-can
```

Expected: Build succeeded.

- [ ] **Step 7: Commit**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add src/actuator_valve.h src/actuator_valve.cpp src/main.cpp test/test_valve_config/test_valve_config.cpp
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "feat(valve): boot loading from node.json + NVS overlay"
```

---

## Task 7: NVS persistence for MQTT writes

**Files:**
- Modify: `src/actuator_valve.cpp`

Add `actuatorValve_saveToNvs()` — called from `main.cpp` cmd handler after applying a setting. Writes only the one key that changed, so it's called individually per setter.

For simplicity, add one helper per field group rather than a single monolithic save:

- [ ] **Step 1: Write the failing test**

Add to `test_valve_config.cpp`:

```cpp
void test_save_nvs_persists_valve_type() {
    actuatorValve_setValveType("solenoid");
    actuatorValve_saveToNvs();

    // Simulate reboot: reset state, reload from NVS (SD absent)
    g_sdJsonAvail = false;
    actuatorValve_begin();
    actuatorValve_loadConfig();

    TEST_ASSERT_EQUAL_STRING("solenoid", actuatorValve_getValveType());
}

void test_save_nvs_persists_all_fields() {
    actuatorValve_setValveType("solenoid");
    actuatorValve_setTriggerSource("manual");
    actuatorValve_setIdleTimeoutS(180);
    actuatorValve_setMaxOpenS(900);
    actuatorValve_setTimeoutDisableAuto(true);
    actuatorValve_setTimeoutAlert(false);
    actuatorValve_saveToNvs();

    g_sdJsonAvail = false;
    actuatorValve_begin();
    actuatorValve_loadConfig();

    TEST_ASSERT_EQUAL_STRING("solenoid", actuatorValve_getValveType());
    TEST_ASSERT_EQUAL_STRING("manual",   actuatorValve_getTriggerSource());
    TEST_ASSERT_EQUAL_UINT32(180,        actuatorValve_getIdleTimeoutS());
    TEST_ASSERT_EQUAL_UINT32(900,        actuatorValve_getMaxOpenS());
    TEST_ASSERT_TRUE(actuatorValve_getTimeoutDisableAuto());
    TEST_ASSERT_FALSE(actuatorValve_getTimeoutAlert());
}
```

Declare in `actuator_valve.h`:

```cpp
void actuatorValve_saveToNvs();
```

Add two tests to `main()`.

- [ ] **Step 2: Run — confirm FAIL**

```bash
pio test -e native -f test_valve_config
```

- [ ] **Step 3: Implement `actuatorValve_saveToNvs()` in `src/actuator_valve.cpp`**

```cpp
void actuatorValve_saveToNvs() {
    Preferences prefs;
    prefs.begin("valve", false);
    prefs.putString("valve_type",           _valveType);
    prefs.putString("trigger_source",       _triggerSource);
    prefs.putUInt("idle_timeout_s",         _idleTimeoutS);
    prefs.putUInt("max_open_s",             _maxOpenS);
    prefs.putBool("timeout_disable_auto",   _timeoutDisableAuto);
    prefs.putBool("timeout_alert",          _timeoutAlert);
    prefs.end();
}
```

- [ ] **Step 4: Run — confirm PASS**

```bash
pio test -e native -f test_valve_config
```

Expected: `23 Tests 0 Failures 0 Ignored — OK`

- [ ] **Step 5: Commit**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add src/actuator_valve.h src/actuator_valve.cpp test/test_valve_config/test_valve_config.cpp
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "feat(valve): NVS persistence for config writes"
```

---

## Task 8: MQTT cmd handler — 6 new keys in main.cpp

**Files:**
- Modify: `src/main.cpp`

Add six new cmd keys to the existing `handleCmd()` function, just after the existing `valve_auto` block (around line 1567). Each key: apply setter, call `saveToNvs()`.

- [ ] **Step 1: Locate insertion point**

In `src/main.cpp`, find:

```cpp
    if (!doc["valve_auto"].isNull()) {
        actuatorValve_setAuto(doc["valve_auto"].as<bool>());
    }
}
```

- [ ] **Step 2: Add the 6 new cmd handlers**

Insert immediately before the closing `}` of the `handleCmd` function:

```cpp
    if (!doc["set_valve_type"].isNull()) {
        actuatorValve_setValveType(doc["set_valve_type"].as<const char*>());
        actuatorValve_saveToNvs();
    }
    if (!doc["set_trigger_source"].isNull()) {
        actuatorValve_setTriggerSource(doc["set_trigger_source"].as<const char*>());
        actuatorValve_saveToNvs();
    }
    if (!doc["set_valve_idle_timeout"].isNull()) {
        actuatorValve_setIdleTimeoutS(doc["set_valve_idle_timeout"].as<uint32_t>());
        actuatorValve_saveToNvs();
    }
    if (!doc["set_valve_max_open"].isNull()) {
        actuatorValve_setMaxOpenS(doc["set_valve_max_open"].as<uint32_t>());
        actuatorValve_saveToNvs();
    }
    if (!doc["set_valve_timeout_disable_auto"].isNull()) {
        actuatorValve_setTimeoutDisableAuto(doc["set_valve_timeout_disable_auto"].as<bool>());
        actuatorValve_saveToNvs();
    }
    if (!doc["set_valve_timeout_alert"].isNull()) {
        actuatorValve_setTimeoutAlert(doc["set_valve_timeout_alert"].as<bool>());
        actuatorValve_saveToNvs();
    }
```

- [ ] **Step 3: Build to confirm no errors**

```bash
cd "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1"
pio run -e waveshare-esp32-s3-rs485-can
```

Expected: Build succeeded.

- [ ] **Step 4: Commit**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add src/main.cpp
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "feat(valve): 6 new MQTT cmd keys for valve config"
```

---

## Task 9: Heartbeat fields — 6 new fields in M0 status

**Files:**
- Modify: `src/main.cpp`

Add six new fields to `publishM0Status()` immediately after the existing `valve_open` / `valve_auto` lines (~line 222).

- [ ] **Step 1: Find the insertion point**

In `src/main.cpp`, find:

```cpp
    doc["valve_open"] = actuatorValve_isOpen();
    doc["valve_auto"] = actuatorValve_isAuto();
```

- [ ] **Step 2: Add 6 new heartbeat fields**

Insert immediately after those two lines:

```cpp
    doc["valve_type"]                 = actuatorValve_getValveType();
    doc["trigger_source"]             = actuatorValve_getTriggerSource();
    doc["valve_idle_timeout_s"]       = actuatorValve_getIdleTimeoutS();
    doc["valve_max_open_s"]           = actuatorValve_getMaxOpenS();
    doc["valve_timeout_disable_auto"] = actuatorValve_getTimeoutDisableAuto();
    doc["valve_timeout_alert"]        = actuatorValve_getTimeoutAlert();
```

- [ ] **Step 3: Build to confirm no errors**

```bash
pio run -e waveshare-esp32-s3-rs485-can
```

Expected: Build succeeded.

- [ ] **Step 4: Commit**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add src/main.cpp
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "feat(valve): 6 new heartbeat fields for valve config"
```

---

## Task 10: HA discovery — `publishHaDiscoveryValveConfig()`

**Files:**
- Modify: `src/main.cpp`

Add a new static function `publishHaDiscoveryValveConfig()` that publishes 6 HA entities:
- 2 × `select` (valve_type, trigger_source)
- 2 × `number` (idle_timeout_s, max_open_s)  
- 2 × `switch` (timeout_disable_auto, timeout_alert)

The `select` entity type is new to this codebase. HA MQTT select discovery uses `homeassistant/select/<uid>/config` with `options` array, `command_topic`, `command_template`, `state_topic`, and `value_template`.

- [ ] **Step 1: Add the new discovery function**

Insert `publishHaDiscoveryValveConfig()` immediately after `publishHaDiscoveryValve()` (after line ~1214):

```cpp
static bool publishHaDiscoveryValveConfig() {
    bool ok = true;

    // Helper: publish one select entity
    auto pubSelect = [&](const char* uid, const char* name, const char* valueKey,
                         const char* cmdKey,
                         std::initializer_list<const char*> options) {
        JsonDocument doc;
        doc["name"]             = name;
        doc["unique_id"]        = uid;
        doc["object_id"]        = uid;
        doc["entity_category"]  = "config";
        doc["state_topic"]      = TOPIC_STATUS;
        char tmpl[80];
        snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", valueKey);
        doc["value_template"]   = tmpl;
        doc["command_topic"]    = TOPIC_CMD;
        char cmdTmpl[80];
        snprintf(cmdTmpl, sizeof(cmdTmpl), "{\"%s\": \"{{ value }}\"}", cmdKey);
        doc["command_template"] = cmdTmpl;
        JsonArray opts = doc["options"].to<JsonArray>();
        for (const char* o : options) opts.add(o);
        doc["availability_topic"]    = TOPIC_LWT;
        doc["payload_available"]     = "online";
        doc["payload_not_available"] = "offline";
        fillHaDevice(doc);
        char payload[768];
        if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; return; }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/select/%s/config", uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    };

    pubSelect("twwp_" NODE_ID "_valve_type", "Valve Type",
              "valve_type", "set_valve_type",
              {"test", "solenoid", "ball_valve"});

    pubSelect("twwp_" NODE_ID "_trigger_source", "Valve Trigger Source",
              "trigger_source", "set_trigger_source",
              {"flow", "manual"});

    // Helper: publish one number entity (integer seconds)
    auto pubTimeoutNumber = [&](const char* uid, const char* name,
                                const char* valueKey, const char* cmdKey) {
        JsonDocument doc;
        doc["name"]             = name;
        doc["unique_id"]        = uid;
        doc["object_id"]        = uid;
        doc["entity_category"]  = "config";
        doc["state_topic"]      = TOPIC_STATUS;
        char tmpl[80];
        snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", valueKey);
        doc["value_template"]   = tmpl;
        doc["command_topic"]    = TOPIC_CMD;
        char cmdTmpl[80];
        snprintf(cmdTmpl, sizeof(cmdTmpl), "{\"%s\": {{ value | int }}}", cmdKey);
        doc["command_template"] = cmdTmpl;
        doc["unit_of_measurement"] = "s";
        doc["min"]              = 0;
        doc["max"]              = 3600;
        doc["step"]             = 1;
        doc["mode"]             = "box";
        doc["icon"]             = "mdi:timer-outline";
        doc["availability_topic"]    = TOPIC_LWT;
        doc["payload_available"]     = "online";
        doc["payload_not_available"] = "offline";
        fillHaDevice(doc);
        char payload[768];
        if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; return; }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/number/%s/config", uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    };

    pubTimeoutNumber("twwp_" NODE_ID "_valve_idle_timeout", "Valve Idle Timeout",
                     "valve_idle_timeout_s", "set_valve_idle_timeout");
    pubTimeoutNumber("twwp_" NODE_ID "_valve_max_open", "Valve Max Open Time",
                     "valve_max_open_s", "set_valve_max_open");

    // Helper: publish one switch entity (bool flag)
    auto pubFlagSwitch = [&](const char* uid, const char* name,
                             const char* valueKey, const char* cmdKeyTrue, const char* cmdKeyFalse) {
        JsonDocument doc;
        doc["name"]             = name;
        doc["unique_id"]        = uid;
        doc["object_id"]        = uid;
        doc["entity_category"]  = "config";
        doc["state_topic"]      = TOPIC_STATUS;
        char tmpl[96];
        snprintf(tmpl, sizeof(tmpl), "{{ 'ON' if value_json.%s else 'OFF' }}", valueKey);
        doc["value_template"]   = tmpl;
        doc["command_topic"]    = TOPIC_CMD;
        doc["payload_on"]       = cmdKeyTrue;
        doc["payload_off"]      = cmdKeyFalse;
        doc["availability_topic"]    = TOPIC_LWT;
        doc["payload_available"]     = "online";
        doc["payload_not_available"] = "offline";
        fillHaDevice(doc);
        char payload[768];
        if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; return; }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/switch/%s/config", uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    };

    pubFlagSwitch("twwp_" NODE_ID "_valve_timeout_disable_auto",
                  "Timeout Disables Auto",
                  "valve_timeout_disable_auto",
                  "{\"set_valve_timeout_disable_auto\": true}",
                  "{\"set_valve_timeout_disable_auto\": false}");

    pubFlagSwitch("twwp_" NODE_ID "_valve_timeout_alert",
                  "Timeout Publishes Alert",
                  "valve_timeout_alert",
                  "{\"set_valve_timeout_alert\": true}",
                  "{\"set_valve_timeout_alert\": false}");

    Serial.print("[MQTT] HA valve config discovery ");
    Serial.println(ok ? "published" : "partial");
    return ok;
}
```

- [ ] **Step 2: Register the new function in `publishOnlineState()`**

Find:

```cpp
    publishHaDiscoveryValve();
```

Add immediately after:

```cpp
    publishHaDiscoveryValveConfig();
```

- [ ] **Step 3: Build to confirm no errors**

```bash
pio run -e waveshare-esp32-s3-rs485-can
```

Expected: Build succeeded.

- [ ] **Step 4: Commit**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add src/main.cpp
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "feat(valve): HA discovery for valve config (select/number/switch)"
```

---

## Task 11: Docs update — MQTT_TOPIC_MAP.md and USER_OPERATIONS.md

**Files:**
- Modify: `docs/MQTT_TOPIC_MAP.md`
- Modify: `docs/USER_OPERATIONS.md`

- [ ] **Step 1: Add 6 new cmd keys to MQTT_TOPIC_MAP.md**

Find the `twwp/<id>/cmd` section. Add a new "Valve Config" block:

```markdown
### Valve Config

| Key | Type | Effect |
|---|---|---|
| `set_valve_type` | string | Set valve hardware type (`test`/`solenoid`/`ball_valve`). Persisted to NVS. |
| `set_trigger_source` | string | Set trigger source (`flow`/`manual`). Persisted to NVS. |
| `set_valve_idle_timeout` | int (0–3600) | Safety close N seconds after last flow while valve is open. 0 = disabled. Persisted. |
| `set_valve_max_open` | int (0–3600) | Safety close N seconds after valve opened. 0 = disabled. Persisted. |
| `set_valve_timeout_disable_auto` | bool | If true, disable auto mode when safety close fires. Persisted. |
| `set_valve_timeout_alert` | bool | If true, publish to `twwp/<id>/alert` when safety close fires. Persisted. |
```

- [ ] **Step 2: Add valve config section to USER_OPERATIONS.md**

Add a new "Valve Configuration" section under the existing Valve section:

```markdown
## Valve Configuration

Valve behaviour is controlled by six config fields. All are readable in the heartbeat (`twwp/<id>/status`) and writable via MQTT cmd or HA entity.

### Valve type

Controls hardware wiring model. Current wiring is `test` (relay driving an LED indicator).

```bash
mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"set_valve_type": "solenoid"}'
```

| Value | Meaning |
|---|---|
| `test` | Relay + LED (current bench setup) |
| `solenoid` | Production solenoid — sustained energise to hold open |
| `ball_valve` | Not yet implemented — logs warning, falls back to solenoid |

### Trigger source

Controls what opens/closes the valve automatically.

```bash
mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"set_trigger_source": "manual"}'
```

| Value | Meaning |
|---|---|
| `flow` | Open when flow rate > 0.05 L/min (default — current test behaviour) |
| `manual` | Loop does nothing; valve responds to `valve_open` cmd only |

### Safety timers

Two independent timers. Both default to 0 (disabled).

**Idle timeout** — safety-closes the valve if it has been open with no flow for N seconds:

```bash
mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"set_valve_idle_timeout": 300}'
```

**Max-open timeout** — safety-closes the valve N seconds after it was opened, regardless of flow:

```bash
mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"set_valve_max_open": 600}'
```

Set either to 0 to disable.

### Safety close behaviour flags

When a safety timer fires:

```bash
# Disable auto mode after safety close (prevents re-open until manually re-enabled)
mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"set_valve_timeout_disable_auto": true}'

# Publish alert to twwp/wh_001/alert when safety close fires
mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"set_valve_timeout_alert": true}'
```

The SD log always records a safety close regardless of these flags.
```

- [ ] **Step 3: Build and run tests one final time**

```bash
cd "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1"
pio test -e native -f test_valve_config
pio run -e waveshare-esp32-s3-rs485-can
```

Expected: 23 tests pass, firmware build succeeds.

- [ ] **Step 4: Commit**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add docs/MQTT_TOPIC_MAP.md docs/USER_OPERATIONS.md
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "docs(valve): MQTT topic map and user operations for valve config"
```

---

## Task 12: Bench test

With firmware flashed on hardware, verify all 9 test scenarios from the spec.

- [ ] Subscribe to status and alert topics:

```bash
mosquitto_sub \
  --capath /etc/ssl/certs \
  -h twwp-iot.duckdns.org -p 8883 \
  -u twwp_wh_001 -P <MQTT_PASS> \
  -t 'twwp/wh_001/status' -t 'twwp/wh_001/alert' -v
```

- [ ] **Test 1:** `trigger_source: manual` — flow should NOT open valve

```bash
mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"set_trigger_source": "manual"}'
```
Run water through sensor 1. Confirm `valve_open` stays `false` in heartbeat.

- [ ] **Test 2:** `trigger_source: flow` — flow should open valve again

```bash
mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"set_trigger_source": "flow"}'
```
Confirm `valve_open` goes `true` when flow > 0.05 L/min.

- [ ] **Test 3:** `idle_timeout_s: 10` — safety close 10s after flow stops

```bash
mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"set_valve_idle_timeout": 10}'
mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"set_trigger_source": "manual"}'
mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"valve_open": true}'
```
Wait 10s with no flow. Confirm `valve_open` → `false` in heartbeat.

- [ ] **Test 4:** `max_open_s: 10` — safety close 10s after open, even with flow

```bash
mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"set_valve_max_open": 10}'
mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"valve_open": true}'
```
Run water through sensor 1 continuously. Confirm `valve_open` → `false` at ~10s.

- [ ] **Test 5:** `timeout_disable_auto: true` — auto off after safety close

```bash
mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"set_valve_timeout_disable_auto": true}'
```
Trigger safety close. Confirm `valve_auto` → `false` in heartbeat.

- [ ] **Test 6:** `timeout_alert: true` — alert published on safety close

```bash
mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"set_valve_timeout_alert": true}'
```
Trigger safety close. Confirm `VALVE_SAFETY_CLOSE` JSON appears on `twwp/wh_001/alert`.

- [ ] **Test 7:** `valve_type: ball_valve` — warning logged, solenoid fallback

```bash
mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"set_valve_type": "ball_valve"}'
```
Confirm `[VALVE] ball_valve not implemented` warning in serial monitor.

- [ ] **Test 8:** All 6 new fields appear in heartbeat

Inspect one heartbeat. Confirm: `valve_type`, `trigger_source`, `valve_idle_timeout_s`, `valve_max_open_s`, `valve_timeout_disable_auto`, `valve_timeout_alert` all present.

- [ ] **Test 9:** All 6 HA entities appear on device card

Open HA → Devices → TWWP wh_001. Confirm 6 new entities are writable: Valve Type (select), Valve Trigger Source (select), Valve Idle Timeout (number), Valve Max Open Time (number), Timeout Disables Auto (switch), Timeout Publishes Alert (switch).

- [ ] **Final commit (if any fixes needed from bench test)**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add -p
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "fix(valve): bench test corrections"
```

---

## Spec coverage check

| Spec requirement | Task |
|---|---|
| `valve_type` config field with defaults | Task 2 |
| `trigger_source` config field | Task 2 |
| `idle_timeout_s` + `max_open_s` | Task 2 |
| `timeout_disable_auto` + `timeout_alert` | Task 2 |
| Trigger dispatch: flow vs manual vs unknown | Task 3 |
| Idle timer logic | Task 4 |
| Max-open timer logic | Task 4 |
| Safety close sequence (close + disable_auto + alert + SD log) | Task 4 + 5 |
| node.json boot loading | Task 6 |
| NVS overlay over SD | Task 6 |
| NVS persistence on MQTT write | Task 7 |
| 6 new MQTT cmd keys | Task 8 |
| 6 new heartbeat fields | Task 9 |
| 2 select + 2 number + 2 switch HA entities | Task 10 |
| MQTT_TOPIC_MAP.md | Task 11 |
| USER_OPERATIONS.md | Task 11 |
| 9 bench tests | Task 12 |
