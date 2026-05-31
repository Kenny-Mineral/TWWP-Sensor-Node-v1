# Firmware Architecture — TWWP Node

> Reference document for all firmware development.
> Roo Code / Claude Code must read this before writing or reviewing any firmware file.

---

## Core design principles

| Principle | What it means |
|---|---|
| **Offline-first** | Node works 100% without internet. Never drop data because MQTT is down. |
| **Eventually consistent** | Queue on SD, drain when online. |
| **Fail-safe** | Sensors + solenoid work even if WiFi, MQTT, SD or RTC is down. |
| **Stateful** | Remembers everything across reboots. |
| **Self-healing** | Recovers from crashes, bad configs, network loss without user intervention. |
| **No blocking** | No `delay()` in main loop. No blocking > 10s without `watchdog_feed()`. |

---

## Layered architecture

```
┌─────────────────────────────┐
│         main.cpp            │  setup() + loop() only — no business logic
├─────────────────────────────┤
│       Services layer        │  HealthService, AlertService, TelemetryService, CalibrationService (M6+)
├─────────────────────────────┤
│       Drivers layer         │  net_wifi, net_mqtt, time_rtc, store_sd, sensor_*, actuator_*, watchdog
├─────────────────────────────┤
│      Hardware layer         │  include/pins.h — pin numbers only, nothing else
└─────────────────────────────┘
```

Data flows upward. Control flows downward. Layers never skip.

---

## Board hardware facts (from Waveshare demo source)

| Item | Detail |
|---|---|
| Module | ESP32-S3-WROOM-1, 16MB flash, 8MB OPI PSRAM |
| Onboard RTC | PCF85063 at I²C addr `0x51`, on **internal** GPIO38(SDA)/GPIO39(SCL) — not used by TWWP |
| External RTC | DS3231 at `0x68` on GPIO9(SDA)/GPIO3(SCL) — this is what TWWP uses |
| RS485 | UART1 GPIO17(TX)/GPIO18(RX)/GPIO21(TXD1EN). Uses `UART_MODE_RS485_HALF_DUPLEX` — hardware auto-toggles DE/RE. |
| CAN | TWAI peripheral GPIO15(TX)/GPIO16(RX) |
| GPIO47 | Reserved onboard — do not use |
| PSRAM | Allocate large buffers with `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` |
| FreeRTOS | Pin all tasks to core 0 with `xTaskCreatePinnedToCore(..., 0)` |
| USB serial | USB CDC — requires `-DARDUINO_USB_CDC_ON_BOOT=1` or Serial is silent |

---

## RS485 driver pattern

Based directly on Waveshare `WS_RS485.cpp`:

```cpp
// Init — call once in setup()
HardwareSerial rs485Serial(1);

void rs485_init(unsigned long baud) {
    rs485Serial.begin(baud, SERIAL_8N1, RXD1, TXD1);
    rs485Serial.setPins(-1, -1, -1, TXD1EN);          // GPIO21 auto DE/RE
    rs485Serial.setMode(UART_MODE_RS485_HALF_DUPLEX);  // hardware handles direction
}

// Send
void rs485_send(uint8_t* data, size_t len) {
    rs485Serial.write(data, len);
}

// Receive — run in a FreeRTOS task pinned to core 0
void rs485_task(void* param) {
    while (1) {
        if (rs485Serial.available()) {
            // read and process
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
```

No Modbus library is needed for the physical layer. For Modbus RTU protocol (YiErYi M5), a library handles framing on top of this.

---

## Driver inventory

| File | Purpose |
|---|---|
| `net_wifi.{h,cpp}` | WiFiManager + reconnect + credential reset |
| `net_mqtt.{h,cpp}` | MQTT/TLS client + HA discovery + SD offline buffer |
| `net_ota.{h,cpp}` | OTA update driver — MQTT-triggered HTTPS update with MD5 verification, rollback window, and ArduinoOTA servicing during idle state |
| `net_ap.{h,cpp}` | Concurrent STA+AP upload portal — WiFi AP broadcast, HTTP server (port 80), auto-trigger on WiFi loss/weak RSSI, MQTT `start_ap` command, SD token auth, OLED UPLOAD MODE screen, serial debug commands |
| `time_rtc.{h,cpp}` | DS3231 + NTP sync + drift correction |
| `store_sd.{h,cpp}` | SD event log + time-series data log + FIFO ring-buffer queue + JSON file helpers + buffer stats/fetch/ack for upload portal |
| `watchdog.{h,cpp}` | Hardware WDT + crash log |
| `status_led.{h,cpp}` | WS2812 RGB via FastLED |
| `display_oled.{h,cpp}` | SSD1306 128×64 I²C OLED — 6-frame sliding carousel (WQ summary, remin detail, flow/waste, tank level, sys health, branding), sticky header, UPLOAD MODE screen when AP active |
| `rs485_mux.{h,cpp}` | RS485 byte classifier — routes `$WM` ASCII frames (TDS meter) and Modbus RTU frames (YiErYi) on the shared UART1 bus; 64-byte Modbus FIFO, ASCII accumulator, 200 ms timeout guard |
| `sensor_leak.{h,cpp}` | MH-RD digital — LOW = wet, logs event on state change |
| `sensor_flow.{h,cpp}` | Hall pulse counter on GPIO4/5 — ISR debounce, low-flow cutoff, multi-point K-table with linear interpolation, moving-average smoothing, uint64_t raw pulse totals, K-factor from `node.json`, NVS+SD persistence, rate/total/today/week/month/year per channel, runtime-configurable debounce/window. Flow calibration wizard per channel (begin/commit/accept/abort). Sensor model registry (USN-HS06PE, USN-HS06PS, DWS-MH-02). |
| `session_flow.{h,cpp}` | Tap session lifecycle — IDLE/ACTIVE/ENDING state machine, configurable idle timeout (NVS, 5–100 s) and flow threshold (NVS, 0.01–0.5 L/min), 10-session ring buffer with SD persistence, retained `sessions_recent` MQTT publish, leak-suspect detection |
| `sensor_voltage.{h,cpp}` | ADS1115 I²C ADC (0x48) + 100kΩ/33kΩ divider — 12V battery voltage, %, charge state, NVS-persisted v_min/v_max/cal (M2.5) |
| `sensor_pressure.{h,cpp}` | Stub — analog ADC + voltage divider (M2, sensor not yet purchased) |
| `sensor_temp.{h,cpp}` | Stub — no DS18B20; temperature will come from YiErYi 3788 RS485 (M5) |
| `sensor_yieryi.{h,cpp}` | YiErYi 3178/3788 Modbus RTU via RS485 UART1 (M5) — non-blocking state machine, CRC validation, pH/ORP mode command, raw-frame diagnostics. Calibration date per zone stored in `node.json`, settable via MQTT. |
| `sensor_tds_meter.{h,cpp}` | Standalone ESP32+ADS1115 EC/TDS meter (M6) — parses `$WM` ASCII frames from shared RS485 bus, dual-probe (PRE_RO / POST_RO), 60 s staleness watchdog, EC correction factor wizard (NVS persisted). |
| `actuator_valve.{h,cpp}` | Active-low relay driver — auto-opens on sensor 1 flow via `FLOW_ACTIVE_THRESHOLD_LPM`; manual override via MQTT `valve_open`/`valve_auto` cmd keys |

---

## Services layer (M6+)

| Service | Purpose |
|---|---|
| `HealthService` | Validates sensor readings against thresholds from `node.json`. Sets `flow_ok`, `pressure_ok`, `power_ok` on `SensorData`. |
| `CalibrationService` | Loads calibration factors from `node.json`. Accepts remote update via MQTT. |
| `AlertService` | Fires on state change only (anti-spam). Publishes to `twwp/<id>/alert`. |
| `TelemetryService` | Sends full status snapshot to `twwp/<id>/status` every 10s. |

---

## SensorData model

This is the core data contract. Only add fields — never remove or rename.

```cpp
struct SensorData {
    uint32_t  timestamp;
    float     flow1;           // L/min, calibrated
    float     flow_total;      // L, daily total
    uint64_t  pulses_raw_1;    // lifetime raw pulses, sensor 1 (never reset)
    uint64_t  pulses_raw_2;    // lifetime raw pulses, sensor 2 (never reset)
    float     pressure;        // kPa, calibrated
    float     temperature;     // °C
    float     supply_voltage;  // V
    bool      leak;
    bool      flow_ok;         // set by HealthService
    bool      pressure_ok;
    bool      power_ok;
    // M5 — RS485-3177 × 3 zones (pre-RO filter, post-RO filter, remineralised)
    // Field names are locked in — HA entities, InfluxDB schema, and Grafana are already configured for these.
    float     wq_pre_ro_ph;    float wq_pre_ro_orp;   float wq_pre_ro_ec;   float wq_pre_ro_temp;
    float     wq_post_ro_ph;   float wq_post_ro_orp;  float wq_post_ro_ec;  float wq_post_ro_temp;
    float     wq_remin_ph;     float wq_remin_orp;    float wq_remin_ec;    float wq_remin_temp;
};
```

> **Note:** The raw pulse fields (`pulses_raw_1`/`pulses_raw_2`) are accumulated from ISR-driven counters and persisted as `uint64_t` to NVS (`p1`/`p2` keys) and SD (`/config/flow_total.json`). This enables post-hoc recalibration: volume = total_pulses / K, with zero rounding error. See [`src/sensor_flow.cpp`](src/sensor_flow.cpp:38).

---

## Main loop (M7+ target)

```cpp
void loop() {
    watchdog_feed();

    SensorData data = readAllSensors();

    healthService.evaluate(data);
    alertService.evaluate(data);
    ruleEngine.evaluate(data);

    if (alertService.hasNewAlert())
        mqtt.publishAlert(alertService.getState());

    telemetryService.sendStatus(data, alertService.getState());

    storeSd_logEvent(data);
    mqtt.drainBuffer();
}
```

---

## SD card layout

```
/
├── log/
│   ├── YYYY-MM-DD.csv     ← event log: leak state changes, boot events, warnings
│   ├── sessions.csv       ← per-session log: id, start_ts, end_ts, dur, vol, peak
│   ├── cal_sessions.csv   ← calibration events: ts, type, channel_or_zone, old_value, new_value, ref_value, duration_s
│   └── crashes.txt        ← watchdog resets + buffer overflow records
├── data/
│   └── YYYY-MM-DD.csv     ← time-series: all sensor readings, one row per 60 s
├── buf/
│   └── 0000000001.json    ← unsent MQTT messages, drained FIFO on reconnect
└── config/
    ├── node.json              ← K-factor, SD retention, calibration, thresholds, AP config
    ├── flow_total.json        ← persisted flow totals + subtotals + date (SD layer)
    ├── sessions_recent.json   ← last 10 sessions ring buffer snapshot (restored on boot)
    ├── upload_token.json      ← per-node HMAC token for relay auth: {"token":"<hex>"}
    └── upload.html            ← self-contained upload portal page served by AP HTTP server
```

`node.json` current schema:
```json
{
  "sd": {
    "serial_commands_enabled": true,
    "retention_days": 365,
    "auto_prune": false
  },
  "flow": {
    "k_factor_1": 5500,
    "k_factor_2": 20700,
    "k_table_1": [
      {"flow_lpm": 0.42, "k": 4972},
      {"flow_lpm": 0.99, "k": 5468},
      {"flow_lpm": 1.42, "k": 5476}
    ],
    "k_table_2": [
      {"flow_lpm": 0.42, "k": 21120},
      {"flow_lpm": 0.99, "k": 20818},
      {"flow_lpm": 1.38, "k": 21104}
    ],
    "debounce_us_1": 1000,
    "debounce_us_2": 500,
    "flow_avg_window": 5
  },
  "ap": {
    "ssid": "twwp-wh_001",
    "password": "",
    "duration_s": 300,
    "auto_on_wifi_loss": true,
    "auto_on_loss_delay_s": 60
  }
}
```

> K-tables are optional — if absent the firmware uses the matching single `k_factor_*` as a 1-point table (backward compatible). `debounce_us_*` and `flow_avg_window` are also optional and default to the compile-time values in [`include/config.h`](include/config.h).

`data/YYYY-MM-DD.csv` column header (current — columns added as sensors come online):
```
ts,flow_rate_1,flow_total_1,flow_today_1,flow_rate_2,flow_total_2,flow_today_2,leak,supply_voltage,wq_pre_ro_ph,wq_pre_ro_orp,wq_pre_ro_ec,wq_pre_ro_temp,wq_post_ro_ph,wq_post_ro_orp,wq_post_ro_ec,wq_post_ro_temp,wq_remin_ph,wq_remin_orp,wq_remin_ec,wq_remin_temp,tds_pre_ro_ec,tds_pre_ro_ppm,tds_post_ro_ec,tds_post_ro_ppm
```

---

## Data persistence layers

Every piece of state that must survive a reboot uses a layered strategy. The rule is: **write locally first, publish to MQTT second. Never rely on MQTT being available to preserve data.**

| Layer | What | How often | Survives |
|---|---|---|---|
| RAM | All live sensor values, rates, subtotals, ring buffers | Continuous | Normal operation only — lost on power cut |
| NVS (ESP32 flash) | `flow_total_1`, `flow_total_2` (float), `totalPulses1`, `totalPulses2` (uint64_t via `p1`/`p2` keys) | Every 10 s if changed | Power loss — at most 10 s gap |
| SD `/config/flow_total.json` | Flow totals + uint64_t pulse totals (`p1`/`p2`) + period subtotals + current date | Every 60 s if changed | Full power loss including NVS corruption; also carries subtotals NVS cannot |
| SD `/data/YYYY-MM-DD.csv` | All sensor readings snapshot | Every 60 s | Permanent offline record for time-series analysis |
| SD `/log/YYYY-MM-DD.csv` | State-change events (leak, reboot, errors) | On event | Permanent audit trail |
| SD `/buf/<seq>.json` | MQTT messages that failed to send | On every publish attempt | Replayed FIFO to broker when connectivity returns |

**Boot restore order for flow totals:**
1. Load SD `flow_total.json` — gives float totals (`t1`/`t2`), uint64_t pulse totals (`p1`/`p2`), subtotals (`today1`/`today2`/`week1`/`week2`/`month1`/`month2`/`year1`/`year2`), and saved date
2. Overlay NVS totals if larger — NVS saves more frequently so is likely more recent. NVS also carries uint64_t pulse totals (`p1`/`p2` keys in Preferences "flow" namespace)
3. Subtotals are restored only if the saved date matches today; otherwise reset to zero
4. Lifetime volume is computed as `totalPulses / interpolatedK(smoothedFlowRate)` — raw pulses are the authoritative volume source

**Adding a new sensor** — follow this pattern:
- Store live value in RAM (driver static)
- Persist critical accumulators to NVS (Preferences namespace per driver)
- Persist full state to SD config JSON every 60 s
- Append reading to `/data/` CSV on each 60 s data log tick (add column to header)
- Publish via MQTT heartbeat and HA discovery on connect

---

## MQTT topics

Full topic list with QoS, retain flags, and HA discovery topics: see `docs/MQTT_TOPIC_MAP.md`.

Summary:

| Topic | Direction | Content |
|---|---|---|
| `twwp/<id>/status` | node → broker | All sensor readings + network info, every 10 s, retained |
| `twwp/<id>/alert` | node → broker | State-change events (leak, etc.) |
| `twwp/<id>/log` | node → broker | SD write failures, rate-limited 1/min |
| `twwp/<id>/lwt` | node → broker | `online` / `offline`, retained |
| `twwp/<id>/session` | node → broker | Session-end event (not retained) — id, start/end ts, duration, volume, peak |
| `twwp/<id>/sessions_recent` | node → broker | Retained JSON of last 10 sessions (newest-first). Republished on reconnect. |
| `twwp/<id>/cal_session` | node → broker | Calibration event on wizard accept. type, old/new value, ref_value, duration_s. |
| `twwp/<id>/cmd` | broker → node | Command channel (actuator M3, OTA M4, session config, upload portal control) |
| `twwp/<id>/ota_state` | node → broker | Dedicated retained OTA progress/status — state enum + progress %, for live dashboards |
| `twwp/<id>/wq_config` | node → broker | Retained WQ threshold/label/name config, published on connect and after any cmd change |
| `twwp/register` | node → broker | First-connect registration (M8) |
| `homeassistant/...` | node → broker | HA auto-discovery configs, retained |

All topics: port 8883 TLS only. Never plain 1883.

---

## Security

| Requirement | Implementation |
|---|---|
| Encrypted transport | `WiFiClientSecure` + Let's Encrypt CA cert, port 8883 |
| Authentication | Per-device Mosquitto username + password |
| Anonymous access | Blocked — `allow_anonymous false` |
| CA cert pinning | `client.setCACert(MQTT_CA_CERT)` — never `setInsecure()` |
| Broker hardening | `ufw deny 1883`, Fail2ban on SSH |

---

## Failure behaviour

| Failure | Behaviour |
|---|---|
| WiFi drops | Keep logging to SD. Retry with backoff. RTC keeps time on coin-cell. |
| MQTT unreachable | Buffer to SD `/buf/`. Drain FIFO on reconnect. |
| SD fails | Set flag. Publish `"sd write failed"` to `twwp/<id>/log` (rate-limited). Continue degraded. |
| Sensor garbage | HealthService marks `_ok = false`. AlertService fires `SENSOR_FAILURE`. |
| Power cut mid-write | Sequential filenames — partial writes skipped on replay. Flow totals recovered from NVS (≤10 s gap) then SD backup. |
| Loop stall | Hardware watchdog reboots after 30s. Reason written to `/log/crashes.txt`. |
| TLS failure | Log SSL error code. Retry with backoff. Never fall back to plain MQTT. |
| Bad OTA | If boot crashes within 60s → rollback via `esp_ota_set_boot_partition`. |

---

## ESPHome (prototyping only — not production)

ESPHome is useful for:
- Quickly validating new sensor wiring before writing C++ drivers
- Confirming Modbus register addresses on the YiErYi 3788 (M5) before implementing in PlatformIO
- Testing DS18B20 1-Wire addresses at boot

ESPHome is not used for production firmware — it lacks offline buffering, custom MQTT retry logic, and the FreeRTOS task structure this project requires.
