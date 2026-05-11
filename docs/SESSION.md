# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done

### Session 2026-05-11 — Dual EC/TDS meter integration (M6) + build fixes

**Scope:** Integrated a standalone ESP32+ADS1115 dual EC/TDS meter into the firmware. The meter broadcasts unsolicited ASCII frames (`$WM,<temp1>,<ec1>,<ppm1>,<temp2>,<ec2>,<ppm2>\r\n`) over the same RS485 bus as the YiErYi Modbus meter.

**New files created:**
- `include/rs485_mux.h` + `src/rs485_mux.cpp` — protocol multiplexer: owns UART1, routes `$WM` frames to TDS driver and all other bytes to a 64-byte Modbus FIFO feeding sensor_yieryi
- `include/sensor_tds_meter.h` + `src/sensor_tds_meter.cpp` — TDS meter driver: frame parser, 60s staleness watchdog, getters for temp/ec/ppm per zone

**Files modified:**
- `src/sensor_yieryi.cpp` — replaced 5 direct `rs485Serial.*` calls with `rs485Mux_*` equivalents; removed UART1 init block (moved to mux); removed `HardwareSerial` and `driver/uart.h` includes
- `src/main.cpp` — added mux/TDS begin+loop hooks; 6 new CSV columns (`tds_*`); 12 new status fields per zone (ec, temp, ppm, online, fail_count, last_error × 2 zones); 6 HA discovery entities; `addTdsMeterStatus()` helper
- `platformio.ini` — pinned `platform = espressif32 @ 6.9.0` to prevent pioarduino fork auto-upgrade
- `src/net_ota.cpp` — replaced deprecated `mbedtls_md5_starts/update/finish` with `_ret` variants (IDF 4.x active API)
- `src/watchdog.cpp` — replaced IDF 5.x struct-based `esp_task_wdt_init` with IDF 4.x `esp_task_wdt_init(timeout, panic)` signature

**Build result:** `pio run` succeeded. Zero errors.

**Not yet verified on hardware:** EC/TDS meter not yet wired to the Waveshare RS485 terminal block. Build-only verification.

---

### Session 2026-05-08 (continued) — OTA remote-path hardening handoff

OTA URL parser updated: accepts `http://` and `https://`, follows up to 3 redirects, validates HTTPS against `OTA_CA_CERT` (falls back to `MQTT_CA_CERT`). `USER_OPERATIONS.md` updated. Build succeeded (RAM 18.7%, Flash 22.4%).

### Session 2026-05-08 — M5 hardware test + YiErYi driver

First live hardware test of YiErYi RS485-3177. A/B polarity swapped on first attempt; swapping one end fixed it. Confirmed Modbus frame layout, ORP sign-bit encoding, TDS = EC × 0.5. Added calibration date tracking. Build succeeded.

### Session 2026-05-05 — HA dashboard + monitoring stack

InfluxDB 3 Core + Grafana deployed on Hetzner VPS. HA writing live data. Full Lovelace dashboard deployed to `wh-001` view. Key fixes: HA influxdb YAML must be UI-only, not YAML-included.

## In progress
none

## Next step
- Wire EC/TDS meter RS485 A/B terminals to Waveshare node terminal block; confirm shared bus coexistence with YiErYi meter
- Flash firmware and verify serial monitor shows `[TDS] P1...P2...` frames every ~3s; confirm YiErYi `wq_pre_ro_fail_count` does NOT increase (bus coexistence test)
- Check MQTT status payload for `tds_pre_ro_*` and `tds_post_ro_*` fields with real values
- Verify 6 new HA TDS entities appear in device list and update live
- Validate SD CSV has 6 new populated TDS columns in today's log file
- Perform one real MQTT-driven OTA validation using a hosted `firmware.bin` URL
- Record first calibration dates via MQTT: `{"set_wq_pre_ro_ph_cal_date": "2026-05-08", ...}`

## Tool last used
claude-code

## Updated
2026-05-11 14:30
