// Unit tests for rs485_mux — frame classifier, Modbus FIFO, and injection hook.
// Runs on the host via PlatformIO native env; no hardware required.

#include <unity.h>
#include <Arduino.h>   // resolves to test/stubs/Arduino.h — provides millis(), setMillis(), Serial, HardwareSerial

// ── Mock sensorTdsMeter_onFrame ───────────────────────────────────────────────
// Declared by sensor_tds_meter.h (included inside rs485_mux.cpp).
// Providing the definition here before the #include means this TU has exactly
// one definition and rs485_mux.cpp's calls resolve to it.

#include "sensor_tds_meter.h"

static char g_lastFrame[256] = "";
static int  g_dispatchCount  = 0;

void sensorTdsMeter_onFrame(const char* line) {
    strncpy(g_lastFrame, line, sizeof(g_lastFrame) - 1);
    g_lastFrame[sizeof(g_lastFrame) - 1] = '\0';
    g_dispatchCount++;
}

// ── Driver under test ─────────────────────────────────────────────────────────
// UNIT_TEST is already defined via build_flags (-DUNIT_TEST), so rs485Mux_inject
// and rs485Mux_resetForTest are compiled in.
#include "../../src/rs485_mux.cpp"

// ── Test helpers ──────────────────────────────────────────────────────────────

static void injectStr(const char* s) {
    for (const char* p = s; *p; p++) {
        rs485Mux_inject((uint8_t)*p);
    }
}

// ── Tests ─────────────────────────────────────────────────────────────────────

void test_modbus_byte_goes_to_fifo() {
    rs485Mux_inject(0x01);
    TEST_ASSERT_EQUAL(1,    rs485Mux_available());
    TEST_ASSERT_EQUAL(0x01, rs485Mux_read());
    TEST_ASSERT_EQUAL(0,    rs485Mux_available());
}

void test_multiple_modbus_bytes_maintain_fifo_order() {
    rs485Mux_inject(0x01);
    rs485Mux_inject(0x03);
    rs485Mux_inject(0x00);
    TEST_ASSERT_EQUAL(3,    rs485Mux_available());
    TEST_ASSERT_EQUAL(0x01, rs485Mux_read());
    TEST_ASSERT_EQUAL(0x03, rs485Mux_read());
    TEST_ASSERT_EQUAL(0x00, rs485Mux_read());
    TEST_ASSERT_EQUAL(0,    rs485Mux_available());
}

void test_fifo_overflow_silently_drops() {
    for (int i = 0; i < 64; i++) rs485Mux_inject(0xAA);
    TEST_ASSERT_EQUAL(64, rs485Mux_available());

    rs485Mux_inject(0xBB);  // 65th byte — dropped
    TEST_ASSERT_EQUAL(64, rs485Mux_available());

    for (int i = 0; i < 64; i++) {
        TEST_ASSERT_EQUAL(0xAA, rs485Mux_read());
    }
    TEST_ASSERT_EQUAL(0, rs485Mux_available());
}

void test_dollar_byte_does_not_go_to_fifo() {
    rs485Mux_inject('$');
    TEST_ASSERT_EQUAL(0, rs485Mux_available());
}

void test_complete_wm_frame_dispatches_to_tds() {
    injectStr("$WM,28.5,412.0,206.0,28.1,18.0,9.0\r\n");
    TEST_ASSERT_EQUAL(1, g_dispatchCount);
    TEST_ASSERT_EQUAL(0, rs485Mux_available());  // no bytes leaked to FIFO
}

void test_dispatched_frame_content_is_correct() {
    injectStr("$WM,28.5,412.0,206.0,28.1,18.0,9.0\r\n");
    TEST_ASSERT_NOT_NULL(strstr(g_lastFrame, "$WM,"));
    TEST_ASSERT_NOT_NULL(strstr(g_lastFrame, "28.5"));
    TEST_ASSERT_NOT_NULL(strstr(g_lastFrame, "206.0"));
}

void test_non_wm_dollar_frame_not_dispatched() {
    injectStr("$XX,1,2,3\r\n");
    TEST_ASSERT_EQUAL(0, g_dispatchCount);
    TEST_ASSERT_EQUAL(0, rs485Mux_available());
}

void test_modbus_bytes_after_frame_are_unaffected() {
    injectStr("$WM,28.5,412.0,206.0,28.1,18.0,9.0\r\n");
    rs485Mux_inject(0x01);
    rs485Mux_inject(0x03);
    TEST_ASSERT_EQUAL(1,    g_dispatchCount);
    TEST_ASSERT_EQUAL(2,    rs485Mux_available());
    TEST_ASSERT_EQUAL(0x01, rs485Mux_read());
    TEST_ASSERT_EQUAL(0x03, rs485Mux_read());
}

void test_partial_frame_timeout_discards_and_no_dispatch() {
    setMillis(1000);
    injectStr("$WM,28.5,412");  // partial — never terminated
    setMillis(1201);             // advance past 200ms guard
    rs485Mux_inject('X');        // trigger timeout check
    TEST_ASSERT_EQUAL(0, g_dispatchCount);
}

void test_frame_shorter_than_5_bytes_not_dispatched() {
    // $WM\n is only 4 chars — below the minimum length check
    rs485Mux_inject('$');
    rs485Mux_inject('W');
    rs485Mux_inject('M');
    rs485Mux_inject('\n');
    TEST_ASSERT_EQUAL(0, g_dispatchCount);
}

void test_second_frame_after_first_dispatched() {
    injectStr("$WM,28.5,412.0,206.0,28.1,18.0,9.0\r\n");
    injectStr("$WM,29.0,500.0,250.0,29.0,20.0,10.0\r\n");
    TEST_ASSERT_EQUAL(2, g_dispatchCount);
}

// ── Unity plumbing ────────────────────────────────────────────────────────────

void setUp() {
    setMillis(0);
    g_dispatchCount = 0;
    g_lastFrame[0]  = '\0';
    rs485Mux_resetForTest();
}

void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_modbus_byte_goes_to_fifo);
    RUN_TEST(test_multiple_modbus_bytes_maintain_fifo_order);
    RUN_TEST(test_fifo_overflow_silently_drops);
    RUN_TEST(test_dollar_byte_does_not_go_to_fifo);
    RUN_TEST(test_complete_wm_frame_dispatches_to_tds);
    RUN_TEST(test_dispatched_frame_content_is_correct);
    RUN_TEST(test_non_wm_dollar_frame_not_dispatched);
    RUN_TEST(test_modbus_bytes_after_frame_are_unaffected);
    RUN_TEST(test_partial_frame_timeout_discards_and_no_dispatch);
    RUN_TEST(test_frame_shorter_than_5_bytes_not_dispatched);
    RUN_TEST(test_second_frame_after_first_dispatched);
    return UNITY_END();
}
