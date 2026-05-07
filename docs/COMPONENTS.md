# TWWP Component Registry

All physical hardware used in this project. Update this file whenever a component is added, swapped, or confirmed.

---

## Main Controller

| Field | Value |
|---|---|
| **Part** | Waveshare ESP32-S3-RS485-CAN |
| **MCU** | Espressif ESP32-S3, dual-core 240 MHz, 8 MB PSRAM, 16 MB Flash |
| **Connectivity** | WiFi 2.4 GHz, USB-C, RS485 (UART1), CAN (TWAI) |
| **Onboard** | PCF85063 RTC (unused — external DS3231 preferred), WS2812 RGB LED (GPIO48) |
| **Enclosure** | IP67-rated, 2×12 header inside enclosure |
| **Power input** | 12–24 V DC via screw terminal, or USB-C for bench use |
| **URL** | https://www.waveshare.com/wiki/ESP32-S3-RS485-CAN |

---

## Modules

### DS3231 + microSD Combo Module

| Field | Value |
|---|---|
| **Part** | DS3231 RTC + microSD SPI breakout (combo board) |
| **RTC chip** | DS3231 — temperature-compensated crystal, ±2 ppm accuracy |
| **I²C address** | 0x68 (RTC), 0x57 (EEPROM if fitted) |
| **SD interface** | SPI (MOSI/SCK/MISO/CS) |
| **Pull-ups** | 4.7 kΩ onboard on SDA/SCL — do not add more |
| **Backup** | CR2032 coin cell — insert before first power-up |
| **VCC** | 3.3 V |
| **GPIO** | SDA→GPIO9, SCL→GPIO3, MOSI→GPIO11, SCK→GPIO12, MISO→GPIO13, CS→GPIO14 |
| **URL** | TBD — confirm *(search "DS3231 SD combo module" on AliExpress)* |

### MH-RD Raindrop / Leak Detector

| Field | Value |
|---|---|
| **Part** | MH-RD raindrop sensor module (probe board + comparator board) |
| **Output** | DO (digital, LOW = wet), AO (analog, unused) |
| **Logic** | Comparator with trimmer pot for sensitivity |
| **VCC** | 3.3 V |
| **GPIO** | DO→GPIO6 |
| **Notes** | Adjust trimmer until on-board LED lights reliably when probe is wet |
| **URL** | TBD — confirm *(search "MH-RD raindrop sensor module" on AliExpress)* |

---

## Flow Sensors

K value = pulses per litre. The active K value for each channel is loaded from `/config/node.json` at boot — no reflash needed when swapping sensors.

### Sensor Library

| Model | Flow Range | K Value (pulses/L) | Pipe fit | Notes | URL |
|---|---|---|---|---|---|
| **USN-HS06PE** | 0.3–6.0 LPM | 5,500 | 1/4" PE pipe | RO output — general purpose, wider range | https://www.aliexpress.com/item/1005006630839984.html |
| **USN-HS06PS** | 0.1–1.0 LPM | 20,700 | 1/4" PE or PVC | RO input — low-flow, 0.8 mm inner bore, high pressure drop | https://www.aliexpress.com/item/1005006630839984.html |

> Add a row whenever a new sensor model is tested. The K value on the housing is a starting point — calibrate against a measured volume for best accuracy.

### Tracked Metrics (per channel)

These are the values the firmware captures and publishes to HA, matching the Flowtek HA Water V2 dashboard:

| Metric | Unit | HA entity type | Persists reboot? | Notes |
|---|---|---|---|---|
| K value (active) | pulses/L | sensor / diagnostic | — | Loaded from `node.json`, shown in HA for verification |
| Flow rate | L/min | sensor, measurement | No | Calculated each second from pulse count |
| Total usage | L | sensor, total_increasing | Yes | Saved to SD `/config/flow_total.json` |
| Usage today | L | sensor, measurement | Partial | Reset at midnight via RTC date change |
| Usage this week | L | sensor, measurement | Partial | Reset on Monday midnight |
| Usage this month | L | sensor, measurement | Partial | Reset on 1st of month midnight |
| Usage this year | L | sensor, measurement | Partial | Reset on 1 Jan midnight |

> "Partial" means the daily/weekly/monthly/yearly subtotals reset correctly as long as the node hasn't been powered off across a boundary. The lifetime total is always safe — it is persisted to SD.

### K-Value Config

Flow calibration and signal processing parameters are loaded from `/config/node.json` on the SD card at boot. No reflash needed to change sensors or runtime settings.

Multi-point calibration example:

```json
{
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
  }
}
```

| Key | Default | Meaning |
|---|---|---|
| `flow.k_factor_1` | 5500 | Nominal K for flow sensor 1 (RO Output, USN-HS06PE on GPIO4). Fallback if no K-table is configured. |
| `flow.k_factor_2` | 20700 | Nominal K for flow sensor 2 (RO Input, USN-HS06PS on GPIO5). Fallback if no K-table is configured. |
| `flow.k_table_1` | — | Optional ordered array of calibration points for channel 1 (lowest flow first). Overrides single K. |
| `flow.k_table_2` | — | Optional ordered array of calibration points for channel 2 (lowest flow first). Overrides single K. |
| `flow.debounce_us_1` | 1000 | ISR debounce for channel 1 (µs). Range 100–10000. Default 1000 is safe to ~10.9 L/min. |
| `flow.debounce_us_2` | 500 | ISR debounce for channel 2 (µs). Range 100–10000. Default 500 is safe to ~5.8 L/min. |
| `flow.flow_avg_window` | 5 | Moving-average window size (samples, 1–20). Shared across both channels. |

> If a K-table is absent, the firmware uses the matching single `k_factor_*` as a 1-point table. Backward compatible with older `node.json` files that only have `k_factor_1` / `k_factor_2`.

### Wiring

3-wire: VCC (5 V), GND, Signal (open-collector, pulled up internally via `INPUT_PULLUP`).

| Signal | GPIO |
|---|---|
| Flow #1 signal | GPIO4 |
| Flow #2 signal | GPIO5 |
| VCC | 5 V header |
| GND | GND header |

---

### Calibration Data

Calibration testing (from `flow_sensor_calibration_v105.xlsx`) revealed significant K-factor variation in the PE (output) sensor and good linearity in the PS (input) sensor:

| Sensor | Channel | Nominal K | K at Low Flow | K at Med Flow | K at High Flow | Variation |
|---|---|---|---|---|---|---|
| USN-HS06PE (Output) | 1 (GPIO4) | ~5,500 | ~4,972 | ~5,468 | ~5,476 | **~10%** |
| USN-HS06PS (Input) | 2 (GPIO5) | ~20,700 | ~21,120 | ~20,818 | ~21,105 | ~2% |

**Key findings:**
- The PE sensor shows ~10% K variation across its flow range — a single fixed K causes systematic error, hence the multi-point K-table with linear interpolation.
- The PS sensor is relatively linear (~2% variation) — a single K works well but a K-table provides additional precision.
- The firmware defaults (`k_factor_1` = 5,500, `k_factor_2` = 20,700) are now calibrated values, not the old placeholder values (38 / 38).

**Firmware compensation strategy:**
1. Raw pulses are accumulated each 1-second interval.
2. A moving-average window (default 5 samples) smooths the flow rate before K lookup.
3. The smoothed flow rate selects (or interpolates between) K-table points.
4. Lifetime volume uses `totalPulses / interpolatedK(smoothedFlowRate)` — zero rounding error from raw pulse accumulation.

---

## Pressure Sensor

**Status: not yet purchased.**

| Field | Value |
|---|---|
| **Part** | TBD |
| **Output** | 0–5 V analog (assumed) |
| **Range** | TBD — confirm PSI/bar range before ordering |
| **Wiring** | 0–5 V → 2:1 resistor divider → 0–2.5 V → GPIO7 (ADC1_CH6) |
| **VCC** | 5 V |
| **GPIO** | GPIO7 |
| **URL** | TBD |

---

## Temperature

Temperature is provided by the YiErYi 3788 RS485 water quality sensor (see below). No separate DS18B20 probe is used in this node. GPIO10 is allocated but unused.

---

## Actuator

**Status: not yet purchased.**

| Field | Value |
|---|---|
| **Part** | TBD — solenoid valve or motorised ball valve |
| **Driver** | N-MOSFET (IRLZ44N + 1N4007 flyback diode) if solenoid |
| **GPIO** | GPIO8 (MOSFET gate via 100 Ω series, 10 kΩ pull-down) |
| **VCC** | 12 V (coil, from board screw terminal) |
| **URL** | TBD |

---

## Water Quality Sensor

| Field | Value |
|---|---|
| **Part** | YiErYi 3788 (RS485 Modbus) |
| **Interface** | RS485 half-duplex, UART1 |
| **Parameters** | pH, ORP, EC, TDS, CF, water temperature, relative humidity |
| **Baud** | 9600 8N1 |
| **GPIO** | TX→GPIO17, RX→GPIO18, DE/RE→GPIO21 (auto via `UART_MODE_RS485_HALF_DUPLEX`) |
| **Status** | Firmware driver implemented; hardware response validation pending |
| **URL** | TBD |

---

## Consumables

| Item | Spec | Notes |
|---|---|---|
| microSD card | 8–32 GB SDHC, Class 10+ | Format FAT32 before first use |
| CR2032 coin cell | 3 V | DS3231 backup — insert before first power-up |

---

## Outstanding TBDs

| Item | Needed for |
|---|---|
| DS3231+SD combo module URL | Docs completeness |
| MH-RD module URL | Docs completeness |
| Pressure transducer model + URL | M2 firmware |
| Actuator model (solenoid or ball valve) + URL | M3 firmware |
| YiErYi 3788 URL + Modbus register map | M5 firmware |
