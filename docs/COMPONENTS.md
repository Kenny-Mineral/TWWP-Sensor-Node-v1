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
| **USN-HS06PE** | 0.3–6.0 LPM | 38 | 1/4" PE pipe | General purpose, wider range | https://www.aliexpress.com/item/1005006630839984.html |
| **USN-HS06PS** | 0.1–1.0 LPM | 200 | 1/4" PE or PVC | Low-flow, 0.8 mm inner bore, high pressure drop | https://www.aliexpress.com/item/1005006630839984.html |

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

Set in `/config/node.json` on the SD card:

```json
{
  "flow": {
    "k_factor_1": 38,
    "k_factor_2": 38
  }
}
```

| Key | Default | Meaning |
|---|---|---|
| `flow.k_factor_1` | 38 | K value for flow sensor on GPIO4 |
| `flow.k_factor_2` | 38 | K value for flow sensor on GPIO5 |

Swap to K=200 for USN-HS06PS by editing the SD card file — no reflash.

### Wiring

3-wire: VCC (5 V), GND, Signal (open-collector, pulled up internally via `INPUT_PULLUP`).

| Signal | GPIO |
|---|---|
| Flow #1 signal | GPIO4 |
| Flow #2 signal | GPIO5 |
| VCC | 5 V header |
| GND | GND header |

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
| **Baud** | TBD — confirm from Modbus register map |
| **GPIO** | TX→GPIO17, RX→GPIO18, DE/RE→GPIO21 (auto via `UART_MODE_RS485_HALF_DUPLEX`) |
| **Status** | Blocked — hardware debug pending |
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
