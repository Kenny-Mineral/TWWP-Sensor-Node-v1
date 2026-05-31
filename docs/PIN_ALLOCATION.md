# Pin Allocation — Waveshare ESP32-S3-RS485-CAN

**Last updated:** 2026-05-26
**Version:** v4 — OLED moved to main Wire bus (GPIO9/GPIO3); GPIO1/GPIO2 SH1.0 free.
**Source of truth in code:** `include/pins.h`. Update both together — never one alone.

---

## A. Hard-reserved — board hardware (do not reassign)

| GPIO | Function | Source |
|---|---|---|
| GPIO15 | CAN TXD2 (TWAI) | Board hardware |
| GPIO16 | CAN RXD2 (TWAI) | Board hardware |
| GPIO17 | RS485 TXD1 (UART1) | Board hardware |
| GPIO18 | RS485 RXD1 (UART1) | Board hardware |
| GPIO21 | RS485 DE/RE (TXD1EN) — auto-controlled by UART_MODE_RS485_HALF_DUPLEX | Board hardware |
| GPIO19 | USB D− | Board hardware |
| GPIO20 | USB D+ | Board hardware |
| GPIO43 | USB-CDC console TXD | Board hardware |
| GPIO44 | USB-CDC console RXD | Board hardware |
| GPIO38 | Onboard PCF85063 RTC — I²C SDA (internal, not on header) | Waveshare demo: I2C_Driver.h |
| GPIO39 | Onboard PCF85063 RTC — I²C SCL (internal, not on header) | Waveshare demo: I2C_Driver.h |
| GPIO47 | Onboard CH1 control | Waveshare demo: WS_GPIO.h |

> **RS485 note:** The board uses the ESP32's native `UART_MODE_RS485_HALF_DUPLEX` hardware mode.
> GPIO21 (TXD1EN) is set via `lidarSerial.setPins(-1, -1, -1, TXD1EN)` — the hardware toggles DE/RE automatically.
> No manual DE/RE toggling in firmware. No separate library needed for the physical bus layer.

> **Onboard PCF85063 RTC note:** The board has an onboard PCF85063 RTC at I²C address `0x51` on GPIO38/39.
> TWWP firmware uses the **external DS3231+SD combo module** instead — it runs on a separate I²C bus (GPIO9/GPIO3).
> There is no conflict. If you ever need both, use `Wire` for external (GPIO9/3) and `Wire1` for internal (GPIO39/38).

---

## B. Externally accessible header (2×12, requires opening enclosure)

| Left col | GPIO | | Right col | GPIO | Notes |
|---|---|---|---|---|---|
| 3V3 | — | | 5V | — | Power rails |
| GND | — | | GND | — | |
| TXD | GPIO43 | | IO20 | GPIO20 | USB D+ — leave alone |
| RXD | GPIO44 | | IO19 | GPIO19 | USB D− — leave alone |
| IO3 | GPIO3 | | IO14 | GPIO14 | GPIO3 = strapping pin, fine as driven I²C clock |
| IO4 | GPIO4 | | IO13 | GPIO13 | |
| IO5 | GPIO5 | | IO12 | GPIO12 | |
| IO6 | GPIO6 | | IO11 | GPIO11 | |
| IO7 | GPIO7 | | IO10 | GPIO10 | |
| IO8 | GPIO8 | | IO9 | GPIO9 | |

**SH1.0 sockets (side, no enclosure needed):** GPIO1, GPIO2 — both ADC1.

---

## C. Strapping / boot pins

| GPIO | Concern |
|---|---|
| GPIO0 | Boot mode — not on header |
| GPIO3 | JTAG source select — fine as driven I²C clock, avoid floating |
| GPIO45 | VDD_SPI — not on header |
| GPIO46 | Boot ROM log — not on header |

---

## D. Phase 1 full pin allocation (M0 → M3)

| Function | GPIO | Dir | Pull | ADC? | VCC | Notes |
|---|---|---|---|---|---|---|
| **Flow #1 pulse (Tap Output)** | 4 | INPUT | PULLUP | — | 5V | DWS-MH-02, Ch1 — user tap consumption. attachInterrupt FALLING |
| **Flow #2 pulse (RO Output)** | 5 | INPUT | PULLUP | — | 5V | USN-HS06PE, Ch2 — RO output into tank/system. attachInterrupt FALLING |
| **Leak DO** | 6 | INPUT | PULLUP | — | 3V3 | MH-RD comparator out, LOW = wet |
| **Flow #3 pulse (RO Input)** | 7 | INPUT | PULLUP | — | 5V | USN-HS06PS, Ch3 — raw RO input / grey-water reference. attachInterrupt FALLING |
| **Valve relay output** | 8 | OUTPUT | — | — | 5V (relay VCC) | Active-low relay module IN1. LOW=open. Current load: 12V LED; future: ball valve. |
| **I²C SDA (DS3231 + OLED)** | 9 | open-drain | 4.7kΩ on module | — | 3V3 | DS3231 @ 0x68, EEPROM @ 0x57, SSD1306 OLED @ 0x3C — shared Wire bus, no address conflict |
| **OLED button** | 10 | INPUT | PULLUP | — | 3V3 | Tactile button, LOW=pressed — cycles display frame |
| **SPI MOSI (SD)** | 11 | OUTPUT | — | — | 3V3 | SPI2/FSPI |
| **SPI SCK (SD)** | 12 | OUTPUT | — | — | 3V3 | |
| **SPI MISO (SD)** | 13 | INPUT | — | — | 3V3 | |
| **SPI CS (SD)** | 14 | OUTPUT | — | — | 3V3 | Software CS |
| **I²C SCL (DS3231 + OLED)** | 3 | open-drain | 4.7kΩ on module | — | 3V3 | Strapping pin — OK as driven I²C clock. Shared with OLED SSD1306. |
| **Status LED** | 48 | WS2812 data | — | — | 3V3 | Onboard RGB, FastLED |

---

## E. Free pins after Phase 1 + OLED

All IO3–IO14 on the 2×12 header are allocated.
SH1.0 sockets (GPIO1, GPIO2) are **free** — OLED moved to main Wire bus (GPIO9/GPIO3).
Off-header for future: GPIO1, GPIO2 (SH1.0), GPIO33–37, GPIO40–42. (GPIO47 reserved onboard.)

---

## F. Decision log

- [x] D1: GPIO access — full 2×12 header accessible (enclosure open). IO1/IO2 on SH1.0 always accessible.
- [x] D2: GPIO8 device — relay module (active-low, 5V coil). Currently driving 12V LED; future ball valve.
- [x] D3: Valve driver — relay module with optoisolator. GPIO8 → IN1. Jumper JD-VCC+VCC bridged (shared 5V).
- [x] D4: GPIO7 = flow ch3 (USN-HS06PS, raw RO input / grey-water reference). Ch1 (GPIO4) = DWS-MH-02 (user tap output). Ch2 (GPIO5) = USN-HS06PE (RO output to tank). Pressure sensor deferred to TBD free pin.
- [ ] D5: Leak detector — DO only (GPIO6) or DO + AO (GPIO6 + GPIO2)?
- [ ] D6: Power topology — 12V into board VCC terminal → tap 3V3/5V from header?
- [ ] D7: GPIO3 as I²C SCL — if uncomfortable, swap SCL→GPIO14 and SD CS→GPIO2.
- [ ] D8: DS3231 SQW → GPIO1 for RTC-wake deep sleep?

---

## G. Electrical sanity checks

- Hall flow sensors: confirmed working direct-to-ESP32. VCC = 5V, signal 3.3V-safe.
- All GPIOs are 3.3V max — never connect 5V signal direct.
- DS18B20: 3-wire mode, 4.7kΩ pull-up.
- DS3231: insert CR2032 before first power-up.
- Combo module VCC: verify 3.3V compatible before powering.
- Don't add I²C pull-ups if module already has them — stack = ~2.35kΩ effective, too low.
