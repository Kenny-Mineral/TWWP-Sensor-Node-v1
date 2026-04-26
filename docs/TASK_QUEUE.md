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
- [ ] **Buffer overflow cap.** `storeSd_bufferMessage()` has no cap. If `s_seq - oldestSeq > SD_MAX_BUFFER_LINES`, delete oldest before writing, append warning to `/log/crashes.txt`.
- [ ] **SD-failure surfacing.** On write failure in `store_sd.cpp`, publish `"sd write failed"` to `twwp/<id>/log` via MQTT (rate-limited 1/min).
- [ ] **Reset-creds gesture.** `digitalRead(0) == LOW` held > 5s in `loop()` → `netWifi_resetCredentials()`.
- [ ] **Heartbeat enrichment.** Add `wifi_ssid`, `ip`, `mqtt_buffer_count` to heartbeat JSON.
- [ ] **HA device availability.** Single `device_availability` block on all discovery payloads — DRY.

---

## M0.5 — TLS + security hardening

> Do this before M1. Public MQTT without TLS is unacceptable.

### Server-side (user does manually)
- [ ] Let's Encrypt cert exists for `twwp-iot.duckdns.org`. If not: `certbot certonly --standalone -d twwp-iot.duckdns.org`.
- [ ] `mosquitto.conf`: `listener 8883`, cert/key/cafile paths, `allow_anonymous false`, `password_file`.
- [ ] `ufw deny 1883/tcp`. `ufw allow 8883/tcp`.
- [ ] Add per-device passwords: `mosquitto_passwd -b /mosquitto/config/passwordfile twwp_wh_001 <pass>`.
- [ ] Verify: `mosquitto_pub -h twwp-iot.duckdns.org -p 8883 --cafile ca.crt -u twwp_wh_001 -P <pass> -t test -m hello`.

### Firmware-side (agent does)
- [ ] `secrets.h.sample`: add `MQTT_PORT 8883` and `MQTT_CA_CERT` (raw string PEM literal).
- [ ] `net_mqtt.cpp`: replace `WiFiClient` with `WiFiClientSecure`. Call `client.setCACert(MQTT_CA_CERT)` before connect.
- [ ] `config.h`: `#define MQTT_CLIENT_ID "twwp_" NODE_ID`.
- [ ] On TLS handshake failure: log SSL error code to serial + SD crashes log. Retry with backoff.
- [ ] Update `docs/MQTT_TOPIC_MAP.md` — note port 8883 only.

---

## M1 — Hall flow sensor

- [ ] Confirm part number with user (YF-S201? K-factor depends on part).
- [ ] Update `docs/PIN_ALLOCATION.md` — commit GPIO4 for flow #1.
- [ ] Replace `sensor_flow_stub.{h,cpp}` with `sensor_flow.{h,cpp}`.
- [ ] HA discovery: `flow_rate` (`measurement`, `L/min`) and `flow_total` (`total_increasing`, `L`).
- [ ] Update `docs/MQTT_TOPIC_MAP.md` and `docs/HA_DISCOVERY.md`.

---

## M2 — Pressure + DS18B20

- [ ] Confirm pressure transducer model with user (need PSI range and output voltage).
- [ ] Pressure: averaged ADC read on GPIO7. Voltage divider 2:1 (0–5V → 0–2.5V).
- [ ] DS18B20: `OneWire` + `DallasTemperature`. Auto-discover ROMs at boot, expose by index.
- [ ] HA discovery: `pressure` and `temperature` per probe.

---

## M3 — Solenoid command channel

- [ ] Confirm device with user — solenoid or flow switch.
- [ ] If solenoid: N-MOSFET driver (IRLZ44N + 1N4007 flyback). Document in `docs/SOLENOID_DRIVER.md`.
- [ ] Replace `actuator_solenoid_stub.{h,cpp}`.
- [ ] `net_mqtt.cpp::onMessage`: parse `{"solenoid":"open"|"close"}`.
- [ ] Safety: auto-close after N minutes if no confirmation, configurable in `node.json`.

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
