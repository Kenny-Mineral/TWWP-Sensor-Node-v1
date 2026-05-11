// test/test_valve_config/test_valve_config.cpp
#include <unity.h>
#include <Arduino.h>   // test/stubs/Arduino.h — millis(), setMillis(), Serial

// ── Stubs ────────────────────────────────────────────────────────────────────

#include "sensor_flow.h"
static float g_flowRate = 0.0f;
float sensorFlow_getRateLpm(uint8_t) { return g_flowRate; }

#include "store_sd.h"
static char g_lastLogEvent[256] = "";
bool storeSd_logEvent(const char* msg) {
    strncpy(g_lastLogEvent, msg, sizeof(g_lastLogEvent) - 1);
    return true;
}

#include "net_mqtt.h"
static char g_lastAlertPayload[512] = "";
static char g_lastAlertTopic[128]   = "";
void netMqtt_publishSub(const char* topic, const char* payload) {
    strncpy(g_lastAlertTopic,   topic,   sizeof(g_lastAlertTopic)   - 1);
    strncpy(g_lastAlertPayload, payload, sizeof(g_lastAlertPayload) - 1);
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
    g_lastLogEvent[0]     = '\0';
    g_lastAlertPayload[0] = '\0';
    g_lastAlertTopic[0]   = '\0';
    g_nvsStore.clear();
    actuatorValve_begin();
}

// ── Tests ─────────────────────────────────────────────────────────────────────

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

void setUp()    { resetAll(); }
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_after_begin);
    RUN_TEST(test_setters_persist_values);
    RUN_TEST(test_unknown_trigger_source_accepted);
    return UNITY_END();
}
