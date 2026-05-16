# TWWP Task Queue

Ordered milestone task list. Pick up at the first unchecked `[ ]` item.
Design rules, locked decisions, and standing rules are in `docs/FIRMWARE_ARCHITECTURE.md`.

User-facing command, setup, config, troubleshooting, SD, MQTT/offload, or monitor-output changes must update `docs/USER_OPERATIONS.md` in the same commit.

---

## M0 — bring-up

### M0.1 — User-side prep
- [x] Copy `include/secrets.h.sample` → `include/secrets.h`. Fill in MQTT_HOST, MQTT_PORT (8883), MQTT_USER, MQTT_PASS, MQTT_CA_CERT, NODE_ID.
- [x] Format microSD card FAT32. Insert with CR2032 in DS3231.
- [x] Wire per `docs/WIRING_M0.md`.
- [x] `pio run` — compiles clean.
- [x] `pio run -t upload` + `pio device monitor`.
- [x] Verify serial output matches `WIRING_M0.md` "what to look for" section.
- [x] Drip water on leak probe → HA `binary_sensor` flips + entry in `/log/YYYY-MM-DD.csv`.

### M0.2 — Offline buffering
- [x] Block MQTT (kill broker or pull WiFi).
- [x] Trigger 5–10 leak transitions.
- [x] Confirm `/buf/` accumulates files.
- [x] Restore broker → files drain in order, HA shows historical transitions.

### M0.3 — Polish
- [x] **SD serial maintenance.** `sdls`, `sdcat`, `sdrm`, `sdinfo`, and `sdprune` over USB serial.
- [x] **SD retention config.** Optional `sd.retention_days`, `sd.auto_prune`, and `sd.serial_commands_enabled` loaded from `/config/node.json`.
- [x] **Buffer overflow cap.** `storeSd_bufferMessage()` drops oldest before writing and appends warning to `/log/crashes.txt`.
- [x] **SD-failure surfacing.** On write failure in `store_sd.cpp`, publish `"sd write failed: <context>"` to `twwp/<id>/log` via MQTT (rate-limited 1/min).
- [x] **Reset-creds gesture.** `digitalRead(PIN_RESET_CREDS) == LOW` held > 5s in `loop()` → `netWifi_resetCredentials()`.
- [x] **Heartbeat enrichment.** Added `wifi_ssid`, `ip`, `mqtt_buffer_count` to heartbeat JSON.
- [x] **HA device availability.** `fillHaDevice()` helper extracted — shared by all discovery payloads.

---

## M0.5 — TLS + security hardening ✓ DONE

All items complete. TLS on port 8883 confirmed working end-to-end.

### Server-side
- [x] Let's Encrypt cert on `twwp-iot.duckdns.org`.
- [x] `mosquitto.conf`: listener 8883, cert/key/cafile, `allow_anonymous false`, `password_file`.
- [x] `ufw`: 1883 blocked, 8883 open.
- [x] Per-device credentials: `twwp_wh_001` in passwordfile.
- [x] Verified: MQTT TLS connects cleanly.

### Firmware-side
- [x] `secrets.h.sample`: `MQTT_PORT 8883`, `MQTT_CA_CERT` PEM literal.
- [x] `net_mqtt.cpp`: `WiFiClientSecure` + `setCACert(MQTT_CA_CERT)`. Never falls back to plain MQTT.
- [x] Client ID: `"twwp_" NODE_ID` (built in `netMqtt_loop()`).
- [x] TLS handshake failure: logs SSL error to serial + SD crash log, retries with exponential backoff.
- [x] Create `docs/MQTT_TOPIC_MAP.md` — topic list with QoS, retain, and description. (Done in M1.)

---

## M1 — Hall flow sensor

Sensors confirmed: USN-HS06PE (K=38, 0.3–6.0 LPM) and USN-HS06PS (K=200, 0.1–1.0 LPM). See `docs/COMPONENTS.md` for full sensor library.
K value loaded from `/config/node.json` (`flow.k_factor_1`, `flow.k_factor_2`) — no reflash needed to swap sensors.

- [x] Confirm part number. USN-HS06PE (K=38) and USN-HS06PS (K=200).
- [x] Replace `sensor_flow_stub.{h,cpp}` with `sensor_flow.{h,cpp}`.
  - Interrupt-driven pulse counter on GPIO4 (flow #1) and GPIO5 (flow #2).
  - Load K factor from `node.json`; default 1000 if absent.
  - Expose: `sensorFlow_getRateLpm(uint8_t ch)`, `sensorFlow_getTotalL(uint8_t ch)`, plus today/week/month/year subtotals and `sensorFlow_getKFactor(uint8_t ch)`.
  - Two-layer persistence: NVS (Preferences) every 10 s, SD `/config/flow_total.json` every 60 s.
- [x] HA discovery: `flow_rate` (`measurement`, `L/min`), `flow_total` (`total_increasing`, `L`), today/week/month/year (`measurement`, `L`), and K factor (diagnostic) per channel.
- [x] Add all flow and K-factor fields to heartbeat JSON.
- [x] Time-series CSV log: `/data/YYYY-MM-DD.csv` written every 60 s with all current sensor readings.
- [x] Create `docs/MQTT_TOPIC_MAP.md` with all current topics.
- [x] Update `docs/PIN_ALLOCATION.md` — mark GPIO4/5 confirmed for flow sensors.
- [x] **Flow sensor firmware improvements** — ISR debounce, low-flow cutoff, multi-point K-table with linear interpolation, moving-average smoothing (configurable 1–20 samples), uint64_t raw pulse totals as authoritative volume source, configurable debounce (100–10000 µs) per channel, NVS + SD persistence of raw pulses, HA discovery for diagnostics (`pulses_raw`, `k_applied`, `flow_avg_window`) and config entities (K-table text, debounce number, avg window number), backward-compatible with single K-factor config. See [`plans/flow-sensor-improvements.md`](plans/flow-sensor-improvements.md).
- [x] **Session timing fields** — `flow_dur_s` (actual flow time, excludes idle gaps) and `idle_s` (tap-off gaps within session) added to session_flow driver, all session payloads (MQTT event, sessions_recent array, status heartbeat), HA discovery sensors, and SD sessions.csv log. HA Markdown card template provided for session list display.
- [x] **Flow sensor model registry** — compile-time registry of known sensors (USN-HS06PE, USN-HS06PS, DWS-MH-02). Model selectable per channel via HA `select` entity or MQTT cmd `set_sensor_model_1/2`. Applying a model loads its K-table, debounce, and minPulses defaults. DWS-MH-02 includes 5-point K-table derived from its F=15Q−2 formula for accurate low-flow measurement. Node.json key: `flow.sensor_model_1/2`. Explicit k_factor/k_table/debounce in node.json still override model defaults.
- [x] **Flow K-factor calibration wizard** — per-channel begin/commit/accept/abort state machine. Set reference volume → Start Cal → fill container → Commit → see suggested K → Accept. MQTT status fields: `cal_state_1/2`, `cal_suggested_k_1/2`, `cal_pulses_since_start_1/2`, `cal_ref_vol_1/2`. HA entities: 2 buttons + 1 number + 1 sensor per channel. Full playbook in `USER_OPERATIONS.md`.

---

## M2 — Pressure sensor

No DS18B20 — temperature comes from YiErYi 3788 RS485 sensor (M5). Pressure sensor not yet purchased.

- [ ] Confirm pressure transducer model + PSI range with user before ordering.
- [ ] Pressure: averaged ADC read on GPIO7. Voltage divider 2:1 (0–5V → 0–2.5V).
- [ ] HA discovery: `pressure` (measurement, kPa or PSI — confirm unit with user).
- [ ] Add `pressure` to heartbeat JSON and `docs/COMPONENTS.md`.

---

## M2.5 — 12V Battery Voltage Monitor ✓ DONE

Hardware: ADS1115 16-bit I²C ADC (0x48) + 100kΩ/33kΩ voltage divider. Shares I²C bus with DS3231 on GPIO9/GPIO3.

- [x] Confirm hardware: ADS1115 + 100kΩ/33kΩ divider. Divider ratio = 4.0303. GAIN_ONE (±4.096V, 0.125 mV/LSB).
- [x] Create `src/sensor_voltage.{h,cpp}` — `sensorVoltage_begin/loop/getVoltageV/getPercentPct/getState/setVMin/setVMax/setCalFactor`. NVS namespace `"voltage"` (keys: v_min 11.8, v_max 12.6, cal 1.0).
- [x] 5-sample moving average for noise suppression. 12-slot ring buffer (60 s history) for charge state detection (±0.05 V/60 s threshold).
- [x] Add `adafruit/Adafruit ADS1X15 @ ^2.5.0` to `platformio.ini`.
- [x] HA discovery: `supply_voltage` (V, device_class=voltage), `supply_voltage_pct` (%, device_class=battery), `supply_voltage_state` (Charging/Discharging/Stable). Config numbers: v_min, v_max, cal_factor — all adjustable from HA without reflash.
- [x] Add `supply_voltage`, `supply_voltage_pct`, `supply_voltage_state`, `voltage_v_min`, `voltage_v_max`, `voltage_cal_factor` to heartbeat JSON.
- [x] MQTT cmd: `set_v_min`, `set_v_max`, `set_voltage_cal` — all persisted to NVS.
- [x] Add `supply_voltage` column to `/data/YYYY-MM-DD.csv` time-series log.
- [x] Update `include/pins.h` — note ADS1115 @ 0x48 sharing GPIO9/GPIO3.

---

## M3 — Actuator command channel

Valve type not yet finalised — currently using a 12V LED + relay module to simulate. Driver is live. **Note:** LED is on NC terminal — driver currently inverted (LOW=relay on=LED off). Revert `actuator_valve.cpp` HIGH/LOW when moving to NO terminal or real valve.

- [ ] Confirm final valve type (motorised ball valve or solenoid) and purchase.
- [ ] Update `docs/COMPONENTS.md` with valve model + URL once purchased.
- [ ] Document final driver circuit in `docs/ACTUATOR_DRIVER.md` once valve type confirmed.
- [x] Replace `actuator_solenoid_stub.{h,cpp}` → `actuator_valve.{h,cpp}`. Active-low relay driver on GPIO8, auto-triggered by flow sensor 1 via `FLOW_ACTIVE_THRESHOLD_LPM`. MQTT `valve_open`/`valve_auto` cmd keys. HA `binary_sensor` discovery. `PIN_VALVE` in `pins.h`.
- [x] MQTT command channel for valve: `{"valve_open": true/false}` and `{"valve_auto": true/false}` on `twwp/<id>/cmd`.
- [ ] Safety: auto-close after configurable idle timeout if no flow activity (e.g. 10 min). Configurable in `node.json`, default off.

---

## M4 — OTA over MQTT ✓ DONE

- [x] **Decision:** MQTT-driven OTA (internet) + ArduinoOTA (LAN) — both supported.
- [x] MQTT-driven: subscribe `twwp/<id>/cmd` for `{"ota_url": "...", "ota_md5": "..."}`, fetch HTTPS with `WiFiClientSecure`, write with `Update.h`.
- [x] MQTT progress published to `twwp/<id>/ota_state` (retained) every 2s during download.
- [x] Rollback: NVS `ota_boot_pending` flag checked on boot; if crash detected within 60s, `esp_ota_mark_app_invalid_rollback_and_reboot()` triggers IDF bootloader rollback to previous partition.
- [x] Serial console: `ota <url> [md5]` and `ota_state` commands.
- [x] ArduinoOTA: enabled in `netOta_loop()` when state = IDLE, hostname `twwp-<NODE_ID>`.
- [x] HA discovery: `sensor.twwp_<id>_ota_state` and `sensor.twwp_<id>_ota_progress` diagnostic entities.
- [x] Server: Hetzner nginx serves `https://twwp-iot.duckdns.org/firmware/` with TLS.
- [x] Documentation: USER_OPERATIONS, MQTT_TOPIC_MAP, FIRMWARE_ARCHITECTURE updated.
- [x] **`platformio.ini` OTA env fix** — `[env:ota]` `extends` directive corrected to `env:waveshare-esp32-s3-rs485-can` (was missing `env:` prefix causing `UndefinedEnvPlatformError`). `upload_port` set to direct IP `192.168.20.18` (mDNS `twwp-wh_001.local` does not resolve on this network).

## Standalone 12V boot fix ✓ DONE (2026-05-04)

**Root cause:** `ARDUINO_USB_CDC_ON_BOOT=1` + `ARDUINO_USB_MODE=1` puts Serial into HWCDC mode. Without a USB host, every `Serial.println()` in setup() blocks indefinitely. Accumulated block time caused the hardware watchdog to fire before setup() completed — device never came online on 12V-only power.

- [x] Added `Serial.setTxTimeoutMs(0)` in `src/main.cpp` immediately after `Serial.begin(115200)`. Makes HWCDC TX non-blocking: writes drop silently rather than blocking when no USB host is present.
- [x] Reduced `delay(1000)` → `delay(500)` after Serial.begin (minor, boot is faster).
- [x] USB cable flash succeeded (2026-05-04). Also fixed `lib_deps` being in `[env:ota]` instead of base env — base env build was missing all third-party libraries.
- [x] Verify: unplug USB, confirm HA stays green on 12V-only. ✓ Confirmed 2026-05-05.
- [ ] **Optional:** Fix corrupted NVS `k_factor_2` (stored as 2000, should be 20700). Send `{"k_factor_2": 20700}` via MQTT on `twwp/wh_001/cmd`. Firmware already uses correct value from k-table so this is low priority.

---

## Monitoring Stack — InfluxDB 3 Core + Grafana

Server-side analytics layer. Local project: `/home/kenny/twwp-monitoring/`. Not firmware.

- [x] Create `docker-compose.yml` — InfluxDB 3 Core (`influxdb:3-core`) + Grafana (`grafana/grafana-oss`) on Hetzner VPS.
- [x] Grafana datasource auto-provisioned (InfluxQL, bearer token auth).
- [x] HA integration config (`ha-config/influxdb.yaml`) — all live TWWP entities + 12 water quality stubs.
- [x] Three-zone water quality schema locked in: `pre_ro`, `post_ro`, `remin` — entity names and M5 firmware field contract documented.
- [x] Deploy guide: `docs/SETUP.md` with UFW rules, token creation, HA wiring steps.
- [x] **Deploy to server** — `docker compose up -d` on Hetzner VPS. InfluxDB 3 Core + Grafana running. Token generated, HA wired up.
- [x] Verify HA writing — InfluxDB confirmed receiving data (85 events on first flush, ~2 events/10s thereafter). Grafana Explore shows TWWP measurements.
- [x] **Critical fix** — `influxdb: !include influxdb.yaml` in `configuration.yaml` silently blocks entire influxdb component (YAML schema validation fails without connection keys). Removed the include line entirely. InfluxDB integration is now 100% UI-managed (Settings → Integrations → InfluxDB). `ha-config/influxdb.yaml` kept as documentation only — do NOT reference it from configuration.yaml.
- [x] Grafana port binding fixed — changed from `127.0.0.1:3000` to `0.0.0.0:3000` so Tailscale traffic can reach it. Accessible at `http://100.67.244.37:3000`.
- [x] Create Grafana dashboards: Overview, Flow History, Water Quality (3-zone), System. All 4 deployed and live. Panels rebuilt to use confirmed InfluxDB entity IDs — previous dashboards had wrong entity IDs for TDS (used non-existent Yieryi pre/post-RO entities) and battery/WiFi (used non-existent `%` measurement). Water Quality now correctly uses `pre_ro_tds_meter_*` and `post_ro_tds_meter_*` for Pre/Post-RO zones and `remineralised_water_quality_*` for Remin. EC (µS/cm) panels omitted — that measurement is empty in InfluxDB. Dashboard JSON provisioned from server at `/home/kenny/projects/twwp-monitoring/grafana/provisioning/dashboards/`.
- [x] Export final dashboard JSON committed to `grafana/provisioning/dashboards/`.

---

## M5 — YiErYi RS485-3177/3178 (water quality sensors)

Three sensors: pre-RO filter, post-RO filter, remineralised. Each: pH, ORP, EC, temp.
Firmware driver implemented from the vendor Modbus register sheet. Hardware response validation still required on the Waveshare RS485 port.

- [x] Read vendor `Modbus Communication Data Format-V1.01.xlsx` to confirm 9600 8N1, read register `0x0000` count `4`, 16-byte response, and pH/ORP mode register `0x0005`.
- [x] Add `sensor_yieryi.{h,cpp}` using UART1 (`UART_MODE_RS485_HALF_DUPLEX`, GPIO17/18, GPIO21 auto DE/RE).
- [x] Publish per-zone fields in `twwp/<id>/status` JSON — field names locked in (see MQTT_TOPIC_MAP.md):
  `wq_pre_ro_ph`, `wq_pre_ro_orp`, `wq_pre_ro_ec`, `wq_pre_ro_temp`,
  `wq_post_ro_ph`, `wq_post_ro_orp`, `wq_post_ro_ec`, `wq_post_ro_temp`,
  `wq_remin_ph`, `wq_remin_orp`, `wq_remin_ec`, `wq_remin_temp`.
- [x] HA MQTT discovery: 12 sensor entities (4 metrics × 3 zones) — names locked in, Grafana/InfluxDB already configured for them.
- [x] Staleness watchdog: no successful read in 60s → status values become `null`.
- [x] **Hardware test** (2026-05-08): wired one pre-RO meter, confirmed CRC-valid frame, pH/EC/temp/ORP all live in MQTT. A/B polarity was swapped on first attempt — symptom is `read timeout` + empty `raw_hex`.
- [x] **ORP encoding fix**: 3177 uses bit-15 sign flag (not standard int16). `0x81BB` → +443 mV, not -32325. Fix in `parseReadResponse()`. Confirmed against live meter.
- [x] **TDS/PPM**: `tdsPpm = ecUsCm * 0.5f`. Confirmed 77 µS/cm → 38.5 ppm matches meter display. Published as `wq_<zone>_tds_ppm`.
- [x] **Calibration date tracking**: `ph_cal_date`, `orp_cal_date`, `ec_cal_date` per zone stored in `node.json`, published in status, settable via MQTT cmd `set_wq_<zone>_*_cal_date`. HA text sensor entities per zone. See `USER_OPERATIONS.md`.
- [x] **ArduinoOTA fix** (net_ota.cpp): `handle()` now called in all non-active states; `begin()` now always runs regardless of boot-flag path; `onError` sets IDLE not FAILED.
- [x] **MQTT OTA remote-hosting hardening** (net_ota.cpp): OTA URL parser now accepts `http://` and `https://`, follows up to 3 redirects, and supports a separate `OTA_CA_CERT` for firmware hosts that do not share the broker CA. Reason: off-site OTA was failing under realistic hosting setups even though the base OTA feature existed.
- [ ] Investigate ArduinoOTA LAN OTA — UDP invitation reaches device (port 3232 open) but device does not respond. Likely router AP/client isolation. Test: disable AP isolation or use tcpdump on device's RSSI-confirmed AP.
- [x] Validate MQTT-driven OTA end-to-end after the remote-hosting hardening. Use a real hosted `firmware.bin`, confirm `ota_state` progress and reboot on success, or capture exact `ota_error` on failure.
- [ ] Confirm Modbus addresses on post-RO and remineralised meters before enabling those zones in `/config/node.json`.
- [x] Grafana dashboards complete — see Monitoring Stack section above.

---

## M6 — Dual EC/TDS Meter (RS485 ASCII mux) ✓ DONE

Standalone ESP32-WROOM-32 + ADS1115 EC/TDS meter transmitting `$WM,...` ASCII frames on shared RS485 bus. Multiplexed with YiErYi Modbus by first byte (`$` vs `0x01`).

- [x] `src/rs485_mux.{h,cpp}` — byte classifier, 64-byte Modbus FIFO, ASCII accumulator, 200ms timeout guard, dispatches `$WM` frames to TDS driver. `processByte()` extracted for testability.
- [x] `src/sensor_tds_meter.{h,cpp}` — `sscanf` parser for 6-field `$WM` frame, dual-probe struct (PRE_RO / POST_RO), 60s staleness watchdog, fail counter.
- [x] Publish 12 TDS fields in `twwp/<id>/status` — `tds_pre_ro_ec/temp/ppm/online/fail_count/last_error` × 2 zones.
- [x] HA MQTT discovery: 6 sensor entities (ec/temp/ppm × 2 zones).
- [x] SD CSV: 6 new columns appended to `/data/YYYY-MM-DD.csv`.
- [x] Native unit tests: 22 tests (11 mux + 11 TDS parser) — `pio test -e native` all pass. `[env:native]` added to `platformio.ini`. Stubs in `test/stubs/`.
- [x] **Phase 2 bench test (2026-05-12)**: `[TDS] P1/P2` frames confirmed every ~3s. HA receiving all 6 TDS entities with real values. YiErYi water quality also present — bus coexistence confirmed. No SD failure alerts.
- [x] `docs/USER_OPERATIONS.md` updated: serial monitor receive-only, SD silent writes, HWCDC JSON truncation, DI/RO wiring gotcha, TDS calibration note.
- [x] **TDS probe calibration** — Software EC correction factor per zone (NVS persisted, default 1.0). Wizard: set ref EC → begin → commit (snapshots raw EC) → shows suggested factor → accept/abort. Direct set also available. Raw EC exposed alongside calibrated EC/ppm. Full playbook in `USER_OPERATIONS.md`. MQTT cmd keys: `tds_cal_begin/commit/accept/abort`, `set_tds_cal_ref_ec_0/1`, `set_tds_pre_ro/post_ro_ec_cal_factor`, `set_tds_pre_ro/post_ro_cal_date`.
- [x] **Grafana panels**: TDS and temperature panels live in twwp-water-quality dashboard.

**Wiring note:** WROOM-32 RS485 module DI (Data In = TX from MCU) and RO (Receiver Output = RX to MCU) were swapped on first bench attempt. Correct wiring: DI→TX pin, RO→RX pin of WROOM-32.

---

## M7 — HealthService

Note: CalibrationService is superseded — calibration is now handled per-driver (flow K-factor wizard in `sensor_flow.cpp`, EC correction factor in `sensor_tds_meter.cpp`) with full HA and MQTT integration. No separate CalibrationService needed.

- [ ] `src/services/HealthService.{h,cpp}`: validates sensor readings, sets `flow_ok`, `pressure_ok`, `power_ok` flags — published in status and surfaced as HA binary sensors.
- [ ] Extend `SensorData` struct — single authoritative snapshot per loop tick rather than ad-hoc reads in MQTT publish path.

---

## M8 — AlertService + TelemetryService

- [ ] `src/services/AlertService.{h,cpp}`: fires on state change only. Types: LEAK_DETECTED, FLOW_ANOMALY, PRESSURE_OUT_OF_RANGE, LOW_VOLTAGE, SENSOR_FAILURE, DEVICE_REBOOT.
- [ ] `src/services/TelemetryService.{h,cpp}`: sends snapshot to `twwp/<id>/status` every 10s.
- [ ] Main loop order: `healthService → alertService → ruleEngine → telemetryService`.

---

## M8.5 — MQTT Data Usage Optimization

Current heartbeat generates ~465 MB/month (status @ 10s interval + sessions). Investigate and optimize data footprint if network constraints require it.

- [ ] **Audit status payload size** — parse actual JSON from serial monitor, measure compressed size via gzip. Identify low-value diagnostic fields that could be moved to on-demand queries or removed entirely.
- [ ] **Evaluate heartbeat interval trade-off** — test 30s interval (reduces to ~155 MB/month). Measure HA responsiveness impact (valve trigger, leak detection latency). Config: `HEARTBEAT_INTERVAL_MS` in `include/config.h`.
- [ ] **Selective field updates** — publish only changed fields in status heartbeat rather than full JSON snapshot (requires delta tracking). Higher complexity; defer unless data usage is blocking.
- [ ] **Session batching** — if water treatment cycles are frequent, consider grouping session-end publishes (e.g., buffer 5 sessions, publish batch). Trade-off: HA real-time visibility vs. lower MQTT traffic.
- [ ] **Compression candidate** — if broker supports compressed payloads (rare), evaluate gzip before publish.

**Baseline for reference:** 1.8 KB/10s heartbeat + ~2.5 KB sessions per cycle. Monthly: ~465 MB (status only).

---

## M-Upload — Mobile Offline Buffer Upload

Field users can push buffered node data via phone without WiFi. Phone uses its own internet (cellular) to relay buffered MQTT messages to the server via HTTPS. Triggered by QR code or app button.

### M-Upload.1 — Firmware: Concurrent WiFi AP + HTTP Server

Concurrent STA+AP: node stays connected to home WiFi (or offline) while broadcasting its own AP for phones to join. HTTP server on AP interface exposes buffer management API.

- [x] **New module `src/net_ap.{h,cpp}`** — WiFi AP management + HTTP server:
  - `netAp_begin()` — initializes AP SSID from secrets.h (`AP_SSID`), password from `AP_PASS`.
  - `netAp_start(duration_s)` — activates AP for N seconds, then auto-deactivates.
  - `netAp_isActive()` — returns true while AP is broadcasting.
  - HTTP server starts when AP is active, stops when it deactivates.
  - Call `netAp_loop()` in main loop to handle timer expiry.

- [x] **HTTP server endpoints (port 80):**
  - `GET /` → serves upload page (from SD `/config/upload.html` or PROGMEM fallback).
  - `GET /api/buffer/stats` → JSON: `{"count": N, "oldest_ts": ..., "newest_ts": ..., "est_bytes": N}`.
  - `GET /api/buffer/fetch?count=N` → JSON array of N oldest buffer messages.
  - `POST /api/buffer/ack` body `{"count": N}` → deletes N oldest files from `/buf/`.

- [x] **MQTT trigger:** Handle `{"start_ap": true, "duration_s": 300}` on `twwp/<id>/cmd` → calls `netAp_start()`.

- [x] **Automatic AP trigger on WiFi loss:** Track WiFi connectivity state. If:
  - WiFi STA disconnected for > `AP_AUTO_TRIGGER_LOSS_MS` (default 60s, configurable in `node.json` as `ap.auto_trigger_loss_s`), OR
  - WiFi RSSI weak (< `AP_AUTO_TRIGGER_RSSI_THRESHOLD` dBm, default -75, also in `node.json` as `ap.weak_rssi_threshold`)
  - Then automatically activate AP for `AP_AUTO_DURATION_S` (default 600s = 10 min, also configurable).
  - Reset the timer if WiFi reconnects with strong signal.
  - Log to SD: `[AP] auto-triggered: wifi loss` or `[AP] auto-triggered: weak signal`.

- [x] **Heartbeat fields:** Add `ap_active`, `ap_ssid`, `ap_clients`, `ap_expires_s`, `wifi_rssi`, `wifi_uptime_s` to status JSON.

- [x] **Config constants** (`include/config.h`):
  - `AP_BROADCAST_SSID` format: `"twwp-" NODE_ID` (e.g., `twwp-wh_001`).
  - `AP_DEFAULT_DURATION_S` = 300 (manual trigger).
  - `AP_AUTO_DURATION_S` = 600 (automatic trigger on WiFi loss/weak signal).
  - `AP_AUTO_TRIGGER_LOSS_MS` = 60000 (1 min default, configurable via `node.json`).
  - `AP_AUTO_TRIGGER_RSSI_THRESHOLD` = -75 dBm (configurable via `node.json`).
  - `AP_PORT` = 80.
  - `AP_GATEWAY_IP` = 192.168.4.1 (ESP32 default, no change needed).

- [x] **secrets.h.sample:** Add `AP_SSID` and `AP_PASS` placeholders.

- [x] **Library selection:** Confirm ESPAsyncWebServer or Arduino WebServer available in `platformio.ini`. Add if missing.
  - Implemented with core `WebServer`; `pio run` confirms it links in the current toolchain.

### M-Upload.2 — Relay Server: HTTPS Upload Endpoint (Hetzner VPS)

Server-side service receives batched messages from phones and publishes to MQTT broker.

- [x] **Location:** `/home/kenny/twwp-monitoring/` in this local workspace (server deploy target may still be `/home/kenny/projects/twwp-monitoring/` on the VPS).

- [x] **New service in docker-compose.yml:**
  - Image: Python 3.11 + FastAPI (or Node.js + Express).
  - Internal port: 8000.
  - Environment: `MQTT_BROKER=mosquitto`, `MQTT_USER`, `MQTT_PASS`.
  - No external port — proxied by nginx.
  - Implemented as `relay-service` in `/home/kenny/twwp-monitoring/docker-compose.yml`, bound to `127.0.0.1:8000` for nginx-only access.

- [x] **Endpoint:** `POST /api/v1/node-upload`
  - Request: `{"node_id": "wh_001", "token": "...", "messages": [{"t": "topic", "p": "payload"}, ...], "uploader_email": "optional@email.com"}`
  - `uploader_email` is optional — may be null or absent (anonymous/skipped).
  - Response: `{"published": N, "failed": 0}` or error if token invalid.
  - Auth: Bearer token per node (stored in `/config/upload_token.json` on node SD, embedded in web page).
  - Implemented in `/home/kenny/twwp-monitoring/upload-relay/app.py`.

- [x] **CRM lead capture:** On each request, append one row to `/data/upload_leads.csv` on the server: `{timestamp, node_id, messages_count, uploader_email_or_blank}`. This is the source of truth for manual CRM outreach. TODO (future): POST email to Mailchimp/Brevo or TWWP Rails app user model.

- [x] **MQTT publish:** Service connects to Mosquitto using `twwp_relay` account (add to `passwordfile`). Publishes each message with QoS 0, not retained.
  - Implemented with TLS MQTT env vars in `.env.example`; still needs live credentials and runtime deployment verification.

- [ ] **nginx routing:** Add location block `^/api/v1/node-upload` → proxy to `http://relay-service:8000`. Keep existing TLS.
  - Sample config added to `/home/kenny/twwp-monitoring/docs/SETUP.md`; not deployed or verified from this workspace.

- [x] **Rate limiting:** Max 500 messages per request, max 1 request per node per minute. Return 429 if exceeded.

- [x] **Token rotation command:** MQTT `{"rotate_upload_token": true}` generates new token, writes to `/config/upload_token.json`, publishes to `twwp/<id>/status`.

### M-Upload.3 — Web Page: Self-Contained Upload UI

Single HTML file served from node. Two flows in one page: anonymous (QR scanner) and member (came via Tap-Map app). Doubles as a TWWP onboarding touchpoint.

- [x] **Location:** `/config/upload.html` on SD card (loaded by HTTP server).

- [x] **PROGMEM fallback:** Embed inline HTML in `net_ap.cpp` as a string literal if SD fails.

- [x] **Anonymous flow** (no URL params — arrived by scanning QR code):
  1. Load → `GET /api/buffer/stats` → display buffer summary with TWWP branding and node ID.
  2. Batch selector: Last 10 / 50 / 100 / All (capped to 500).
  3. **Optional email capture:** "Help TWWP track this upload — enter your email" + skip button. Not a gate — upload proceeds either way.
  4. "Download from node" button → `GET /api/buffer/fetch?count=N` → JS stores in memory.
  5. Reconnect prompt → detect `navigator.onLine` → enable Upload button.
  6. `POST https://twwp-iot.duckdns.org/api/v1/node-upload` with auth token + optional email.
  7. On success → `POST /api/buffer/ack` → SD buffer cleared.
  8. **Onboarding prompt:** "Thank you! Want free access to TWWP water? [Create account →]" linking to `https://app.thewholeywaterproject.com/users/sign_up`.
  - Firmware-side page implemented. End-to-end browser upload still depends on relay-side CORS / upload endpoint work in M-Upload.2.

- [x] **Member flow** (URL param `?member=1` — deeplinked from Tap-Map app after joining node WiFi):
  - Skip email capture (member already identified server-side by the app action that triggered the AP).
  - Header: "Syncing [Node ID] data for TWWP" (shorter, less introductory).
  - Same download → reconnect → upload → ack steps.
  - No onboarding prompt at end (already a member).

- [x] **Design constraints:**
  - Single HTML file (no external dependencies, no CDN calls — phone may have poor signal after joining AP).
  - All CSS/JS inline. TWWP brand colors (blue/teal tones matching `thewholeywaterproject.com`).
  - Works on iOS Safari and Android Chrome.
  - Node ID and firmware version in header (from stats endpoint response).
  - Upload token from stats endpoint or embedded at page serve time (not hardcoded in PROGMEM version).

### M-Upload.4 — QR Code + OLED Display State

Physical QR code and OLED update when AP is active.

- [ ] **QR content:** WiFi standard format: `WIFI:T:WPA;S:twwp-wh_001;P:<AP_PASS>;;`
  - Generated offline, printed on laminated label (near tap or on device sticker).
  - Below QR: "After joining WiFi, open browser: 192.168.4.1"
  - Node ID in human text.

- [x] **OLED state (in `src/display_oled.cpp`):**
  - When `netAp_isActive()` = true, show new display mode:
    - Line 1: "UPLOAD MODE"
    - Line 2: `SSID: twwp-wh_001`
    - Line 3: `IP: 192.168.4.1`
    - Line 4: Countdown `T: 4m 32s`
  - Refresh countdown every second (or every 10s for power saving).

- [ ] **Optional: QR code on OLED** — Evaluate U8g2 library QR support. If 128×64 resolution is sufficient for a scannable code, render WiFi QR dynamically. Otherwise, text-only display as above.

- [ ] **App integration:** User's web app has "Sync node data" button → sends `{"start_ap": true, "duration_s": 300}` to `twwp/<id>/cmd`. App shows "AP active. Scan QR on the node or at 192.168.4.1."

- [ ] **Physical button trigger (placeholder):** Long-press `PIN_RESET_CREDS` or dedicate a new GPIO. Resolve with `PIN_ALLOCATION.md` — check GPIO conflicts. If conflict exists, document resolution (different timing duration or new GPIO).

### M-Upload.5 — Tap-Map App Integration (Rails)

**Rails app side only** (`app.thewholeywaterproject.com`). Enables members to trigger the upload flow from the web app without needing to be physically at the tap first.

- [ ] **"Sync offline data" button** on the waterhouse/tap detail page:
  - Visible when: `mqtt_buffer_count > 0` from the node's most recent heartbeat (surfaced via HA → Rails API, or direct MQTT subscribe in backend).
  - Greyed out when: `ap_active: true` (AP already running — avoid double-trigger).
  - Hidden when: node is online and buffer is empty.

- [ ] **Button action:** POST to `/waterhouses/:id/sync` (new Rails controller action):
  - Publishes `{"start_ap": true, "duration_s": 300}` to `twwp/<node_id>/cmd` via MQTT from the Rails backend.
  - MQTT client option: `ruby-mqtt` gem or a lightweight sidecar service. **Confirm approach with Tap-Map app architect before implementing** — backend may already have an MQTT publish path.

- [ ] **App UX after trigger:**
  - Toast: "AP active for 5 minutes — head to the tap and scan the QR code."
  - Show deeplink button: "Open upload portal →" → links to `http://192.168.4.1/?member=1`.
  - Note shown: "First connect your phone to the tap's WiFi network (twwp-[node_id]), then open the link."

- [ ] **Status badge on tap card / detail page:**
  - "Last synced: X minutes ago" — from the relay server's `upload_leads.csv` (last row for this node_id) or a dedicated MQTT event.
  - "N messages pending sync" when buffer is non-empty.

---

## M9 — Device lifecycle

- [ ] First-connect registration: publish `{device_id, firmware_version, mac, ip}` to `twwp/register`.
- [ ] Decommission command: `{"action":"decommission"}` → wipe NVS, reboot to captive portal.
- [ ] MQTT rate-limit guard: > 60 publishes/min → back off + warn.
- [ ] Document credential rotation in `docs/DEVICE_LIFECYCLE.md`.
