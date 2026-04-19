# TWWP Node — Waveshare ESP32-S3-RS485-CAN

> Firmware for a single TWWP (The Wholey Water Project) collection node.
> Phase 1, Milestone 0 — Leak detector + RTC/SD logging + MQTT/TLS + Home Assistant auto-discovery.

## What this folder is

A PlatformIO project for VS Code. The Waveshare demo ZIP (`ESP32-S3-RS485-CAN-Demo.zip`) contains board-specific driver examples — refer to it for RS485 and I²C patterns. Stub sensors are placeholders so firmware compiles end-to-end before all hardware is wired.

## Hardware

| Item | Status | Notes |
|---|---|---|
| Waveshare ESP32-S3-RS485-CAN | active | ESP32-S3-WROOM-1, 16MB flash, 8MB OPI PSRAM |
| MH-RD leak detector | **active M0** | |
| DS3231 + microSD combo module | **active M0** | External module — not the onboard PCF85063 |
| Hall flow sensor (3/8" push-fit) | stub → M1 | |
| Pressure sensor (analog) | stub → M2 | |
| DS18B20 temp probe | stub → M2 | |
| 12V solenoid | stub → M3 | |
| YiErYi 3788 RS485 water quality | blocked → M5 | Pending hardware debug |

## Locked decisions

- **Stack:** PlatformIO + Arduino framework
- **Transport:** MQTT/TLS port 8883 (`WiFiClientSecure` + CA cert). Plain 1883 blocked at server.
- **WiFi provisioning:** WiFiManager captive portal — `TWWP-Setup-XXXX` AP on first boot
- **MQTT broker:** DNS name, never hardcoded IP
- **Per-device credentials:** unique client_id, username, password per node
- **HA integration:** MQTT auto-discovery
- **Time:** External DS3231 combo module (GPIO9/GPIO3). NTP syncs RTC; RTC holds offline. NTP wins on conflict.
- **RS485:** `UART_MODE_RS485_HALF_DUPLEX` — hardware auto-handles DE/RE via GPIO21. No manual toggling.
- **FreeRTOS tasks:** pinned to core 0. No blocking > 10s without `watchdog_feed()`.
- **PSRAM:** large buffers via `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`
- **ArduinoJson:** v7 (Waveshare bundles 7.2.1 — v7 API, not v6)
- **ESPHome:** used for sensor prototyping and Modbus register validation only — not production firmware
- **Fail-safes:** SD ring-buffer queue, MQTT replay on reconnect, hardware watchdog, crash log

## Board hardware notes

The Waveshare ESP32-S3-RS485-CAN has some onboard hardware to be aware of:

| Item | GPIO | Notes |
|---|---|---|
| Onboard PCF85063 RTC | GPIO38 (SDA) / GPIO39 (SCL) | Internal I²C bus, not on header. TWWP uses external DS3231 instead — no conflict. |
| Onboard CH1 control | GPIO47 | Reserved — do not use |
| RS485 transceiver | GPIO17/18/21 | Auto DE/RE via UART_MODE_RS485_HALF_DUPLEX |
| CAN transceiver | GPIO15/16 | TWAI peripheral |

## Pin map (M0)

| Function | GPIO | Notes |
|---|---|---|
| Leak DO | GPIO6 | INPUT_PULLUP, LOW = wet |
| I²C SDA (DS3231) | GPIO9 | 4.7kΩ pull-ups on module |
| I²C SCL (DS3231) | GPIO3 | Strapping pin, fine as driven clock |
| SPI MOSI (SD) | GPIO11 | SPI2 |
| SPI SCK (SD) | GPIO12 | |
| SPI MISO (SD) | GPIO13 | |
| SPI CS (SD) | GPIO14 | |
| Status LED | GPIO48 | WS2812 onboard RGB |

See `docs/PIN_ALLOCATION.md` and `03_waveshare_pinout_labeled.png` for full Phase 1 map.

## Quickstart

```bash
cp config_examples/secrets.h.sample include/secrets.h
# Fill in: MQTT_HOST, MQTT_PORT (8883), MQTT_USER, MQTT_PASS, MQTT_CA_CERT, NODE_ID

pio run
pio run -t upload
pio device monitor
```

First boot: AP `TWWP-Setup-<chipid>` (password `wateriswet`) → connect, browse to `192.168.4.1`, enter WiFi creds. Node joins WiFi, syncs NTP, connects MQTT/TLS, registers with HA.

## File map

```
twwp-node/
├── README.md
├── platformio.ini
├── include/
│   ├── secrets.h.sample      ← copy → secrets.h (gitignored)
│   ├── pins.h                ← all GPIO assignments
│   └── config.h              ← node ID, intervals, topics
├── src/
│   ├── main.cpp
│   ├── net_wifi.{h,cpp}      ← WiFiManager + reconnect
│   ├── net_mqtt.{h,cpp}      ← MQTT/TLS + HA discovery + offline buffer
│   ├── time_rtc.{h,cpp}      ← DS3231 + NTP sync
│   ├── store_sd.{h,cpp}      ← SD logging + ring-buffer queue
│   ├── watchdog.{h,cpp}      ← hardware WDT + crash log
│   ├── status_led.{h,cpp}    ← WS2812 RGB status
│   ├── sensor_leak.{h,cpp}   ← MH-RD (active M0)
│   ├── sensor_flow_stub.{h,cpp}
│   ├── sensor_pressure_stub.{h,cpp}
│   ├── sensor_temp_stub.{h,cpp}
│   ├── actuator_solenoid_stub.{h,cpp}
│   └── services/             ← introduced M6+
│       ├── HealthService.{h,cpp}
│       ├── CalibrationService.{h,cpp}
│       ├── AlertService.{h,cpp}
│       └── TelemetryService.{h,cpp}
├── docs/
│   ├── PIN_ALLOCATION.md
│   ├── MQTT_TOPIC_MAP.md
│   ├── HA_DISCOVERY.md
│   ├── FAILSAFE_DESIGN.md
│   ├── WIRING_M0.md
│   ├── FIRMWARE_ARCHITECTURE.md
│   ├── CALIBRATION.md        ← M6
│   ├── SOLENOID_DRIVER.md    ← M3
│   ├── DEVICE_LIFECYCLE.md   ← M8
│   └── HANDOFF_TO_ROO.md
├── ha_examples/
│   └── mosquitto_smoke_test.sh
└── config_examples/
    ├── secrets.h.sample
    └── node_config.json.sample
```

## Milestone roadmap

| Milestone | What | Status |
|---|---|---|
| M0 | Leak, RTC/SD, MQTT/TLS, HA discovery | 🔨 in progress |
| M0.5 | TLS hardening + per-device broker credentials | ⏳ next |
| M1 | Hall flow sensor + K-factor + daily totals | ⬜ |
| M2 | Pressure + DS18B20 | ⬜ |
| M3 | Solenoid command channel | ⬜ |
| M4 | OTA over MQTT | ⬜ |
| M5 | YiErYi 3788 RS485 (blocked) | 🔴 |
| M6 | HealthService + CalibrationService | ⬜ |
| M7 | AlertService + TelemetryService | ⬜ |
| M8 | Device lifecycle management | ⬜ |
