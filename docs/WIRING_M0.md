# Wiring — Milestone 0

> **Pin map version:** v3 (April 19 2026) — leak DO is now **GPIO6**, SPI/I²C pins reorganised. If you have an older wiring diagram or breadboard from a previous session, re-check every wire against this document.

Wire only what is in this document. Everything else (flow, pressure, temp, solenoid) is stubbed in firmware and adds no wiring requirement for M0.

---

## What you need

- Waveshare ESP32-S3-RS485-CAN board (image 4)
- 12–24 V DC PSU for the board's top terminal, OR USB-C for bench bring-up
- MH-RD raindrop / leak module — both boards (probe + comparator), 3-wire ribbon (image 6)
- Combo RTC (DS3231) + microSD breakout (image 5)
- Formatted FAT32 microSD card (8–32 GB SDHC, Class 10+)
- CR2032 coin cell for the DS3231
- ~12 × 150 mm jumper wires (M-F for header, M-M if soldering)
- Cheap multimeter for continuity checks

---

## Step 1 — Open the enclosure

The 2×12 GPIO header is inside the case. Pop the two end caps and slide the PCB out. You lose the IP rating once opened — that's fine for bench bring-up.

---

## Step 2 — Power

For first bring-up, power from USB-C only. Move to 12 V once everything works.

```
12–24 V (+) ──→ board top terminal "VCC"
12–24 V (−) ──→ board top terminal "GND"
```

---

## Step 3 — I²C bus (DS3231 RTC)

```
RTC VCC ──→ board 3V3 pin
RTC GND ──→ board GND pin
RTC SDA ──→ board GPIO9
RTC SCL ──→ board GPIO3
RTC SQW    leave unconnected (future use)
RTC 32K    leave unconnected
```

The combo module has 4.7 kΩ pull-ups onboard — do not add more.

Insert the CR2032 **before** powering up so the RTC starts with a valid clock state.

---

## Step 4 — SPI bus (SD card)

The SD slot and RTC sit on the same combo module and share VCC/GND. The SD is on SPI2.

```
SD VCC  ──→ board 3V3   (shared with RTC VCC — same module pin)
SD GND  ──→ board GND
SD MOSI ──→ board GPIO11
SD SCK  ──→ board GPIO12
SD MISO ──→ board GPIO13
SD CS   ──→ board GPIO14
```

Format the card FAT32, no tricks needed. Insert card before power-up.

---

## Step 5 — Leak detector (MH-RD)

Two PCBs in the kit:

- **Probe board** — copper interdigitated grid. Two pads at the bottom (DSK, MH-RD). 2-wire to the comparator.
- **Comparator board** — small blue PCB with trimmer pot and 4 pins: `VCC GND DO AO`.

```
Probe DSK   ───→ Comparator DSK pad
Probe MH-RD ───→ Comparator MH-RD pad

Comparator VCC ──→ board 3V3
Comparator GND ──→ board GND
Comparator DO  ──→ board GPIO6      ← digital out (LOW = wet)
Comparator AO  ──→ (leave unconnected for M0)
```

After power-up, drip a few drops on the probe — the on-board LED should light. Adjust the trimmer pot if sensitivity is wrong.

---

## Step 6 — Onboard status LED

Nothing to wire — already on GPIO48.

---

## Wiring summary table

| Module | Signal | ESP32 GPIO | Notes |
|---|---|---|---|
| DS3231 | VCC | 3V3 | |
| DS3231 | GND | GND | |
| DS3231 | SDA | **GPIO9** | I²C |
| DS3231 | SCL | **GPIO3** | I²C — strapping pin, fine as driven clock |
| SD card | VCC | 3V3 | Shared with RTC on combo module |
| SD card | GND | GND | |
| SD card | MOSI | **GPIO11** | SPI2 |
| SD card | SCK | **GPIO12** | SPI2 |
| SD card | MISO | **GPIO13** | SPI2 |
| SD card | CS | **GPIO14** | Software CS |
| MH-RD | VCC | 3V3 | |
| MH-RD | GND | GND | |
| MH-RD | DO | **GPIO6** | Digital in, LOW = wet |

---

## Sanity checks before flashing

1. Continuity between every wire and its labelled header pin.
2. 3.3 V present on 3V3 rail under USB power (measure with multimeter).
3. DS3231 visible at address 0x68 on I²C scan (firmware prints this on boot).
4. SD card mounts as FAT32 in your laptop — then eject and insert into board.

---

## After flashing — expected serial output

```
[TWWP] booting...
[SD] ready, next buffer seq=0
[RTC] online, time = 2026-04-19T...Z
[BOOT] boot reason=POWERON
[WiFi] hostname=twwp-node-abc123
[WiFi] no saved credentials — starting captive portal AP: TWWP-Setup-XXXX
...
[WiFi] connected, IP=..., RSSI=...
[RTC] NTP epoch = ...
[MQTT] connecting to mqtt.twwp.nz:8883 (TLS) as twwp_wh_001 ...
[MQTT] TLS handshake OK
[MQTT] connected
[MQTT] HA discovery: homeassistant/binary_sensor/.../leak/config
[LEAK] initial state: DRY
[WDT] armed, timeout=30s
[TWWP] ready
```

Drip water on the probe → `[LEAK] state change → WET` → check HA binary_sensor flips to "Detected" within ~1 second.

---

## What is NOT wired for M0

The following are stubbed in firmware and will not cause compile errors — do not wire them yet:

| Sensor | GPIO | Milestone |
|---|---|---|
| Flow sensor #1 | GPIO4 | M1 |
| Flow sensor #2 | GPIO5 | M1 |
| Pressure | GPIO7 | M2 |
| DS18B20 | GPIO10 | M2 |
| Solenoid gate | GPIO8 | M3 |
| YiErYi 3788 RS485 | GPIO17/18/21 | M5 |
