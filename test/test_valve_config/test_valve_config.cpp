// test/test_valve_config/test_valve_config.cpp
#include <unity.h>
#include <Arduino.h>   // test/stubs/Arduino.h — millis(), setMillis(), Serial, String
#include <Preferences.h>  // test/stubs/Preferences.h — shared NVS stub (g_nvsStore, class Preferences)

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
static JsonDocument g_fakeNodeJson;
static bool         g_sdJsonAvail = false;
bool storeSd_readJsonFile(const char*, JsonDocument& doc) {
    if (!g_sdJsonAvail) return false;
    doc = g_fakeNodeJson;
    return true;
}

#include "net_mqtt.h"
static char g_lastAlertPayload[512] = "";
static char g_lastAlertTopic[128]   = "";
void netMqtt_publishSub(const char* topic, const char* payload) {
    strncpy(g_lastAlertTopic,   topic,   sizeof(g_lastAlertTopic)   - 1);
    strncpy(g_lastAlertPayload, payload, sizeof(g_lastAlertPayload) - 1);
}

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
    g_sdJsonAvail = false;
    g_fakeNodeJson.clear();
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

void setUp()    { resetAll(); }
void tearDown() {}

// ── Task 4: Safety timer tests ────────────────────────────────────────────────

void test_idle_timeout_fires_at_configured_seconds() {
    actuatorValve_setIdleTimeoutS(10);
    actuatorValve_setTriggerSource("manual");
    setMillis(0);
    actuatorValve_open();
    g_flowRate = 0.0f;
    setMillis(9999);
    actuatorValve_loop();
    TEST_ASSERT_TRUE(actuatorValve_isOpen());
    setMillis(10001);
    actuatorValve_loop();
    TEST_ASSERT_FALSE(actuatorValve_isOpen());
}

void test_idle_timeout_resets_while_flow_present() {
    actuatorValve_setIdleTimeoutS(10);
    actuatorValve_setTriggerSource("manual");
    setMillis(0);
    actuatorValve_open();
    g_flowRate = 0.1f;
    setMillis(15000);
    actuatorValve_loop();
    TEST_ASSERT_TRUE(actuatorValve_isOpen());
}

void test_max_open_fires_at_configured_seconds() {
    actuatorValve_setMaxOpenS(10);
    actuatorValve_setTriggerSource("manual");
    setMillis(0);
    actuatorValve_open();
    g_flowRate = 0.5f;
    setMillis(9999);
    actuatorValve_loop();
    TEST_ASSERT_TRUE(actuatorValve_isOpen());
    setMillis(10001);
    actuatorValve_loop();
    TEST_ASSERT_FALSE(actuatorValve_isOpen());
}

void test_max_open_resets_on_valve_close() {
    actuatorValve_setMaxOpenS(10);
    actuatorValve_setTriggerSource("manual");
    setMillis(0);
    actuatorValve_open();
    actuatorValve_close();
    actuatorValve_open();
    setMillis(9999);
    actuatorValve_loop();
    TEST_ASSERT_TRUE(actuatorValve_isOpen());
}

void test_idle_timeout_zero_means_disabled() {
    actuatorValve_setIdleTimeoutS(0);
    actuatorValve_setTriggerSource("manual");
    setMillis(0);
    actuatorValve_open();
    g_flowRate = 0.0f;
    setMillis(99999);
    actuatorValve_loop();
    TEST_ASSERT_TRUE(actuatorValve_isOpen());
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

// ── Task 5: Safety close sequence tests ──────────────────────────────────────

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
    TEST_ASSERT_EQUAL(0, g_lastAlertPayload[0]);
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

// ── Task 6: Boot loading from node.json + NVS overlay ────────────────────────

void test_load_config_reads_valve_block_from_sd() {
    g_sdJsonAvail = true;
    g_fakeNodeJson["valve"]["valve_type"]           = "solenoid";
    g_fakeNodeJson["valve"]["trigger_source"]       = "manual";
    g_fakeNodeJson["valve"]["idle_timeout_s"]       = 120;
    g_fakeNodeJson["valve"]["max_open_s"]           = 300;
    g_fakeNodeJson["valve"]["timeout_disable_auto"] = true;
    g_fakeNodeJson["valve"]["timeout_alert"]        = false;

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

// ── Task 7: NVS persistence ───────────────────────────────────────────────────

void test_save_nvs_persists_valve_type() {
    actuatorValve_setValveType("solenoid");
    actuatorValve_saveToNvs();

    // Simulate reboot: reset, reload from NVS (SD absent)
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

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_after_begin);
    RUN_TEST(test_setters_persist_values);
    RUN_TEST(test_unknown_trigger_source_accepted);
    RUN_TEST(test_flow_trigger_opens_on_flow);
    RUN_TEST(test_flow_trigger_closes_on_no_flow);
    RUN_TEST(test_manual_trigger_does_not_open_on_flow);
    RUN_TEST(test_unknown_trigger_treated_as_manual);
    // Task 4: Safety timers
    RUN_TEST(test_idle_timeout_fires_at_configured_seconds);
    RUN_TEST(test_idle_timeout_resets_while_flow_present);
    RUN_TEST(test_max_open_fires_at_configured_seconds);
    RUN_TEST(test_max_open_resets_on_valve_close);
    RUN_TEST(test_idle_timeout_zero_means_disabled);
    RUN_TEST(test_max_open_zero_means_disabled);
    // Task 5: Safety close sequence
    RUN_TEST(test_safety_close_logs_to_sd);
    RUN_TEST(test_safety_close_publishes_alert_when_enabled);
    RUN_TEST(test_safety_close_no_alert_when_disabled);
    RUN_TEST(test_safety_close_disables_auto_when_flag_set);
    RUN_TEST(test_safety_close_leaves_auto_on_when_flag_not_set);
    // Task 6: Boot loading
    RUN_TEST(test_load_config_reads_valve_block_from_sd);
    RUN_TEST(test_load_config_nvs_overlays_sd);
    RUN_TEST(test_load_config_sd_absent_uses_defaults);
    // Task 7: NVS persistence
    RUN_TEST(test_save_nvs_persists_valve_type);
    RUN_TEST(test_save_nvs_persists_all_fields);
    return UNITY_END();
}
