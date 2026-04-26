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
| `store_sd.{h,cpp}` | SD CSV daily logs + FIFO ring-buffer queue |
| `watchdog.{h,cpp}` | Hardware WDT + crash log |
| `status_led.{h,cpp}` | WS2812 RGB via FastLED |
| `sensor_leak.{h,cpp}` | MH-RD digital/analog |
| `sensor_flow.{h,cpp}` | Hall pulse counter + K-factor + daily total |
| `sensor_pressure.{h,cpp}` | Analog ADC + voltage divider |
| `sensor_temp.{h,cpp}` | DS18B20 1-Wire |
| `sensor_yieryi.{h,cpp}` | YiErYi 3788 Modbus RTU (M5) |
| `actuator_solenoid.{h,cpp}` | N-MOSFET gate driver (M3) |

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
│   ├── YYYY-MM-DD.csv     ← daily sensor log, rotated at RTC midnight
│   └── crashes.txt        ← watchdog + exception crash log
├── buf/
│   └── <seq>.json         ← unsent MQTT messages, drained FIFO on reconnect
└── config/
    └── node.json          ← K-factor, thresholds, calibration, sensor intervals
```

`node.json` example:
```json
{
  "node_id": "wh_001",
  "sensor_interval_ms": 5000,
  "sd": {
    "serial_commands_enabled": true,
    "retention_days": 365,
    "auto_prune": false
  },
  "flow": { "k_factor": 450 },
  "pressure": { "offset": 0.0, "scale": 1.0 },
  "calibration": {
    "flow1": { "pulses_per_liter": 450 },
    "pressure": { "offset": 0.2, "scale": 1.1 }
  },
  "health_thresholds": {
    "min_voltage": 4.5,
    "max_flow": 50.0,
    "min_pressure": 0.0,
    "max_pressure": 700.0
  }
}
```

---

## MQTT topics

| Topic | Direction | Content |
|---|---|---|
| `twwp/<id>/status` | node → broker | Telemetry snapshot (10s) |
| `twwp/<id>/alert` | node → broker | Alert state changes |
| `twwp/<id>/log` | node → broker | SD errors, warnings |
| `twwp/<id>/lwt` | node → broker | Last will: `offline` |
| `twwp/<id>/cmd` | broker → node | solenoid, calibration, decommission, OTA |
| `twwp/register` | node → broker | First-connect registration |
| `homeassistant/...` | node → broker | MQTT discovery configs |

All topics: port 8883 TLS only.

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
| Power cut mid-write | Sequential filenames — partial writes skipped on replay. |
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
