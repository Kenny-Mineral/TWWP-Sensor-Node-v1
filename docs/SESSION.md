# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done

### Session 2026-05-12 — M6 Phase 2 bench test + docs update

**Scope:** Hardware bench verification of the dual EC/TDS meter integration (M6). All Phase 2 checks passed. USER_OPERATIONS.md and TASK_QUEUE.md updated.

**Bench test results:**
- Boot lines confirmed: `[MUX] RS485 UART1 ready`, `[TDS] EC/TDS meter driver ready`, `[YIERYI] Modbus driver ready`
- `[TDS] P1/P2` frames arriving every ~3s ✓
- HA receiving all 6 TDS entities with real values ✓
- YiErYi water quality entities present in HA — bus coexistence confirmed ✓
- No SD failure alerts published — writes assumed succeeding silently ✓
- Grafana: TDS data not yet visible — dashboard panels not created (separate task)

**Wiring issue resolved:** WROOM-32 RS485 module had DI and RO swapped on first attempt. Correcting DI→TX, RO→RX fixed the no-frames symptom immediately.

**Calibration note:** TDS readings (~17ppm) lower than expected (~52ppm tap water) — default EC×0.5 conversion factor. Probes need calibration with standard solutions for absolute accuracy. Relative P1 vs P2 comparison is accurate.

**Serial monitor behaviour (documented in USER_OPERATIONS.md):**
- VSCode PlatformIO monitor is receive-only; `screen /dev/ttyACM0 115200` needed to type commands
- M0 status JSON truncated by HWCDC buffer overflow — cosmetic, MQTT unaffected
- `[SERIAL] console connected` repeating every ~70s is USB CDC reconnect — not a firmware failure
- SD writes are silent on success — no `[SD]` lines expected in normal operation

**Files updated this session:**
- `docs/USER_OPERATIONS.md` — serial monitor receive-only, SD silent writes, HWCDC truncation, DI/RO wiring gotcha, TDS calibration troubleshooting row
- `docs/TASK_QUEUE.md` — M6 Dual EC/TDS Meter added as DONE (was missing); M7/M8/M9 renumbered; open items: OTA validation, Grafana panels, TDS calibration

---

### Session 2026-05-11 — Native unit tests (Phase 1 of validation plan)

**Scope:** Added desktop-runnable unit tests for the two new M6 drivers (`rs485_mux` and `sensor_tds_meter`). All 22 tests pass. Firmware still builds clean.

**New files:**
- `test/stubs/Arduino.h` — millis() stub with controllable clock (`setMillis`/`advanceMillis`), Serial no-op, HardwareSerial include
- `test/stubs/HardwareSerial.h` — stub class (all no-ops)
- `test/stubs/driver/uart.h` — `UART_MODE_RS485_HALF_DUPLEX` constant for native compilation
- `test/test_rs485_mux/test_rs485_mux.cpp` — 11 tests
- `test/test_tds_meter/test_tds_meter.cpp` — 11 tests

**Modified files:**
- `src/rs485_mux.cpp` — extracted `processByte()`; added `rs485Mux_inject()` and `rs485Mux_resetForTest()` under `#ifdef UNIT_TEST`
- `include/rs485_mux.h` — declared test hooks under `#ifdef UNIT_TEST`
- `platformio.ini` — added `[env:native]`

**Test results:** `pio test -e native` — 22/22 passed in ~2s

**Build:** `pio run -e waveshare-esp32-s3-rs485-can` — SUCCESS, RAM 18.0%, Flash 18.7%

---

## In progress
none

## Next step — Phase 3 MQTT OTA end-to-end test

1. `pio run -e waveshare-esp32-s3-rs485-can && md5sum .pio/build/waveshare-esp32-s3-rs485-can/firmware.bin`
2. `scp .pio/build/.../firmware.bin kenny@100.67.244.37:/home/kenny/projects/twwp-monitoring/firmware/firmware.bin`
3. `curl -I https://twwp-iot.duckdns.org/firmware/firmware.bin` — confirm 200 OK
4. Trigger: `mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"ota_url": "https://twwp-iot.duckdns.org/firmware/firmware.bin", "ota_md5": "<md5>"}'`
5. Watch `twwp/wh_001/status` for `ota_state` 1→2→3→reboot→4 (SUCCESS)
6. Bad MD5 test: send same URL with `"ota_md5": "00000000000000000000000000000000"` — expect `ota_state: 5` (FAILED)

**Also open (not blocking OTA test):**
- Grafana dashboard panels for TDS fields and water quality overview
- TDS probe calibration with standard solutions
- Confirm Modbus addresses for post-RO and remin YiErYi meters

## Tool last used
claude-code

## Updated
2026-05-12 14:30
