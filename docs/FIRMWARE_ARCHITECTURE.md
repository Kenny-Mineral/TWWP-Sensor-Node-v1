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
| `time_rtc.{h,cpp}` | DS3231 + NTP sync + drift correction |
| `store_sd.{h,cpp}` | SD event log + time-series data log + FIFO ring-buffer queue + JSON file helpers |
| `watchdog.{h,cpp}` | Hardware WDT + crash log |
| `status_led.{h,cpp}` | WS2812 RGB via FastLED |
| `sensor_leak.{h,cpp}` | MH-RD digital — LOW = wet, logs event on state change |
| `sensor_flow.{h,cpp}` | Hall pulse counter on GPIO4/5 — K-factor from `node.json`, NVS+SD persistence, rate/total/today/week/month/year per channel |
| `session_flow.{h,cpp}` | Tap session lifecycle — IDLE/ACTIVE/ENDING state machine, configurable idle timeout (NVS, 5–100 s) and flow threshold (NVS, 0.01–0.5 L/min), 10-session ring buffer with SD persistence, retained `sessions_recent` MQTT publish, leak-suspect detection |
| `sensor_pressure.{h,cpp}` | Stub — analog ADC + voltage divider (M2, sensor not yet purchased) |
| `sensor_temp.{h,cpp}` | Stub — no DS18B20; temperature will come from YiErYi 3788 RS485 (M5) |
| `sensor_yieryi.{h,cpp}` | Stub — YiErYi 3788 Modbus RTU via RS485 UART1 (M5, blocked on hardware debug) |
| `actuator_solenoid.{h,cpp}` | Stub — N-MOSFET gate driver (M3, actuator not yet purchased) |

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
    float     pressure;        // kPa, calibrated
    float     temperature;     // °C
    float     supply_voltage;  // V
    bool      leak;
    bool      flow_ok;         // set by HealthService
    bool      pressure_ok;
    bool      power_ok;
    // M5 — YiErYi 3788
    float     ph;
    float     orp;
    float     ec;
    float     tds;
    float     water_temp;
};
```

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
│   └── crashes.txt        ← watchdog resets + buffer overflow records
├── data/
│   └── YYYY-MM-DD.csv     ← time-series: all sensor readings, one row per 60 s
├── buf/
│   └── 0000000001.json    ← unsent MQTT messages, drained FIFO on reconnect
└── config/
    ├── node.json              ← K-factor, SD retention, calibration, thresholds
    ├── flow_total.json        ← persisted flow totals + subtotals + date (SD layer)
    └── sessions_recent.json   ← last 10 sessions ring buffer snapshot (restored on boot)
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
    "k_factor_1": 38,
    "k_factor_2": 38
  }
}
```

`data/YYYY-MM-DD.csv` column header (current — columns added as sensors come online):
```
ts,flow_rate_1,flow_total_1,flow_today_1,flow_rate_2,flow_total_2,flow_today_2,leak
```

---

## Data persistence layers

Every piece of state that must survive a reboot uses a layered strategy. The rule is: **write locally first, publish to MQTT second. Never rely on MQTT being available to preserve data.**

| Layer | What | How often | Survives |
|---|---|---|---|
| RAM | All live sensor values, rates, subtotals | Continuous | Normal operation only — lost on power cut |
| NVS (ESP32 flash) | `flow_total_1`, `flow_total_2` | Every 10 s if changed | Power loss — at most 10 s gap |
| SD `/config/flow_total.json` | Flow totals + period subtotals + current date | Every 60 s if changed | Full power loss including NVS corruption; also carries subtotals NVS cannot |
| SD `/data/YYYY-MM-DD.csv` | All sensor readings snapshot | Every 60 s | Permanent offline record for time-series analysis |
| SD `/log/YYYY-MM-DD.csv` | State-change events (leak, reboot, errors) | On event | Permanent audit trail |
| SD `/buf/<seq>.json` | MQTT messages that failed to send | On every publish attempt | Replayed FIFO to broker when connectivity returns |

**Boot restore order for flow totals:**
1. Load SD `flow_total.json` — gives totals, subtotals, and date
2. Overlay NVS totals if larger — NVS saves more frequently so is likely more recent
3. Subtotals are restored only if the saved date matches today; otherwise reset to zero

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
| `twwp/<id>/cmd` | broker → node | Command channel (actuator M3, OTA M4, session config) |
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
