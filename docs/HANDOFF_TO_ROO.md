# Handoff — Roo Code / Claude Code agent task list

This is a discrete, ordered task queue. Each task names files to read, files to edit/create, and a definition of done. Pick up at the next `[ ]` you find.

## How to use this

1. Open the `twwp-node/` folder as the workspace.
2. Read `README.md` → `docs/PIN_ALLOCATION.md` → `docs/MQTT_TOPIC_MAP.md` → `docs/FIRMWARE_ARCHITECTURE.md`.
3. Work tasks **in order**. Tick `[ ]` → `[x]` as you complete each one. Do not jump ahead.
4. Before any pin or library change, re-read `docs/PIN_ALLOCATION.md` and update it in the same commit.
5. The Waveshare demo ZIP (`ESP32-S3-RS485-CAN-Demo.zip`) contains board-specific driver examples — refer to it for RS485 and I2C patterns before writing new drivers.

## Locked decisions

- **Stack:** PlatformIO + Arduino framework
- **WiFi provisioning:** WiFiManager captive portal — first boot opens `TWWP-Setup-XXXX` AP
- **MQTT transport:** TLS only, port 8883, `WiFiClientSecure` + CA cert. Never plain 1883.
- **MQTT broker:** DNS name (e.g. `mqtt.twwp.nz`) — never a hardcoded IP
- **Per-device credentials:** unique `client_id`, username, password per node — stored in `secrets.h`
- **HA integration:** MQTT auto-discovery — no manual YAML
- **Time:** DS3231 external combo module over I²C (GPIO9/GPIO3). NTP syncs RTC when online; RTC holds time offline. NTP wins on conflict; drift > 2s → log correction.
- **Storage:** microSD on SPI (GPIO11/12/13/14). Daily CSV rotation. Ring-buffer queue for offline MQTT.
- **RS485:** Native `UART_MODE_RS485_HALF_DUPLEX` on UART1 (GPIO17/18). DE/RE auto-handled by hardware via GPIO21. No manual DE/RE toggling.
- **FreeRTOS tasks:** Pin all tasks to core 0 (matches Waveshare pattern). No blocking calls > 10s without `watchdog_feed()`.
- **PSRAM:** Use `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` for large buffers (RS485, MQTT queue).
- **ArduinoJson:** v7.x (Waveshare bundles 7.2.1 — breaking changes from v6, use v7 API).
- **ESPHome role:** Use ESPHome for sensor prototyping and Modbus register validation only (quick bring-up, confirm wiring). Production firmware is PlatformIO. See §ESPHome notes below.
- **Pin map:** per `docs/PIN_ALLOCATION.md` and the labeled pinout image (`03_waveshare_pinout_labeled.png`). The image is the visual reference; the MD is the detailed spec.

## ESPHome notes (prototyping use)

ESPHome is useful for:
- Quickly validating new sensor wiring before writing C++ drivers
- Confirming Modbus register addresses on the YiErYi 3788 (M5) before implementing in PlatformIO
- Testing DS18B20 1-Wire addresses at boot

ESPHome is not used for production firmware — it lacks the offline buffering, custom MQTT retry logic, and FreeRTOS task structure this project requires.

---

## M0 — bring-up

### M0.1 — User-side prep
- [ ] Copy `config_examples/secrets.h.sample` → `include/secrets.h`. Fill in `MQTT_HOST`, `MQTT_PORT` (8883), `MQTT_USER`, `MQTT_PASS`, `MQTT_CA_CERT`, `NODE_ID`.
- [ ] Format microSD card FAT32. Insert with CR2032 in DS3231.
- [ ] Wire per `docs/WIRING_M0.md`.
- [ ] `pio run` — compiles clean.
- [ ] `pio run -t upload` + `pio device monitor`.
- [ ] Verify serial output matches `WIRING_M0.md` "what to look for" section.
- [ ] Drip water on leak probe → HA `binary_sensor` flips + entry in `/log/YYYY-MM-DD.csv`.

### M0.2 — Offline buffering
- [ ] Block MQTT (kill broker or pull WiFi).
- [ ] Trigger 5–10 leak transitions.
- [ ] Confirm `/buf/` accumulates files.
- [ ] Restore broker → files drain in order, HA shows historical transitions.

### M0.3 — Polish
- [ ] **Buffer overflow cap.** `storeSd_bufferMessage()` has no cap. If `s_seq - oldestSeq > SD_MAX_BUFFER_LINES`, delete oldest before writing, append warning to `/log/crashes.txt`.
- [ ] **SD-failure surfacing.** On write failure in `store_sd.cpp`, publish `"sd write failed"` to `twwp/<id>/log` via MQTT (rate-limited 1/min).
- [ ] **Reset-creds gesture.** `digitalRead(0) == LOW` held > 5s in `loop()` → `netWifi_resetCredentials()`. GPIO0 is strapping pin — use only for this.
- [ ] **Heartbeat enrichment.** Add `wifi_ssid`, `ip`, `mqtt_buffer_count` to heartbeat JSON.
- [ ] **HA device availability.** Single `device_availability` block on all discovery payloads — DRY.

---

## M0.5 — TLS + security hardening

> Do this before M1. Public MQTT without TLS is unacceptable.

### Server-side (user does manually)
- [ ] Let's Encrypt cert exists for `mqtt.twwp.nz`. If not: `certbot certonly --standalone -d mqtt.twwp.nz`.
- [ ] `mosquitto.conf`: `listener 8883`, cert/key/cafile paths, `allow_anonymous false`, `password_file`.
- [ ] `ufw deny 1883/tcp`. `ufw allow 8883/tcp`.
- [ ] Add per-device passwords: `mosquitto_passwd -b /mosquitto/config/passwordfile twwp_wh_001 <pass>`.
- [ ] Verify: `mosquitto_pub -h mqtt.twwp.nz -p 8883 --cafile ca.crt -u twwp_wh_001 -P <pass> -t test -m hello`.

### Firmware-side (agent does)
- [ ] `secrets.h.sample`: add `MQTT_PORT 8883` and `MQTT_CA_CERT` (raw string PEM literal).
- [ ] `net_mqtt.cpp`: replace `WiFiClient` with `WiFiClientSecure`. Call `client.setCACert(MQTT_CA_CERT)` before connect. Never `client.setInsecure()`.
- [ ] `config.h`: `#define MQTT_CLIENT_ID "twwp_" NODE_ID`.
- [ ] On TLS handshake failure: log SSL error code to serial + SD crashes log. Retry with backoff. Never fall back to plain MQTT.
- [ ] Update `docs/MQTT_TOPIC_MAP.md` — note port 8883 only.

---

## M1 — Hall flow sensor

- [ ] Confirm part number with user (YF-S201? K-factor depends on part).
- [ ] Update `docs/PIN_ALLOCATION.md` — commit GPIO4 for flow #1.
- [ ] Replace `sensor_flow_stub.{h,cpp}` with `sensor_flow.{h,cpp}`:
  - `pinMode(PIN_FLOW_1, INPUT_PULLUP)`
  - `attachInterruptArg(digitalPinToInterrupt(PIN_FLOW_1), &isrPulse, &ctx, FALLING)`
  - ISR: increment `volatile uint32_t pulses`
  - Loop: `lpm = (pulses_this_window * 60.0 / window_s) / k_factor`
  - K-factor from `/config/node.json`
  - Daily reset at RTC midnight; persist daily total to SD before reset
- [ ] HA discovery: `flow_rate` (`measurement`, `L/min`) and `flow_total` (`total_increasing`, `L`).
- [ ] Update `docs/MQTT_TOPIC_MAP.md` and `docs/HA_DISCOVERY.md`.

---

## M2 — Pressure + DS18B20

- [ ] Confirm pressure transducer model with user (need PSI range and output voltage).
- [ ] Pressure: averaged ADC read on GPIO7. Voltage divider 2:1 (0–5V → 0–2.5V). Calibration: zero offset + span scalar in `node.json`.
- [ ] DS18B20: `OneWire` + `DallasTemperature`. Auto-discover ROMs at boot, expose by index.
- [ ] HA discovery: `pressure` and `temperature` per probe.

---

## M3 — Solenoid command channel

- [ ] Confirm image 7 device with user — solenoid or flow switch.
- [ ] If solenoid: N-MOSFET driver (IRLZ44N + 1N4007 flyback). Document in `docs/SOLENOID_DRIVER.md`.
- [ ] Replace `actuator_solenoid_stub.{h,cpp}`.
- [ ] `net_mqtt.cpp::onMessage`: parse `{"solenoid":"open"|"close"}`.
- [ ] HA discovery: `switch.<id>_solenoid` with `command_topic`.
- [ ] Safety: auto-close after N minutes if no confirmation, configurable in `node.json`.

---

## M4 — OTA over MQTT

- [ ] Decide: `ArduinoOTA` (LAN) vs MQTT-driven OTA (internet).
- [ ] If MQTT-driven: subscribe `twwp/<id>/ota` for URL, fetch with `HTTPClient`, write with `Update.h`.
- [ ] Rollback: if boot crashes within 60s, `esp_ota_set_boot_partition` to known-good partition.

---

## M5 — YiErYi 3788 RS485

Blocked on hardware debug. When unblocked:

- [ ] Optionally use ESPHome first to confirm Modbus register addresses and baud rate.
- [ ] Read `references/Modbus Communication Data Format-V1.01.xlsx` to confirm: slave address, baud, 4-register read at `0x0000`.
- [ ] Add `sensor_yieryi.{h,cpp}` using UART1 (`UART_MODE_RS485_HALF_DUPLEX`, GPIO17/18, GPIO21 auto DE/RE).
- [ ] Use a Modbus RTU library for protocol only (physical layer handled by hardware).
- [ ] HA discovery: pH, ORP, EC, TDS, CF, water temp, RH.
- [ ] Staleness watchdog: no successful read in 60s → mark unavailable.

---

## M6 — HealthService + CalibrationService

- [ ] `src/services/HealthService.{h,cpp}`: validates `SensorData`, sets `flow_ok`, `pressure_ok`, `power_ok`. Thresholds from `node.json`.
- [ ] `src/services/CalibrationService.{h,cpp}`: loads from `node.json` `calibration` block. Provides `getFlowFactor()`, `getPressureOffset()`, `getPressureScale()`. Accepts remote update via MQTT command.
- [ ] Extend `SensorData` struct with `bool flow_ok`, `bool pressure_ok`, `bool power_ok`.
- [ ] Apply calibration in sensor drivers — never raw values in MQTT payload.
- [ ] Document field calibration procedure in `docs/CALIBRATION.md`.

---

## M7 — AlertService + TelemetryService

- [ ] `src/services/AlertService.{h,cpp}`: fires on state change only (anti-spam). Types: `LEAK_DETECTED`, `FLOW_ANOMALY`, `PRESSURE_OUT_OF_RANGE`, `LOW_VOLTAGE`, `SENSOR_FAILURE`, `DEVICE_REBOOT`. Publishes to `twwp/<id>/alert`.
- [ ] `src/services/TelemetryService.{h,cpp}`: sends snapshot to `twwp/<id>/status` every 10s. Includes sensors, alert flags, `wifi_rssi`, `uptime`, `sd_free_kb`.
- [ ] Main loop order: `healthService → alertService → ruleEngine → telemetryService`.
- [ ] Update `docs/MQTT_TOPIC_MAP.md`.

---

## M8 — Device lifecycle

- [ ] First-connect registration: publish `{device_id, firmware_version, mac, ip}` to `twwp/register`.
- [ ] Decommission command: `{"action":"decommission"}` → wipe NVS, reboot to captive portal.
- [ ] MQTT rate-limit guard: > 60 publishes/min → back off + warn.
- [ ] Document credential rotation in `docs/DEVICE_LIFECYCLE.md`.

---

## Standing rules

- Never hardcode pin numbers outside `include/pins.h`.
- Never commit `include/secrets.h`.
- Each new MQTT topic → row in `docs/MQTT_TOPIC_MAP.md` in the same commit.
- Each new HA entity → entry in `docs/HA_DISCOVERY.md`.
- Each new GPIO → row in `docs/PIN_ALLOCATION.md` + updated `pins.h`.
- New sensors follow `sensor_leak` pattern: `_begin/_loop/_read*` interface, all MQTT via `netMqtt_publishSub()`, all events via `storeSd_logEvent()`.
- All FreeRTOS tasks pinned to core 0.
- Large buffers allocated from PSRAM: `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`.
- ArduinoJson: use v7 API (`JsonDocument` not `DynamicJsonDocument`).
- Never `client.setInsecure()` for MQTT.
