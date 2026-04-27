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
- [ ] Create `docs/MQTT_TOPIC_MAP.md` — topic list with QoS, retain, and description. (Deferred to M1.)

---

## M1 — Hall flow sensor

Sensors confirmed: USN-HS06PE (K=38, 0.3–6.0 LPM) and USN-HS06PS (K=200, 0.1–1.0 LPM). See `docs/COMPONENTS.md` for full sensor library.
K value loaded from `/config/node.json` (`flow.k_factor_1`, `flow.k_factor_2`) — no reflash needed to swap sensors.

- [x] Confirm part number. USN-HS06PE (K=38) and USN-HS06PS (K=200).
- [x] Replace `sensor_flow_stub.{h,cpp}` with `sensor_flow.{h,cpp}`.
  - Interrupt-driven pulse counter on GPIO4 (flow #1) and GPIO5 (flow #2).
  - Load K factor from `node.json`; default 38 if absent.
  - Expose: `sensorFlow_getRateLpm(uint8_t ch)`, `sensorFlow_getTotalL(uint8_t ch)`, plus today/week/month/year subtotals and `sensorFlow_getKFactor(uint8_t ch)`.
  - Two-layer persistence: NVS (Preferences) every 10 s, SD `/config/flow_total.json` every 60 s.
- [x] HA discovery: `flow_rate` (`measurement`, `L/min`), `flow_total` (`total_increasing`, `L`), today/week/month/year (`measurement`, `L`), and K factor (diagnostic) per channel.
- [x] Add all flow and K-factor fields to heartbeat JSON.
- [x] Time-series CSV log: `/data/YYYY-MM-DD.csv` written every 60 s with all current sensor readings.
- [x] Create `docs/MQTT_TOPIC_MAP.md` with all current topics.
- [x] Update `docs/PIN_ALLOCATION.md` — mark GPIO4/5 confirmed for flow sensors.

---

## M2 — Pressure sensor

No DS18B20 — temperature comes from YiErYi 3788 RS485 sensor (M5). Pressure sensor not yet purchased.

- [ ] Confirm pressure transducer model + PSI range with user before ordering.
- [ ] Pressure: averaged ADC read on GPIO7. Voltage divider 2:1 (0–5V → 0–2.5V).
- [ ] HA discovery: `pressure` (measurement, kPa or PSI — confirm unit with user).
- [ ] Add `pressure` to heartbeat JSON and `docs/COMPONENTS.md`.

---

## M3 — Actuator command channel

Actuator not yet purchased — solenoid valve or motorised ball valve TBD.

- [ ] Confirm actuator type (solenoid valve or motorised ball valve) and purchase.
- [ ] Document driver circuit in `docs/ACTUATOR_DRIVER.md` once type confirmed.
- [ ] Replace `actuator_solenoid_stub.{h,cpp}`.
- [ ] MQTT command: parse `{"actuator":"open"|"close"}` on `twwp/<id>/cmd`.
- [ ] Safety: auto-close after N minutes if no heartbeat/confirmation, configurable in `node.json`.
- [ ] Update `docs/COMPONENTS.md` with actuator model + URL.

---

## M4 — OTA over MQTT

- [ ] Decide: `ArduinoOTA` (LAN) vs MQTT-driven OTA (internet).
- [ ] If MQTT-driven: subscribe `twwp/<id>/ota` for URL, fetch with `HTTPClient`, write with `Update.h`.
- [ ] Rollback: if boot crashes within 60s, `esp_ota_set_boot_partition` to known-good partition.

---

## M5 — YiErYi 3788 RS485

Blocked on hardware debug. When unblocked:

- [ ] Use ESPHome to confirm Modbus register addresses and baud rate.
- [ ] Read `references/Modbus Communication Data Format-V1.01.xlsx` to confirm slave address, baud, register map.
- [ ] Add `sensor_yieryi.{h,cpp}` using UART1 (`UART_MODE_RS485_HALF_DUPLEX`, GPIO17/18, GPIO21 auto DE/RE).
- [ ] HA discovery: pH, ORP, EC, TDS, CF, water temp, RH.
- [ ] Staleness watchdog: no successful read in 60s → mark unavailable.

---

## M6 — HealthService + CalibrationService

- [ ] `src/services/HealthService.{h,cpp}`: validates `SensorData`, sets `flow_ok`, `pressure_ok`, `power_ok`.
- [ ] `src/services/CalibrationService.{h,cpp}`: loads from `node.json` calibration block.
- [ ] Extend `SensorData` struct — apply calibration in drivers, never raw values in MQTT payload.
- [ ] Document field calibration in `docs/CALIBRATION.md`.

---

## M7 — AlertService + TelemetryService

- [ ] `src/services/AlertService.{h,cpp}`: fires on state change only. Types: LEAK_DETECTED, FLOW_ANOMALY, PRESSURE_OUT_OF_RANGE, LOW_VOLTAGE, SENSOR_FAILURE, DEVICE_REBOOT.
- [ ] `src/services/TelemetryService.{h,cpp}`: sends snapshot to `twwp/<id>/status` every 10s.
- [ ] Main loop order: `healthService → alertService → ruleEngine → telemetryService`.

---

## M8 — Device lifecycle

- [ ] First-connect registration: publish `{device_id, firmware_version, mac, ip}` to `twwp/register`.
- [ ] Decommission command: `{"action":"decommission"}` → wipe NVS, reboot to captive portal.
- [ ] MQTT rate-limit guard: > 60 publishes/min → back off + warn.
- [ ] Document credential rotation in `docs/DEVICE_LIFECYCLE.md`.
