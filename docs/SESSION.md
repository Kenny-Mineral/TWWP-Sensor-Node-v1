# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done

### Session 2026-05-11 — Native unit tests (Phase 1 of validation plan)

**Scope:** Added desktop-runnable unit tests for the two new M6 drivers (`rs485_mux` and `sensor_tds_meter`). All 22 tests pass. Firmware still builds clean.

**New files:**
- `test/stubs/Arduino.h` — millis() stub with controllable clock (`setMillis`/`advanceMillis`), Serial no-op, HardwareSerial include
- `test/stubs/HardwareSerial.h` — stub class (begin/setPins/setMode/available/read/write/flush all no-ops)
- `test/stubs/driver/uart.h` — `UART_MODE_RS485_HALF_DUPLEX` constant for native compilation
- `test/test_rs485_mux/test_rs485_mux.cpp` — 11 tests: FIFO order, overflow drop, `$` classification, `$WM` dispatch, non-WM discard, mixed traffic, 200ms timeout, consecutive frames
- `test/test_tds_meter/test_tds_meter.cpp` — 11 tests: sscanf parsing, both probes populated, online/offline, staleness boundary at 60s, invalid zone guards, cumulative fail count

**Modified files:**
- `src/rs485_mux.cpp` — extracted `processByte()` from `rs485Mux_loop()`; added `rs485Mux_inject()` and `rs485Mux_resetForTest()` under `#ifdef UNIT_TEST`
- `include/rs485_mux.h` — declared test hooks under `#ifdef UNIT_TEST`
- `platformio.ini` — added `[env:native]` with `-DUNIT_TEST -I test/stubs -I include`

**Test results:** `pio test -e native` — 22/22 passed in ~2s

**Build:** `pio run -e waveshare-esp32-s3-rs485-can` — SUCCESS, RAM 18.0%, Flash 18.7%

**Bug found by tests:** `lastSuccessMs == 0` is used as "never received a frame" sentinel in `sensorTdsMeter_isOnline()`. If a frame arrives exactly when `millis() == 0` (boot instant), the probe would incorrectly report offline. Not a real issue in production (TDS meter sends every ~3s; first frame arrives well after boot), but test cases must process frames at `millis > 0`.

---

### Session 2026-05-11 — Dual EC/TDS meter integration (M6) + build fixes

Implemented rs485_mux + sensor_tds_meter. Firmware built clean. Not yet verified on hardware. See previous SESSION.md entries for details.

## In progress
none

## Next step — BENCH (Phase 2 + 3 of validation plan)

### Phase 2 — M6 hardware integration test
1. Wire EC/TDS meter RS485 A/B to Waveshare board terminal block (same A/B bus as YiErYi meter)
2. Flash: `pio run -t upload` (hold BOOT, tap RESET, release BOOT)
3. Open serial monitor: `pio device monitor`
4. Confirm boot lines: `[MUX] RS485 UART1 ready`, `[TDS] EC/TDS meter driver ready`, `[YIERYI] Modbus driver ready (UART via rs485_mux)`
5. Confirm TDS frames: `[TDS] P1: ...°C EC=... TDS=...` appearing every ~3s
6. Run `wq_status` — confirm `wq_pre_ro_online: true`, watch `fail_count` for 60s — must NOT increase
7. Check MQTT status: `tds_pre_ro_ec/temp/ppm` and `tds_post_ro_*` present with real values
8. Check HA device page: 6 new `tds_*` entities visible and updating
9. Wait 60s, then `sdcat /data/<today>.csv` — confirm 6 populated TDS columns

### Phase 3 — MQTT OTA end-to-end test
1. `pio run -e waveshare-esp32-s3-rs485-can && md5sum .pio/build/waveshare-esp32-s3-rs485-can/firmware.bin`
2. `scp .pio/build/.../firmware.bin kenny@100.67.244.37:/home/kenny/projects/twwp-monitoring/firmware/firmware.bin`
3. `curl -I https://twwp-iot.duckdns.org/firmware/firmware.bin` — confirm 200 OK
4. Trigger: `mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"ota_url": "https://twwp-iot.duckdns.org/firmware/firmware.bin", "ota_md5": "<md5>"}'`
5. Watch `twwp/wh_001/status` for `ota_state` 1→2→3→reboot→4 (SUCCESS)
6. Bad MD5 test: send same URL with `"ota_md5": "00000000000000000000000000000000"` — expect `ota_state: 5` (FAILED)

## Tool last used
claude-code

## Updated
2026-05-11 15:45
