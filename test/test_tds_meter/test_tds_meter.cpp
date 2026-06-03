// Unit tests for sensor_tds_meter — frame parsing and staleness watchdog.
// Runs on the host via PlatformIO native env; no hardware required.

#include <unity.h>
#include <Arduino.h>   // resolves to test/stubs/Arduino.h — provides millis(), setMillis(), Serial, String
#include <Preferences.h>  // test/stubs/Preferences.h — shared NVS stub

// time_rtc stubs — sensor_tds_meter.cpp calls timeRtc_getISOTimestamp() in calAccept()
#include "time_rtc.h"
String   timeRtc_getISOTimestamp() { return String(""); }
String   timeRtc_getDateString()   { return String(""); }
uint32_t timeRtc_getUnixTime()     { return 0; }
bool     timeRtc_isSynced()        { return true; }
bool     timeRtc_begin()           { return true; }
void     timeRtc_loop()            {}

// ── Driver under test ─────────────────────────────────────────────────────────
// Included directly so it compiles in this TU with the stubs already active.
// sensor_tds_meter.cpp's own #include <Arduino.h> / <Preferences.h> are no-ops (pragma once).
#include "../../src/sensor_tds_meter.cpp"

// ── Test helpers ──────────────────────────────────────────────────────────────

static void resetDriver() {
    setMillis(0);
    sensorTdsMeter_begin();  // memsets all probe state
}

// ── Tests ─────────────────────────────────────────────────────────────────────

void test_offline_before_any_frame() {
    TEST_ASSERT_FALSE(sensorTdsMeter_isOnline(TDS_ZONE_PRE_RO));
    TEST_ASSERT_FALSE(sensorTdsMeter_isOnline(TDS_ZONE_POST_RO));
}

void test_valid_frame_populates_both_probes() {
    sensorTdsMeter_onFrame("$WM,28.5,412.0,206.0,28.1,18.0,9.0\r\n");
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 28.5f,  sensorTdsMeter_getTemp(TDS_ZONE_PRE_RO));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 412.0f, sensorTdsMeter_getEc(TDS_ZONE_PRE_RO));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 206.0f, sensorTdsMeter_getTds(TDS_ZONE_PRE_RO));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 28.1f,  sensorTdsMeter_getTemp(TDS_ZONE_POST_RO));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 18.0f,  sensorTdsMeter_getEc(TDS_ZONE_POST_RO));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 9.0f,   sensorTdsMeter_getTds(TDS_ZONE_POST_RO));
}

void test_valid_frame_sets_online() {
    setMillis(1000);
    sensorTdsMeter_onFrame("$WM,28.5,412.0,206.0,28.1,18.0,9.0\r\n");
    TEST_ASSERT_TRUE(sensorTdsMeter_isOnline(TDS_ZONE_PRE_RO));
    TEST_ASSERT_TRUE(sensorTdsMeter_isOnline(TDS_ZONE_POST_RO));
}

void test_valid_frame_clears_last_error() {
    sensorTdsMeter_onFrame("$WM,junk\r\n");
    sensorTdsMeter_onFrame("$WM,28.5,412.0,206.0,28.1,18.0,9.0\r\n");
    TEST_ASSERT_EQUAL_STRING("ok", sensorTdsMeter_getLastError(TDS_ZONE_PRE_RO));
    TEST_ASSERT_EQUAL_STRING("ok", sensorTdsMeter_getLastError(TDS_ZONE_POST_RO));
}

void test_short_frame_increments_fail_count() {
    sensorTdsMeter_onFrame("$WM,28.5,412.0\r\n");  // only 3 fields
    TEST_ASSERT_EQUAL(1, sensorTdsMeter_getFailCount(TDS_ZONE_PRE_RO));
    TEST_ASSERT_EQUAL(1, sensorTdsMeter_getFailCount(TDS_ZONE_POST_RO));
}

void test_nonnumeric_frame_increments_fail_count() {
    sensorTdsMeter_onFrame("$WM,abc,def,ghi,jkl,mno,pqr\r\n");
    TEST_ASSERT_EQUAL(1, sensorTdsMeter_getFailCount(TDS_ZONE_PRE_RO));
}

void test_fail_count_accumulates() {
    sensorTdsMeter_onFrame("$WM,bad\r\n");
    sensorTdsMeter_onFrame("$WM,bad\r\n");
    sensorTdsMeter_onFrame("$WM,bad\r\n");
    TEST_ASSERT_EQUAL(3, sensorTdsMeter_getFailCount(TDS_ZONE_PRE_RO));
}

void test_stale_after_60s() {
    // Note: lastSuccessMs == 0 is the "never received" sentinel, so the frame
    // must be processed at millis > 0 or isOnline() will always return false.
    setMillis(5000);
    sensorTdsMeter_onFrame("$WM,28.5,412.0,206.0,28.1,18.0,9.0\r\n");
    TEST_ASSERT_TRUE(sensorTdsMeter_isOnline(TDS_ZONE_PRE_RO));

    setMillis(5000 + 60000);  // exactly at boundary — still online
    TEST_ASSERT_TRUE(sensorTdsMeter_isOnline(TDS_ZONE_PRE_RO));

    setMillis(5000 + 60001);  // one ms past — stale
    TEST_ASSERT_FALSE(sensorTdsMeter_isOnline(TDS_ZONE_PRE_RO));
}

void test_invalid_zone_is_offline() {
    TEST_ASSERT_FALSE(sensorTdsMeter_isOnline(2));
    TEST_ASSERT_FALSE(sensorTdsMeter_isOnline(255));
}

void test_invalid_zone_getters_return_zero() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sensorTdsMeter_getTemp(2));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sensorTdsMeter_getEc(2));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sensorTdsMeter_getTds(2));
    TEST_ASSERT_EQUAL(0,          sensorTdsMeter_getFailCount(2));
}

void test_fail_count_is_cumulative_across_success() {
    sensorTdsMeter_onFrame("$WM,bad\r\n");
    sensorTdsMeter_onFrame("$WM,bad\r\n");
    TEST_ASSERT_EQUAL(2, sensorTdsMeter_getFailCount(TDS_ZONE_PRE_RO));
    setMillis(1000);  // must be non-zero — lastSuccessMs==0 is the "never received" sentinel
    sensorTdsMeter_onFrame("$WM,28.5,412.0,206.0,28.1,18.0,9.0\r\n");
    TEST_ASSERT_EQUAL(2, sensorTdsMeter_getFailCount(TDS_ZONE_PRE_RO));  // not reset
    TEST_ASSERT_TRUE(sensorTdsMeter_isOnline(TDS_ZONE_PRE_RO));
}

// ── Unity plumbing ────────────────────────────────────────────────────────────

void setUp()    { resetDriver(); }
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_offline_before_any_frame);
    RUN_TEST(test_valid_frame_populates_both_probes);
    RUN_TEST(test_valid_frame_sets_online);
    RUN_TEST(test_valid_frame_clears_last_error);
    RUN_TEST(test_short_frame_increments_fail_count);
    RUN_TEST(test_nonnumeric_frame_increments_fail_count);
    RUN_TEST(test_fail_count_accumulates);
    RUN_TEST(test_stale_after_60s);
    RUN_TEST(test_invalid_zone_is_offline);
    RUN_TEST(test_invalid_zone_getters_return_zero);
    RUN_TEST(test_fail_count_is_cumulative_across_success);
    return UNITY_END();
}
