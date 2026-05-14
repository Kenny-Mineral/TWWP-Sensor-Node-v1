# User Operations

Practical commands for running and maintaining a TWWP sensor node from the same computer used for PlatformIO.

## Build, Upload, Monitor

Run these from the PlatformIO project root:

```bash
pio run
pio run -t upload
pio device monitor
```

Combined upload and monitor:

```bash
pio run -t upload && pio device monitor
```

If the upload stalls at `Connecting...`, the chip needs to be put into download mode manually:

1. Hold the **BOOT** button.
2. Tap the **RESET** button.
3. Release **BOOT**.

The upload will then proceed. After it finishes, tap **RESET** once more to boot the new firmware normally (or let `pio device monitor` restart it automatically).

If `pio` is not on `PATH`, use:

```bash
/home/kenny/.platformio/penv/bin/pio run
/home/kenny/.platformio/penv/bin/pio run -t upload
/home/kenny/.platformio/penv/bin/pio device monitor
```

Monitor controls:

```text
Ctrl+C          quit monitor
Ctrl+T Ctrl+H   monitor help
```

Expected healthy boot signs:

```text
[BOOT] TWWP Sensor Node starting
[WiFi] connected, IP=...
[RTC] NTP sync ok ...
[VOLTAGE] begin ok
[MQTT] connected
[MQTT] HA discovery published
[M0] status ... "ts":<non-zero>
```

If you see `[VOLTAGE] ADS1115 not found` instead, check that the ADS1115 is wired to GPIO9 (SDA) / GPIO3 (SCL) and that the ADDR pin is tied to GND (I²C address 0x48).

If you see `[OLED] SSD1306 not found at 0x3C` instead of `[OLED] ready`, check that the display VCC/GND are wired to the internal header 3V3/GND pins, and that SDA/SCL are on GPIO9/GPIO3.

## OLED Status Display

The node drives a 0.96" SSD1306 128×64 I²C OLED mounted on the enclosure. It shows a 7-frame sliding carousel cycling every 5 seconds, with a persistent header overlay at the top.

### Wiring

| OLED pin | Board connection |
|---|---|
| VCC | 3V3 on internal 2×12 header (requires enclosure open) |
| GND | GND on internal 2×12 header |
| SDA | GPIO9 — shared Wire bus with DS3231 and ADS1115 |
| SCL | GPIO3 — shared Wire bus with DS3231 and ADS1115 |

The SH1.0 side sockets (GPIO1/GPIO2) are **not** used for the OLED — they carry no power rail.

Button: tactile button, one leg to GPIO10, other leg to GND. Active-low (INPUT_PULLUP).

### Header (always visible — top 12 px)

```
0.0L          TWWP           WM
```

| Section | Meaning |
|---|---|
| Left — `x.xL` | Today's purified output volume (flow sensor 1, resets at midnight) |
| Centre — `TWWP` | Normal. Flashes `!LEAK!` at 1 Hz when leak sensor is wet. |
| Right — status flags | `W` = WiFi connected, `!` = WiFi lost. `M` = MQTT connected, `!` = MQTT lost. `B` = SD offline buffer has unsent messages. |

### Slides (cycle every 5 seconds)

| Frame | Title | Data shown |
|---|---|---|
| 1 | PRE-RO | TDS (ppm hero), EC, temp from TDS meter probe 1. pH/ORP show `--` (no Yieryi on this zone). |
| 2 | POST-RO | TDS (ppm hero), EC, temp from TDS meter probe 2. Rejection % calculated from pre/post TDS. pH/ORP show `--`. |
| 3 | REMIN | Full Yieryi meter readings — pH, ORP, TDS (ppm hero), EC, temp. |
| 4 | FLOW & WASTE | Live flow rates for both sensors (L/min), waste ratio. |
| 5 | STORAGE TANK | Estimated tank level (bar + %, litres, ETA to full). Level calculated from flow differential; override with `displayOled_setTankLiters()` when a level sensor is wired. |
| 6 | FILTER HEALTH | Stub — empty progress bars. Implement once cumulative filter-volume tracking is added. |
| 7 | Branding | TWWP logo + "Wholey Water Project". |

### Button

Press the tactile button (GPIO10) to immediately advance to the next frame. Debounce: 200 ms.

### Tank level calculation

In the absence of a physical level sensor, the firmware estimates tank fill from the flow differential:

```
tank += feed_lpm × dt × 0.20  (RO recovery ratio — 20% of feed becomes pure water)
tank -= dispense_lpm × dt
tank = clamp(tank, 0, 20 L)
```

Tank level is saved to NVS (namespace `oled`, key `tank_l`) every 60 seconds and restored on reboot. To override with a real sensor reading, call `displayOled_setTankLiters(float l)` from any driver.

---

## SD Serial Commands

Open the serial monitor:

```bash
pio device monitor
```

**Important:** the PlatformIO serial monitor in VS Code is **receive-only** — keystrokes are not forwarded to the device. To type commands, use `screen` instead:

```bash
sudo apt install screen      # first time only
screen /dev/ttyACM0 115200
```

To exit screen: `Ctrl+A` then `K` then `Y`. Find the correct port with `ls /dev/ttyACM*`.

Then type one command per line and press Enter.

```text
help
sdinfo
sdls
sdls /log
sdcat /log/2026-04-26.csv
sdrm /log/2000-00-00.csv
sdprune
```

Command behavior:

| Command | What it does |
|---|---|
| `help` | Prints available serial commands. |
| `sdinfo` | Prints SD status, retention settings, buffer count, and next buffer sequence. |
| `sdls` | Lists the SD card root. |
| `sdls <path>` | Lists a directory, for example `/log` or `/buf`. |
| `sdcat <path>` | Prints a file over USB serial. |
| `sdrm <path>` | Removes a file or empty directory. Refuses protected paths. |
| `sdprune` | Deletes dated `/log/YYYY-MM-DD.csv` files older than configured retention. |

## OTA Firmware Update

Two OTA paths are available:

| Path | Use case | Requirement |
|---|---|---|
| ArduinoOTA | LAN development upload — no USB cable needed | Node powered, on same WiFi as laptop |
| MQTT-driven OTA | Remote update from anywhere | Node online, firmware binary on reachable HTTP(S) server |

---

### Method 1 — ArduinoOTA (LAN, no USB)

The node advertises itself via mDNS as `twwp-wh_001.local` once WiFi connects.

**Steps:**

1. Make sure the node is powered and connected to WiFi (check `twwp/wh_001/#` heartbeat is arriving).
2. In VS Code PlatformIO toolbar, click the environment selector (bottom status bar — shows `waveshare-esp32-s3-rs485-can` by default) and switch to **`ota`**.
3. Click the **Upload** button (→). PlatformIO will build and push over WiFi.
4. Switch the environment back to `waveshare-esp32-s3-rs485-can` for normal USB work.

The `[env:ota]` block in `platformio.ini` targets the node's current LAN IP. If the NODE_ID or DHCP lease changes, update `upload_port` in that block.

ArduinoOTA is a local-network workflow only. It relies on `espota` over port `3232` and is not the off-site update path; Tailscale is useful for reaching Home Assistant or your tooling remotely, but the node itself still needs the firmware file to be reachable from its own network.

ArduinoOTA has no password set — LAN access only, not exposed to the internet.

---

### Method 2 — MQTT-driven OTA (remote)

Use this when the node is deployed and USB access is not practical. This is the correct off-site update path.

**Step 1 — Build the binary:**

```bash
pio run
```

The compiled binary is at:
```
.pio/build/waveshare-esp32-s3-rs485-can/firmware.bin
```

**Step 2 — Get the MD5:**

```bash
md5sum .pio/build/waveshare-esp32-s3-rs485-can/firmware.bin
```

**Step 3 — Upload to the firmware server:**

```bash
scp .pio/build/waveshare-esp32-s3-rs485-can/firmware.bin \
  kenny@100.67.244.37:/var/www/twwp/firmware/firmware.bin
```

Password: see `~/.twwp/INFRASTRUCTURE.md` (kenny SSH password via Tailscale).

The nginx container serves `https://twwp-iot.duckdns.org/firmware/firmware.bin` from `/var/www/twwp/firmware/` on the host. Directory is owned by kenny — no sudo or Docker workaround needed.

The firmware host can be:

- A normal public HTTPS site
- A private HTTP server reachable from the node's network
- A different HTTPS host than the MQTT broker, as long as its CA is added to `OTA_CA_CERT` in `include/secrets.h`

The OTA client follows up to 3 HTTP redirects. Use a direct static file URL when possible.

**Step 4 — Trigger the update via MQTT:**

```bash
mosquitto_pub \
  -h twwp-iot.duckdns.org -p 8883 \
  --capath /etc/ssl/certs \
  -u YOUR_MQTT_USER -P YOUR_MQTT_PASS \
  -t 'twwp/wh_001/cmd' \
  -m '{"ota_url":"https://twwp-iot.duckdns.org/firmware/twwp-vX.X.X.bin","ota_md5":"<md5-from-step-2>"}'
```

`ota_md5` is optional but recommended — the node will reject the image if the hash doesn't match.

Examples:

```text
https://twwp-iot.duckdns.org/firmware/twwp-v0.1.1.bin
http://192.168.20.10:8080/firmware/twwp-v0.1.1.bin
```

**Monitor progress:**

```bash
mosquitto_sub \
  -h twwp-iot.duckdns.org -p 8883 \
  --capath /etc/ssl/certs \
  -u YOUR_MQTT_USER -P YOUR_MQTT_PASS \
  -t 'twwp/wh_001/#' -v
```

Watch `twwp/wh_001/ota_state` and `ota_progress_pct` in the heartbeat. The node reboots automatically on success.

---

### OTA via serial console (USB only)

When connected via USB, the serial console also accepts OTA commands:

```text
ota https://twwp-iot.duckdns.org/firmware/twwp-vX.X.X.bin
ota https://twwp-iot.duckdns.org/firmware/twwp-vX.X.X.bin <md5>
ota_state
```

Note: the serial console input buffer is 128 bytes — very long URLs may be truncated. Use MQTT OTA for long URLs.

---

### OTA status fields in heartbeat

| Field | Type | Meaning |
|---|---|---|
| `ota_state` | number | `0=IDLE`, `1=DOWNLOADING`, `2=VERIFYING`, `3=APPLYING`, `4=SUCCESS`, `5=FAILED` |
| `ota_progress_pct` | number | Download progress 0–100% |
| `ota_error` | string | Last error message (only present on failure) |

### Home Assistant OTA entities

| Entity | Purpose |
|---|---|
| `sensor.twwp_<id>_ota_state` | Current OTA state code |
| `sensor.twwp_<id>_ota_progress` | Download progress % |

### Rollback

If the node crashes within 60 seconds of booting new firmware, it automatically rolls back to the previous partition. No action needed.

Protected paths that `sdrm` refuses:

```text
/
/log
/buf
/data
/config
```

Example cleanup of the old malformed test log:

```text
sdrm /log/2000-00-00.csv
sdls /log
```

## Saving SD Output To A Local File

Use PlatformIO's monitor log filter:

```bash
pio device monitor --filter log2file
```

Then run:

```text
sdcat /log/2026-04-26.csv
```

PlatformIO writes the monitor stream to a local log file in the project folder. The output includes normal telemetry as well as the SD file dump, so trim it on the computer if you need a clean CSV export.

## SD Retention Config

Optional settings live on the SD card at:

```text
/config/node.json
```

Minimal example:

```json
{
  "sd": {
    "serial_commands_enabled": true,
    "retention_days": 365,
    "auto_prune": false
  }
}
```

Fields:

| Field | Meaning |
|---|---|
| `serial_commands_enabled` | Enables or disables SD serial maintenance commands. Defaults to `true`. |
| `retention_days` | Number of days to keep dated `/log/YYYY-MM-DD.csv` files. `0` disables pruning. Defaults to `0`. |
| `auto_prune` | If `true`, runs retention pruning once at boot. Defaults to `false`. |

To prune manually:

```text
sdprune
```

Pruning only applies to valid dated log files such as:

```text
/log/2026-04-26.csv
```

It does not remove `/buf` offline MQTT queue files or undated logs like `unsynced.csv`.

## Current Data Offload Status

Implemented now:

```text
Manual serial export: sdcat <path>
Offline MQTT buffering: /buf/<seq>.json drains when MQTT reconnects
Daily CSV logging: /log/YYYY-MM-DD.csv
```

Not implemented yet:

```text
Cloud folder upload
Date-range upload
Scheduled daily/weekly/monthly/season/year offload
Delete-after-upload
Server-side archive folders
```

Recommended future design:

```text
Node -> MQTT batch upload -> server archive folder/cloud storage
```

The node should stay simple and reliable. Cloud storage credentials, date range queries, folders, and long-term retention are better handled on the server side. OTA remains separate and is only for firmware updates.

## Resetting WiFi Credentials

Hold the **BOOT button** (GPIO0, the button labelled BOOT on the board) for more than 5 seconds while the node is running normally. The node will:

1. Print `[BOOT] reset-creds gesture — clearing WiFi credentials` to serial.
2. Erase all saved WiFi credentials from flash.
3. Reboot immediately into the WiFi setup captive portal.

Connect to the `TWWP-Setup-XXXXXX` access point with password `wateriswet` and open `http://192.168.4.1` to enter new credentials.

Note: this does not affect the SD card, MQTT credentials, or any configuration in `/config/node.json`.

## Heartbeat / Status JSON

The node publishes a status message to `twwp/<node_id>/status` every 10 seconds and on every MQTT reconnect. Fields:

| Field | Type | Meaning |
|---|---|---|
| `node_id` | string | Node identifier from `secrets.h`. |
| `firmware` | string | Firmware version string. |
| `leak` | bool | `true` if leak sensor is wet. |
| `leak_state` | string | `"WET"` or `"DRY"`. |
| `uptime_ms` | number | Milliseconds since last boot. |
| `wifi_rssi` | number | WiFi signal strength in dBm. `0` if disconnected. |
| `wifi_ssid` | string | Connected WiFi network name. Empty if disconnected. |
| `ip` | string | Node IP address. Empty if disconnected. |
| `mqtt_buffer_count` | number | Number of messages queued in `/buf/` waiting to be sent. |
| `ts` | number | Unix timestamp from DS3231 RTC. `0` if RTC not ready. |
| `wifi_bssid` | string | MAC address of the connected access point. Empty if disconnected. |
| `wifi_signal_pct` | number | WiFi signal strength as percentage (0–100). `0` if disconnected. |
| `wifi_status` | string | `"Connected"` or `"Disconnected"`. |
| `uptime_s` | number | Seconds since last boot. |
| `flow_rate_1` | number | Flow sensor 1 — current rate in L/min. |
| `flow_rate_2` | number | Flow sensor 2 — current rate in L/min. |
| `flow_total_1` | number | Flow sensor 1 — lifetime total in L (persisted to SD). |
| `flow_total_2` | number | Flow sensor 2 — lifetime total in L (persisted to SD). |
| `flow_today_1` | number | Flow sensor 1 — usage today (L, resets at midnight). |
| `flow_today_2` | number | Flow sensor 2 — usage today (L, resets at midnight). |
| `flow_week_1` | number | Flow sensor 1 — usage this week (L, resets Monday midnight). |
| `flow_week_2` | number | Flow sensor 2 — usage this week (L, resets Monday midnight). |
| `flow_month_1` | number | Flow sensor 1 — usage this month (L, resets 1st of month). |
| `flow_month_2` | number | Flow sensor 2 — usage this month (L, resets 1st of month). |
| `flow_year_1` | number | Flow sensor 1 — usage this year (L, resets 1 Jan). |
| `flow_year_2` | number | Flow sensor 2 — usage this year (L, resets 1 Jan). |
| `k_factor_1` | number | Nominal K value for flow sensor 1 (pulses/L). Mirrors `flow.k_factor_1` in `node.json`. |
| `k_factor_2` | number | Nominal K value for flow sensor 2 (pulses/L). Mirrors `flow.k_factor_2` in `node.json`. |
| `k_applied_1` | number | Interpolated K value currently applied to channel 1 (pulses/L). Updated each second from K-table lookup against smoothed flow rate. |
| `k_applied_2` | number | Interpolated K value currently applied to channel 2 (pulses/L). |
| `pulses_raw_1` | number | Lifetime raw pulse count for channel 1 (uint64_t). Never reset — enables post-hoc recalibration. Persisted to NVS + SD. |
| `pulses_raw_2` | number | Lifetime raw pulse count for channel 2 (uint64_t). Never reset — enables post-hoc recalibration. Persisted to NVS + SD. |
| `flow_avg_window_1` | number | Smoothed/averaged flow rate for channel 1 (L/min). Output of the moving-average ring buffer, used as input for K-table interpolation. |
| `flow_avg_window_2` | number | Smoothed/averaged flow rate for channel 2 (L/min). |
| `k_table_1` | string | Current K-table for channel 1 as JSON array. Empty string if using single-point table. |
| `k_table_2` | string | Current K-table for channel 2 as JSON array. Empty string if using single-point table. |
| `debounce_us_1` | number | ISR debounce period for channel 1 (µs). Range 100–10000. |
| `debounce_us_2` | number | ISR debounce period for channel 2 (µs). Range 100–10000. |
| `flow_avg_window` | number | Moving-average window size (samples, 1–20). Shared across both channels. |
| `session_last_id` | number | ID of the most recently completed session. |
| `session_last_start_ts` | number | Unix timestamp when the last session started. |
| `session_last_end_ts` | number | Unix timestamp when the last session ended. |
| `session_last_dur_s` | number | Total wall-clock duration of the last session in seconds. |
| `session_last_flow_dur_s` | number | Seconds water was actually flowing during the last session (excludes idle gaps). |
| `session_last_idle_s` | number | Idle gap time within the last session in seconds (`dur_s − flow_dur_s`). Zero for a normal single tap-on → tap-off event. |
| `session_last_vol_out` | number | Volume through RO Output (sensor 1) during last session, in litres. |
| `session_last_vol_in` | number | Volume through RO Input (sensor 2) during last session, in litres. |
| `session_enabled` | bool | Whether session tracking is currently enabled. |
| `session_idle_timeout_s` | number | Session idle timeout in seconds (5–100). |
| `flow_threshold_lpm` | number | Flow detection threshold in L/min (0.01–0.5). Flow below this is treated as off. |
| `leak_suspect_1` | bool | Non-zero flow below threshold on channel 1 while no session is active. |
| `leak_suspect_2` | bool | Non-zero flow below threshold on channel 2 while no session is active. |
| `waste_ratio_today` | number | Waste:pure ratio for today. (input - output) / output. |
| `waste_ratio_week` | number | Waste:pure ratio for this week. |
| `waste_ratio_month` | number | Waste:pure ratio for this month. |
| `ota_state` | number | OTA state enum: `0=IDLE`, `1=DOWNLOADING`, `2=VERIFYING`, `3=APPLYING`, `4=SUCCESS`, `5=FAILED`. |
| `ota_progress_pct` | number | OTA download progress percentage. |
| `ota_error` | string | Last OTA error message. Present only after a failed OTA. |
| `waste_ratio_year` | number | Waste:pure ratio for this year. |
| `supply_voltage` | number | Calibrated battery voltage in V (5-sample moving average). `0.0` if ADS1115 not found at boot. |
| `supply_voltage_divider` | number | Raw ADS1115 input voltage at the divider midpoint, before the 4.0303x battery scaling. Useful for wiring/debug checks. |
| `supply_voltage_pct` | number | Battery percentage (0–100) calculated from `voltage_v_min` and `voltage_v_max`. |
| `supply_voltage_state` | string | `"Charging"`, `"Discharging"`, or `"Stable"` — derived from voltage trend over the last 60 s. |
| `voltage_v_min` | number | Configured empty-battery voltage (V). Default 11.8. Persisted to NVS. |
| `voltage_v_max` | number | Configured full-battery voltage (V). Default 12.6. Persisted to NVS. |
| `voltage_cal_factor` | number | Voltage calibration multiplier. Default 1.0. Adjust to match a multimeter reading. Persisted to NVS. |
| `valve_open` | bool | `true` when the relay is energised (valve open / LED on). |
| `valve_auto` | bool | `true` = relay is in auto mode (flow-driven). `false` = manual MQTT override is active. |

Note: period subtotals (today/week/month/year) reset correctly across RTC date boundaries. If the node is powered off across a boundary, the subtotals resume from the saved value for that day but reset on the next boundary. The lifetime total (`flow_total_*`) always survives reboots — it is persisted to `/config/flow_total.json` on the SD card every 60 seconds.

## Battery Voltage Monitor (M2.5)

### Hardware

The voltage monitor uses an ADS1115 16-bit I²C ADC (address 0x48) with a 100 kΩ / 33 kΩ resistor voltage divider. The divider scales a 12V battery down to ~3V — safe for the ADS1115 input. The ADS1115 shares the I²C bus with the DS3231 RTC (GPIO9 SDA / GPIO3 SCL) with no conflict.

Wiring summary:

```
12V battery + ──── R1 (100kΩ) ──┬──── A0 (ADS1115)
                                 │
                               C (100nF optional)
                                 │
                              R2 (33kΩ)
                                 │
                               GND ──── battery −
ADS1115: VCC → 3.3V, GND → GND, SDA → GPIO9, SCL → GPIO3, ADDR → GND
```

### HA entities

Three sensors and three config numbers appear automatically on the TWWP device card after the first MQTT connect:

| Entity | Type | What it shows |
|---|---|---|
| Supply Voltage | sensor | Calibrated battery voltage in V after divider scaling and calibration factor (device_class=voltage) |
| Battery | sensor | Battery percentage 0–100% (device_class=battery) |
| Supply Voltage Divider | sensor (diagnostic) | Raw ADS1115 divider-node voltage. Around `3.1V` means the divider is seeing a roughly `12.5V` battery. |
| Battery State | sensor | `Charging` / `Discharging` / `Stable` |
| Battery Empty Voltage | number (config) | Voltage treated as 0% — default 11.8V, range 9–13V |
| Battery Full Voltage | number (config) | Voltage treated as 100% — default 12.6V, range 12–16V |
| Battery Voltage Cal Factor | number (config) | Multiplier applied after divider calculation — default 1.000 |

All three config numbers persist to NVS across reboots. Changes take effect immediately — no reflash needed.

### Calibration procedure

1. Let the node run for at least 30 seconds so the moving average settles.
2. Measure the actual battery voltage with a multimeter.
3. Compare it to `supply_voltage` in the HA sensor or the heartbeat JSON.
   If `supply_voltage_divider` is around `3.1V`, the scaled `supply_voltage` should be around `12.5V`.
4. In HA, open **Battery Voltage Cal Factor** and adjust:
   - If HA reads 12.4V and multimeter reads 12.6V: set cal factor to `1.016` (12.6 / 12.4)
   - If HA reads 12.8V and multimeter reads 12.6V: set cal factor to `0.984`
5. Repeat until HA reading is within ±0.05V of the multimeter.

### Battery % configuration

The percentage is a linear interpolation between `v_min` (0%) and `v_max` (100%). Lead-acid typical values:

| Battery state | Lead-acid 12V |
|---|---|
| Full | 12.6–12.8V |
| 50% | ~12.2V |
| Empty (cut-off) | 11.8–12.0V |

Set `Battery Empty Voltage` and `Battery Full Voltage` in HA to match your battery's datasheet or measured behaviour.

### Charge state detection

The node compares the current smoothed voltage reading to the reading from 60 seconds ago:

- More than +0.05V in 60 s → **Charging**
- More than −0.05V in 60 s → **Discharging**
- Within ±0.05V → **Stable**

This requires the node to have been running for at least 60 seconds before `Charging`/`Discharging` will appear. Until then the state shows `Stable`.

---

## Time-Series Data Log

Every 60 seconds the node appends a row to:

```text
/data/YYYY-MM-DD.csv
```

Each file starts with a header row on creation:

```text
ts,flow_rate_1,flow_total_1,flow_today_1,flow_rate_2,flow_total_2,flow_today_2,leak,supply_voltage
```

Example rows:

```text
1745700000,1.234,456.789,5.234,0.000,123.456,0.000,0,12.543
1745700060,1.189,457.978,6.423,0.000,123.456,0.000,0,12.551
```

Units: `ts` = Unix timestamp, flow values = litres, `leak` = 0 or 1, `supply_voltage` = volts.

The data log writes regardless of WiFi or MQTT status — it is the offline record. Existing files keep their original columns — only new files created after the M2.5 firmware update will include `supply_voltage`.

**SD writes are silent on success.** No `[SD]` lines appear in the serial monitor for normal writes — only failures are logged (via `[SD] data write failed` or an MQTT alert to `twwp/<id>/log`). Absence of `[SD]` output after 60+ seconds of uptime is normal and expected.

View or export via serial:

```text
sdls /data
sdcat /data/2026-04-27.csv
```

Pruning applies to `/data/` the same as `/log/` — files older than `sd.retention_days` are removed by `sdprune`.

## HA Diagnostic Entities

The following diagnostic entities appear automatically on the device card in HA:

### WiFi & Connectivity

| Entity | Type | What it shows |
|---|---|---|
| WiFi Status | binary sensor | Connected / Disconnected |
| WiFi SSID | sensor | Network name |
| WiFi BSSID | sensor | Access point MAC address |
| IP Address | sensor | Node IP on the local network |
| WiFi Signal dB | sensor | RSSI in dBm |
| WiFi Signal | sensor | Signal strength as % |
| Running Time | sensor | Time since last boot (HH:MM:SS) |
| Restart WiFi | button | Disconnects and reconnects WiFi without clearing credentials |

The **Restart WiFi** button sends `{"restart_wifi": true}` to `twwp/<id>/cmd`. The node disconnects and reconnects within a few seconds — no reboot, credentials are kept.

### Flow Sensor Diagnostics

These diagnostic entities appear under the RO Output and RO Input sub-device cards in HA:

| Entity | Type | What it shows | State class |
|---|---|---|---|
| Output Raw Pulses | sensor (diagnostic) | Lifetime raw pulse count for channel 1 (never reset) | total_increasing |
| Input Raw Pulses | sensor (diagnostic) | Lifetime raw pulse count for channel 2 (never reset) | total_increasing |
| Output Applied K | sensor (diagnostic) | Interpolated K value currently applied to channel 1 (pulses/L) | measurement |
| Input Applied K | sensor (diagnostic) | Interpolated K value currently applied to channel 2 (pulses/L) | measurement |
| Output Smoothed Flow | sensor (diagnostic) | Moving-average flow rate for channel 1 (L/min), used for K-table lookup | measurement |
| Input Smoothed Flow | sensor (diagnostic) | Moving-average flow rate for channel 2 (L/min), used for K-table lookup | measurement |

### Flow Configuration Entities

Configurable `number` and `text` entities appear on the device card for changing runtime settings:

| Entity | Type | What it does | Range |
|---|---|---|---|
| Output K Factor | number (config) | Single-point K value for channel 1. Setting this also creates a 1-point K-table. | 1–99999 |
| Input K Factor | number (config) | Single-point K value for channel 2. Setting this also creates a 1-point K-table. | 1–99999 |
| Output K Table | text (config) | JSON array of calibration points for channel 1. Example: `[{"flow_lpm":0.42,"k":4972}]` | — |
| Input K Table | text (config) | JSON array of calibration points for channel 2. | — |
| Output Debounce | number (config) | ISR debounce period in microseconds. | 100–10000 |
| Input Debounce | number (config) | ISR debounce period in microseconds. | 100–10000 |
| Flow Average Window | number (config) | Moving-average window size shared across both channels. | 1–20 |

These entities are all writable — changes are applied immediately in RAM and persisted to `/config/node.json` on the SD card.

## Flow Sensor Total Persistence

Flow totals survive power loss via two layers:

| Layer | What is saved | How often | Survives |
|---|---|---|---|
| NVS (ESP32 flash) | `flow_total_1`, `flow_total_2` | Every 10 s if changed | Power loss with ≤10 s gap |
| SD `/config/flow_total.json` | Totals + subtotals + date | Every 60 s if changed | Full flash wipe / reflash |

On boot the node loads SD first (has subtotals and date), then overlays the NVS total if it is larger (meaning NVS has a more recent value than the last SD save).

## Flow Sensor K Factor Config

Flow calibration is loaded from [`/config/node.json`](docs/USER_OPERATIONS.md:309) at boot. Phase 2 adds multi-point K tables plus a 5-sample moving-average flow window before K lookup. Change the file on the SD card — no reflash needed.

Current defaults:

| Sensor model | Channel | Default nominal K |
|---|---|---|
| USN-HS06PE (RO Output) | 1 / GPIO4 | 5500 pulses/L |
| USN-HS06PS (RO Input) | 2 / GPIO5 | 20700 pulses/L |

Minimal backward-compatible config:

```json
{
  "flow": {
    "k_factor_1": 5500,
    "k_factor_2": 20700
  }
}
```

If [`k_table_1`](docs/USER_OPERATIONS.md:329) or [`k_table_2`](docs/USER_OPERATIONS.md:334) is missing, the firmware automatically treats the matching single [`k_factor_*`](docs/USER_OPERATIONS.md:320) value as a 1-point calibration table.

Multi-point config example:

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
    ]
  }
}
```

Rules:

| Field | Meaning |
|---|---|
| `k_factor_1` / `k_factor_2` | Nominal fallback K for each channel, still published in heartbeat and HA. |
| `k_table_1` / `k_table_2` | Optional ordered calibration arrays, lowest flow first. |
| `flow_lpm` | Calibration flow point in L/min. |
| `k` | K factor at that flow point in pulses/L. |

The firmware clamps below the first point and above the last point, and linearly interpolates between points. Lifetime total volume now comes from raw total pulses divided by the interpolated K based on the smoothed flow rate. Today/week/month/year subtotals also use the interpolated K for each one-second interval.

## Session Tracking

A "session" is a continuous usage event at the RO tap. A session starts when flow on either sensor exceeds the flow threshold (default 0.05 L/min) and ends after the idle timeout expires (default 90 s) with no flow on either sensor. If flow resumes within the idle window, it extends the same session.

### Timing fields

Each session records three time values:

| Field | Meaning |
|---|---|
| `dur_s` | Total wall-clock time from first flow to session end (includes any idle gaps). |
| `flow_dur_s` | Seconds water was actually flowing — sum of all continuous flow segments. |
| `idle_s` | `dur_s − flow_dur_s` — time the tap was off mid-session before the idle timeout fired. |

For a normal tap-on → tap-off event, `flow_dur_s ≈ dur_s` and `idle_s = 0`. `idle_s` is non-zero only when the tap was briefly closed mid-session and the session was extended.

### Session log

When a session ends, the node publishes to `twwp/<node_id>/session` and appends a row to:

```
/log/sessions.csv
```

Session log columns:

```
session_id,start_ts,end_ts,duration_s,flow_duration_s,idle_time_s,volume_out_L,volume_in_L,peak_rate_out,peak_rate_in
```

View via serial:

```
sdcat /log/sessions.csv
```

### Recent sessions in HA

The last 10 sessions are published as a retained JSON array to `twwp/<node_id>/sessions_recent` and appear in HA as `sensor.twwp_wh_001_recent_sessions`. The full Lovelace dashboard at `docs/LOVELACE_DASHBOARD.yaml` includes a sessions view with the flex-table-card table — see the **Home Assistant Dashboard** section below.

The last session fields also appear in the heartbeat status (`session_last_id`, `session_last_dur_s`, `session_last_flow_dur_s`, `session_last_idle_s`, `session_last_vol_out`, `session_last_vol_in`) and are visible as sensors on the main TWWP device card in HA.

Session IDs survive reboots (persisted in NVS).

## Valve / Relay Control

The node controls a relay module on GPIO8 (`PIN_VALVE`). The relay module is active-low — GPIO `LOW` energises the coil (valve open / load on), GPIO `HIGH` de-energises it (valve closed / load off).

Current hardware: 12V LED simulating a ball valve. Wired: relay VCC → board 5V, relay GND → GND, relay IN1 → GPIO8, relay COM/NO terminals in series with the 12V load.

### Auto mode (default)

By default the relay follows flow sensor 1. When `flow_rate_1` exceeds 0.05 L/min the relay opens; when flow drops to zero the relay closes. No MQTT command needed for normal operation.

### Manual override via MQTT

Publish to `twwp/<node_id>/cmd`:

```json
{"valve_open": true}
```

This opens the relay and disables auto mode. To close:

```json
{"valve_open": false}
```

To re-enable flow-driven auto mode:

```json
{"valve_auto": true}
```

### HA entity

A `binary_sensor` named **TWWP \<id\> Valve** appears automatically on the device card (device_class=opening). It shows `Open` / `Closed` based on `valve_open` in the status heartbeat.

Manual open/close can be triggered from HA using the Developer Tools → MQTT → publish to `twwp/<id>/cmd`.

### Boot safety

GPIO8 is driven `HIGH` (relay off) as the very first action in `actuatorValve_begin()`, before WiFi or flow sensors initialise. The relay will never energise spuriously at boot.

---

## Valve Configuration

All six valve config fields are readable in `twwp/<id>/status` and writable via MQTT cmd or HA entity. All writes are persisted to NVS and survive reboots. Defaults can be set in `node.json` under the `"valve"` key.

### Valve type

Controls the hardware wiring model. Current bench setup is `test` (relay driving an LED).

```bash
mosquitto_pub --capath /etc/ssl/certs -h twwp-iot.duckdns.org -p 8883 \
  -u twwp_wh_001 -P <MQTT_PASS> \
  -t 'twwp/wh_001/cmd' -m '{"set_valve_type": "solenoid"}'
```

| Value | Meaning |
|---|---|
| `test` | Relay + LED indicator (current bench setup) |
| `solenoid` | Production solenoid — sustained energise to hold open |
| `ball_valve` | Not yet implemented — logs warning, falls back to solenoid behaviour |

### Trigger source

Controls what automatically opens/closes the valve.

```bash
mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"set_trigger_source": "manual"}'
```

| Value | Meaning |
|---|---|
| `flow` | Open when `flow_rate_1 > 0.05 L/min` (default) |
| `manual` | Loop does nothing; valve only responds to `valve_open` cmd |

### Safety timers

Two independent timers, both default to 0 (disabled). Either or both can be active simultaneously.

**Idle timeout** — safety-closes the valve when it has been open with no flow for N seconds:

```bash
mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"set_valve_idle_timeout": 300}'
```

**Max-open timeout** — safety-closes the valve N seconds after it was opened, regardless of flow:

```bash
mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"set_valve_max_open": 600}'
```

Set either to `0` to disable.

### Safety close behaviour

When a safety timer fires, the valve closes. Two optional follow-on actions:

```bash
# Also disable auto mode — prevents auto-reopen until manually re-enabled
mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"set_valve_timeout_disable_auto": true}'

# Publish an alert to twwp/wh_001/alert
mosquitto_pub ... -t 'twwp/wh_001/cmd' -m '{"set_valve_timeout_alert": true}'
```

The SD log always records a safety close event regardless of these flags.

Alert payload:
```json
{"type": "VALVE_SAFETY_CLOSE", "reason": "idle_timeout", "timeout_s": 300}
```

### node.json defaults

Add a `"valve"` block to `/config/node.json` to set boot-time defaults (NVS values overlay these):

```json
{
  "valve": {
    "valve_type":           "test",
    "trigger_source":       "flow",
    "idle_timeout_s":       0,
    "max_open_s":           0,
    "timeout_disable_auto": false,
    "timeout_alert":        true
  }
}
```

---

## Flow Reset Commands

Reset commands are available via MQTT (`twwp/<node_id>/cmd`), HA device card buttons, and serial console.

| Command | Effect |
|---|---|
| `reset_flow_today_1` | Zero today/week/month/year for RO Output (channel 1). Saved to SD. |
| `reset_flow_today_2` | Zero today/week/month/year for RO Input (channel 2). Saved to SD. |
| `reset_flow_today` | Zero period subtotals for both channels. Saved to SD. |
| `reset_flow_totals_1` | Zero lifetime total + all subtotals for channel 1. Clears NVS + SD. |
| `reset_flow_totals_2` | Zero lifetime total + all subtotals for channel 2. Clears NVS + SD. |
| `reset_flow_totals` | Zero all flow data for both channels. Clears NVS + SD. |

Via serial console:

```
reset_flow_today
reset_flow_today_1
reset_flow_today_2
reset_flow_totals
reset_flow_totals_1
reset_flow_totals_2
```

Via MQTT (example):

```json
{"reset_flow_today_1": true}
```

HA buttons: "Reset Today" and "Reset Totals" appear on the RO Output and RO Input device cards. "Reset Today (Both)" and "Reset Totals (Both)" appear on the main TWWP node card.

The nominal K value is published in the heartbeat as `k_factor_1` / `k_factor_2` and appears as a diagnostic sensor in HA — useful to confirm the configured fallback calibration without checking the SD card.

Flow totals are persisted to `/config/flow_total.json`. To inspect:

```text
sdcat /config/flow_total.json
```

## SD Failure Alerts

If the SD card fails a write, the node publishes a single MQTT message to:

```text
twwp/<node_id>/log
```

with payload:

```text
sd write failed: <context>
```

where `<context>` is `log`, `buffer`, or `buffer-write` depending on which operation failed. This alert is rate-limited to once per minute so it does not flood the broker.

## Buffer Overflow and Crash Log

The offline MQTT buffer in `/buf/` is capped at 500 files (`SD_MAX_BUFFER_LINES`). If the buffer is full when a new message needs to be stored, the oldest file is dropped and a warning is written to:

```text
/log/crashes.txt
```

Each line in `crashes.txt` has the format:

```text
<unix_timestamp>,buffer overflow: dropped /buf/<seq>.json
```

Check this file if you suspect messages were lost during a long offline period:

```text
sdcat /log/crashes.txt
```

## Home Assistant Dashboard

The wh_001 node has a full Lovelace dashboard deployed at:

```
http://100.67.244.37:8123/wh-001
```

Source YAML: `docs/LOVELACE_DASHBOARD.yaml` — this is the source of truth. To redeploy after edits, run from the project root:

```bash
python3 << 'EOF'
import asyncio, json, yaml, websockets

TOKEN = open(os.path.expanduser("~/.twwp/INFRASTRUCTURE.md")).read()
# Extract token from INFRASTRUCTURE.md and paste below:
TOKEN = "YOUR_LONG_LIVED_TOKEN"

async def main():
    async with websockets.connect("ws://100.67.244.37:8123/api/websocket") as ws:
        await ws.recv()
        await ws.send(json.dumps({"type": "auth", "access_token": TOKEN}))
        await ws.recv()
        config = yaml.safe_load(open("docs/LOVELACE_DASHBOARD.yaml"))
        await ws.send(json.dumps({"id": 1, "type": "lovelace/config/save", "url_path": "wh-001", "config": config}))
        print(await ws.recv())

asyncio.run(main())
EOF
```

### Dashboard views

| View | Contents |
|---|---|
| **Overview** | Live flow rates (RO output + input), today totals, leak/valve status, battery %, WiFi signal |
| **Flow Data** | Both channels: current rate + today/week/month/year/lifetime total + reset buttons (today/week/month/year/all) |
| **Sessions** | flex-table-card last 10 sessions table, last session summary, session enable switch, idle timeout + flow threshold sliders |
| **System** | Network (SSID/IP/signal/uptime/restart-wifi), power + battery calibration, K factors, OTA state, factory reset |

### Custom frontend resource

`flex-table-card` v1.4 is installed at:

```
/home/kenny/projects/homeassistant/config/www/flex-table-card.js
```

It is registered as a Lovelace module resource (`/local/flex-table-card.js`). Do not delete this file — the Sessions view depends on it.

---

## Monitoring Stack — InfluxDB + Grafana

Long-term time-series analytics for TWWP. Running on the Hetzner VPS alongside HA and Mosquitto.

**Status: LIVE** (deployed 2026-05-05). InfluxDB receiving data. Grafana accessible.

**Local project:** `/home/kenny/twwp-monitoring/`
**Server location:** `/home/kenny/projects/twwp-monitoring/`

### Access Grafana

```
http://100.67.244.37:3000
```

Tailscale only — not publicly exposed. Login: `admin` / password in `.env` on server (`GRAFANA_ADMIN_PASSWORD`).

### HA → InfluxDB integration

**Managed entirely via the HA UI** — Settings → Integrations → InfluxDB → `twwp_ha (http://localhost:8181)`.

Connection settings (URL, token, org, bucket) live in the HA UI config entry only.

> ⚠️ **DO NOT** add `influxdb:` to `configuration.yaml`. Even with only `include:` entities listed, the YAML key silently fails schema validation and blocks the entire InfluxDB component from loading — including the UI config entry. No errors are logged; data just stops. The fix is to remove the line entirely.

The `ha-config/influxdb.yaml` file in the local twwp-monitoring repo is **documentation only**. It must not be referenced from `configuration.yaml`.

### What gets written to InfluxDB

All HA entity state changes are written (no entity filter active — all entities go in). TWWP entities written include:
- Flow rates and totals (both channels), today/week/month/year subtotals
- Leak state, valve state
- Battery voltage + %, charge state
- WiFi RSSI
- Session data (last volume, duration)

The M5 firmware publishes 12 water quality entities (`wq_pre_ro_*`, `wq_post_ro_*`, `wq_remin_*`). HA writes them to InfluxDB as soon as MQTT discovery publishes and a meter returns valid data.

### Water quality zones (M5)

Three YiErYi RS485-3177 sensors: pre-RO filter, post-RO filter, remineralised. HA entity naming locked in:

```
sensor.wh_001_wq_pre_ro_ph / orp / ec / tds_ppm / temp / humidity
sensor.wh_001_wq_post_ro_ph / orp / ec / tds_ppm / temp / humidity
sensor.wh_001_wq_remin_ph / orp / ec / tds_ppm / temp / humidity
```

Default firmware enables one meter only: pre-RO at Modbus address `1`, 9600 8N1, using the Waveshare onboard RS485 port (GPIO17 TX / GPIO18 RX / GPIO21 auto DE/RE). Additional zones stay disabled until their Modbus addresses are confirmed.

---

## Water Quality Meter — Wiring and First Connection

### Hardware connection

The YiErYi RS485-3177 meter connects to the Waveshare board RS485 terminal block:

```
Meter A  →  Board terminal A
Meter B  →  Board terminal B
Meter GND → Board terminal GND
Meter power → AC adapter (NOT from the ESP32)
```

**A/B polarity matters.** If swapped, the driver will receive no bytes and report `read timeout`. Swapping one end fixes it immediately. Symptom of swapped A/B: `wq_<zone>_online: false`, `wq_<zone>_last_error: read timeout`, `wq_<zone>_raw_hex: ""` (empty), fail count incrementing.

### First connection checklist

1. Power the meter from its AC adapter. Do not use the ESP32 for meter power.
2. Connect A→A, B→B, GND→GND on the terminal block.
3. Confirm the meter's Modbus address is `1` (factory default). If using the vendor PC software (3178.exe) to check or change the address, connect via the USB-RS485 adapter (not the Waveshare board).
4. Open the serial monitor: `pio device monitor` and run `wq_status`.
5. Within 15–30 seconds you should see `online=1`, a non-empty `raw=` hex string, and parsed pH/ORP/EC/temp values.
6. Alternatively, check the MQTT status payload — `wq_pre_ro_online: True` and `wq_pre_ro_last_error: ok`.

### Confirming a good frame

A valid 16-byte frame looks like:
```
01 03 00 08 | [EC_H EC_L] [pH/ORP_H pH/ORP_L] [HUM_H HUM_L] [TMP_H TMP_L] | [RSV RSV] | [CRC_L CRC_H]
```

Example (tap water, pH mode):
```
01 03 00 08 00 52 02 B5 00 5A 00 B6 xx xx CRC CRC
              ^EC=82  ^pH=693→6.93  ^Hum=90% ^Tmp=182→18.2°C
```

### Register decoding reference

| Bytes | Field | Decoding |
|---|---|---|
| 4–5 | EC | uint16, µS/cm (direct) |
| 6–7 | pH or ORP | pH mode: uint16 ÷ 100.0 = pH; ORP mode: bit-15 sign flag (see below) |
| 8–9 | Humidity | uint16, % (direct) — internal meter PCB sensor |
| 10–11 | Temperature | signed int16 ÷ 10.0 = °C |
| 12–13 | Reserve | Ignore — vendor-specific bytes |
| 14–15 | CRC | Modbus CRC-16, little-endian |

**ORP encoding — bit-15 sign convention (not standard int16):**
The 3177 encodes ORP as: bit 15 = sign flag (1=positive, 0=negative), bits 14:0 = magnitude in mV.

Example: `0x81E0` → bit15=1 → positive, magnitude=`0x01E0`=480 → ORP = +480 mV.
Example: `0x0096` → bit15=0 → negative, magnitude=`0x0096`=150 → ORP = -150 mV.

Note: ORP can drift 10–30 mV between the meter display update and the Modbus read — small discrepancies between the display and MQTT are normal.

**TDS:** calculated as `EC (µS/cm) × 0.5` (standard KCl approximation). Confirmed accurate against the meter display. Published as `wq_<zone>_tds_ppm`.

**Humidity:** this is an internal PCB humidity sensor inside the meter enclosure. It does not measure water. Useful for detecting condensation inside the meter — values around 60–95% are normal. No divisor applied; value is direct percent.

---

## Water Quality Status Fields

All per-zone water quality fields in the MQTT heartbeat (`twwp/<node_id>/status`):

| Field | Type | Meaning |
|---|---|---|
| `wq_<zone>_ph` | number or null | pH reading (2 decimal places). Null if offline or stale (>60s since last read). |
| `wq_<zone>_orp` | number or null | ORP in mV. Null if offline or stale. ORP is read in alternating poll cycles with pH. |
| `wq_<zone>_ec` | number or null | Electrical conductivity in µS/cm. Null if offline or stale. |
| `wq_<zone>_tds_ppm` | number or null | TDS in ppm (EC × 0.5). Null if EC is null. |
| `wq_<zone>_temp` | number or null | Water temperature in °C (1 decimal place). Null if offline or stale. |
| `wq_<zone>_humidity` | number or null | Internal meter PCB humidity %. Null if offline or stale. |
| `wq_<zone>_online` | bool | `true` if a CRC-valid read was received within the last 60 seconds. |
| `wq_<zone>_fail_count` | number | Total consecutive failed reads since last boot. Resets on success. |
| `wq_<zone>_last_error` | string | `"ok"` on success. Error message on failure: `"read timeout"`, `"read crc mismatch"`, `"disabled"`. |
| `wq_<zone>_raw_hex` | string | Last successfully parsed Modbus frame as hex bytes. Empty on first boot before any successful read. |
| `wq_<zone>_ph_cal_date` | string | ISO date of last pH calibration (e.g. `"2026-05-08"`). Empty if never recorded. |
| `wq_<zone>_orp_cal_date` | string | ISO date of last ORP calibration. |
| `wq_<zone>_ec_cal_date` | string | ISO date of last EC calibration. |

Zone suffixes: `pre_ro`, `post_ro`, `remin`.

---

## Water Quality Meter — Physical Calibration

### How calibration works

The 3177 meter applies calibration internally. When you calibrate using standard solutions and the meter's physical buttons, the meter recalculates its slope/offset and stores it in its own flash. From that point, all Modbus reads return the calibrated value — the firmware receives the corrected reading automatically, with no firmware change or MQTT command needed.

### Calibration procedure

1. **pH calibration** (2-point recommended — Asia standard solutions): use pH 4.00 and pH 7.00 buffer solutions. Follow the meter's physical calibration menu. After calibration, verify the reading against a fresh buffer — the meter's display should match within ±0.05 pH.
2. **ORP calibration**: immerse the ORP electrode in the ORP standard solution (typically Zobell solution ≈ +220 mV or Quinhydrone). Follow the meter menu.
3. **EC calibration**: use a KCl standard solution of known conductivity (e.g. 1413 µS/cm). Follow the meter menu.

After calibrating, compare the meter display to the `wq_pre_ro_*` fields in the MQTT status. If the values match (within ORP drift tolerance), calibration was successful.

### Recording calibration dates via MQTT

After each physical calibration, publish the date to the MQTT command channel. This date is stored in `/config/node.json` on the SD card and published in all subsequent status payloads.

```bash
mosquitto_pub \
  -h twwp-iot.duckdns.org -p 8883 \
  --capath /etc/ssl/certs \
  -u twwp_wh_001 -P <MQTT_PASS> \
  -t 'twwp/wh_001/cmd' \
  -m '{"set_wq_pre_ro_ph_cal_date": "2026-05-08"}'
```

To record multiple calibrations at once:

```json
{
  "set_wq_pre_ro_ph_cal_date": "2026-05-08",
  "set_wq_pre_ro_orp_cal_date": "2026-05-08",
  "set_wq_pre_ro_ec_cal_date": "2026-05-08"
}
```

Replace `pre_ro` with `post_ro` or `remin` for the other zones.

**Calibration date MQTT cmd keys:**

| Key | Zone | Parameter |
|---|---|---|
| `set_wq_pre_ro_ph_cal_date` | Pre-RO | pH calibration date |
| `set_wq_pre_ro_orp_cal_date` | Pre-RO | ORP calibration date |
| `set_wq_pre_ro_ec_cal_date` | Pre-RO | EC calibration date |
| `set_wq_post_ro_ph_cal_date` | Post-RO | pH calibration date |
| `set_wq_post_ro_orp_cal_date` | Post-RO | ORP calibration date |
| `set_wq_post_ro_ec_cal_date` | Post-RO | EC calibration date |
| `set_wq_remin_ph_cal_date` | Remineralised | pH calibration date |
| `set_wq_remin_orp_cal_date` | Remineralised | ORP calibration date |
| `set_wq_remin_ec_cal_date` | Remineralised | EC calibration date |

The value is a free-form string — ISO date format (`YYYY-MM-DD`) is recommended.

To inspect stored calibration dates:

```text
sdcat /config/node.json
```

Or check the MQTT status payload — calibration date fields are always present (empty string `""` if never recorded).

---

## Water Quality Meter — node.json Configuration

Optional runtime config in `/config/node.json` under the `water_quality` key:

```json
{
  "water_quality": {
    "poll_interval_ms": 15000,
    "read_orp": true,
    "pre_ro":  { "enabled": true,  "address": 1, "read_orp": true },
    "post_ro": { "enabled": false, "address": 2, "read_orp": true },
    "remin":   { "enabled": false, "address": 3, "read_orp": true }
  }
}
```

| Field | Meaning | Default |
|---|---|---|
| `poll_interval_ms` | Time between Modbus poll cycles in ms. Minimum 5000. | 15000 |
| `read_orp` | Global default — read ORP (alternating with pH). Per-zone override available. | true |
| `<zone>.enabled` | Whether this zone is polled at all. | pre_ro: true, others: false |
| `<zone>.address` | Modbus slave address (1–247). Must match address set on the physical meter. | 1, 2, 3 |
| `<zone>.read_orp` | Per-zone ORP read enable (overrides global `read_orp`). | inherits global |

**Enabling additional zones:** first confirm that each meter has a unique Modbus address using the vendor PC software (3178.exe via USB-RS485 adapter). Then update `node.json` and reboot the device. No reflash needed.

---

## Dual EC/TDS Meter

A standalone ESP32-WROOM-32 + ADS1115 EC/TDS meter (separate device) transmits unsolicited ASCII frames over the **same RS485 bus** as the YiErYi water quality meters every ~3 seconds:

```
$WM,<temp1>,<ec1>,<ppm1>,<temp2>,<ec2>,<ppm2>\r\n
```

Probe 1 = pre-RO zone, Probe 2 = post-RO zone. The firmware multiplexes both protocols on UART1 without polling — frames are dispatched by first byte (`$` for ASCII, `0x01` for Modbus).

### Wiring

The EC/TDS meter's RS485 A/B terminals connect to the **same Waveshare board RS485 terminal block** as the YiErYi meter. No additional wiring to the board — extend the existing A/B bus.

| EC/TDS meter terminal | RS485 bus |
|---|---|
| A | A (with YiErYi A) |
| B | B (with YiErYi B) |
| GND | Shared GND |

**RS485 module pin naming (WROOM-32 side):** The module has `DI` (Data In) and `RO` (Receiver Output) pins — these are the UART side, not the A/B bus side.
- `DI` = data going **into** the module to transmit → connects to **TX** of the WROOM-32
- `RO` = received data **out** of the module → connects to **RX** of the WROOM-32

Swapping DI and RO is a common wiring mistake. Symptom: no `[TDS]` frames in serial monitor despite correct A/B polarity.

### MQTT status fields

All TDS meter fields appear in `twwp/<node_id>/status` alongside `wq_*` fields:

| Field | Type | Notes |
|---|---|---|
| `tds_pre_ro_ec` | float or null | Pre-RO EC in µS/cm. Null if offline or stale (>60s). |
| `tds_pre_ro_temp` | float or null | Pre-RO water temperature in °C (1 decimal). |
| `tds_pre_ro_ppm` | float or null | Pre-RO TDS in ppm. |
| `tds_pre_ro_online` | bool | `true` if a valid frame was received within the last 60s. |
| `tds_pre_ro_fail_count` | int | Parse failure counter since boot. |
| `tds_pre_ro_last_error` | string | `""` on success; `"bad frame"` on parse failure. |
| `tds_post_ro_ec` | float or null | Post-RO EC in µS/cm. |
| `tds_post_ro_temp` | float or null | Post-RO water temperature in °C. |
| `tds_post_ro_ppm` | float or null | Post-RO TDS in ppm. |
| `tds_post_ro_online` | bool | `true` if a valid frame received within 60s. |
| `tds_post_ro_fail_count` | int | Parse failure counter. |
| `tds_post_ro_last_error` | string | `""` on success; `"bad frame"` on parse failure. |

### HA entities (6 total)

Auto-discovered via MQTT HA discovery on first boot after the EC/TDS meter is connected:

- `sensor.wh_001_tds_pre_ro_ec` (µS/cm)
- `sensor.wh_001_tds_pre_ro_temp` (°C, device_class: temperature)
- `sensor.wh_001_tds_pre_ro_ppm` (ppm)
- `sensor.wh_001_tds_post_ro_ec` (µS/cm)
- `sensor.wh_001_tds_post_ro_temp` (°C, device_class: temperature)
- `sensor.wh_001_tds_post_ro_ppm` (ppm)

### SD CSV columns

Six new columns are appended to the daily log after existing water quality columns:

```
tds_pre_ro_ec, tds_pre_ro_temp, tds_pre_ro_ppm, tds_post_ro_ec, tds_post_ro_temp, tds_post_ro_ppm
```

### Boot log output

```
[MUX] RS485 UART1 ready
[TDS] EC/TDS meter driver ready
...
[TDS] P1: 28.5°C  EC=412 µS/cm  TDS=206 ppm | P2: 28.1°C  EC=18 µS/cm  TDS=9 ppm
```

`P1` and `P2` lines appear in the serial monitor every ~3 seconds when the EC/TDS meter is powered on the bus.

### Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `tds_pre_ro_online: false` after 60s | Meter not powered or A/B swapped | Check power; swap A/B on one end |
| No `[TDS]` frames despite correct A/B | DI and RO swapped on WROOM-32 RS485 module | Swap the two UART wires on the module side (DI↔RO) |
| `fail_count` incrementing | Frame parse failure — baud rate mismatch or noise | EC/TDS meter must transmit at 9600 baud; check cable length |
| `tds_*` fields absent from MQTT status | Firmware older than TDS meter integration | Reflash firmware |
| TDS ppm reads low compared to reference meter | Default EC×0.5 conversion factor — uncalibrated probes | The conversion factor is fixed in firmware; absolute accuracy requires the EC/TDS meter to be calibrated using the vendor software with known standard solutions. P1 and P2 readings relative to each other are accurate even without calibration. |

---

## Water Quality Meter — Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `online: false`, `last_error: read timeout`, `raw_hex: ""` | A/B polarity swapped or no power to meter | Swap A and B on one end of the cable; confirm meter AC adapter is plugged in |
| `online: false`, `last_error: read crc mismatch`, `raw_hex` has bytes | Modbus address mismatch, or electrical noise | Confirm meter address via vendor software; shorten RS485 cable; check GND connection |
| `online: true` but pH is 300+ or obviously wrong | Meter stuck in wrong register mode | Run serial `wq_poll` to force a fresh cycle; power-cycle the meter |
| ORP differs from meter display by >50 mV | Normal ORP drift between read time and display check; or ORP electrode needs cleaning/recalibration | Acceptable if difference <100 mV. If consistently high offset, recalibrate ORP electrode |
| All three zones show `disabled` | `water_quality` section missing from `node.json` or parse error | Check `sdcat /config/node.json` for syntax errors; pre_ro defaults to enabled so this is unusual |
| `wq_status` serial command shows correct values but MQTT fields are null | Staleness watchdog — last valid read was >60s ago | Check poll cycle is running (look for `fail_count` incrementing). Meter may have been briefly disconnected. |

USB serial diagnostic commands:

```text
wq_status   # print all zones: address, enabled, online, fail_count, last_error, raw_hex, parsed values
wq_poll     # force an immediate poll cycle (next loop iteration)
```

### Verify InfluxDB is receiving data

Open Grafana → Explore → InfluxDB-TWWP → click "select measurement". If TWWP measurements appear in the dropdown, data is flowing.

Or via SSH:
```bash
ssh kenny@100.67.244.37
docker logs twwp-influxdb --since 5m 2>&1 | grep write
```

### Update the stack

```bash
ssh kenny@100.67.244.37
cd /home/kenny/projects/twwp-monitoring
git pull && docker compose pull && docker compose up -d
```

### InfluxDB token

Token is stored in:
- Server: `/home/kenny/projects/twwp-monitoring/.env` → `INFLUXDB_TOKEN`
- HA: `/home/kenny/projects/homeassistant/config/secrets.yaml` → `influxdb_token`
- Grafana: auto-injected at startup via `INFLUXDB_TOKEN` env var

---

## AI Tool Access — Credentials and Server Updates

Claude Code, Codex, and Roo Code can all make direct changes to the HA server and firmware infrastructure via SSH, provided the credentials below are available at session start.

### What each tool can do with SSH access

| Tool | Firmware | HA server (SSH) | HA UI |
|---|---|---|---|
| Claude Code | Build, flash, serial monitor | SSH in, edit configs, restart containers | Via browser if given a long-lived token |
| Codex | Build, flash | SSH in, edit configs | Not directly |
| Roo Code | Build, flash, serial monitor | SSH in, edit configs | Not directly |

### HA server SSH credentials

- **Host:** `100.67.244.37` (Tailscale IP)
- **User:** `kenny`
- **Auth:** SSH key — key is already loaded on this machine, no password needed
- **Sudo password:** stored in `~/.twwp/INFRASTRUCTURE.md` (never in git)

The SSH key auth works without a password. The sudo password is only needed for privileged operations (e.g. restarting Docker containers or editing system config files).

### How an AI tool accesses the server

1. The tool runs `ssh kenny@100.67.244.37 '<command>'` directly — no interactive session needed.
2. For Docker/container restarts: `ssh kenny@100.67.244.37 'echo <sudo_pass> | sudo -S docker restart homeassistant'`
3. For HA config edits: SSH into the server, edit files under `/home/kenny/projects/homeassistant/`, then restart the container.

### Long-lived HA API token

A long-lived HA token is stored in `~/.twwp/INFRASTRUCTURE.md` under the Home Assistant section. It allows tools to call the HA REST API directly — read states, trigger services, reload config — without SSH.

Example usage:

```bash
curl -s -H "Authorization: Bearer <token>" http://100.67.244.37:8123/api/states | jq '.[].entity_id'
```

### What to hand the AI at session start

Tell the tool:
> "SSH to kenny@100.67.244.37 — key auth works. Sudo password is in ~/.twwp/INFRASTRUCTURE.md."

That is sufficient for full server access. No other credential setup is needed unless the sudo password has changed.

---

## Useful Troubleshooting Notes

### Serial monitor JSON truncation

The M0 status heartbeat JSON is long (~800 bytes). When the USB CDC (HWCDC) write buffer fills faster than the PC can drain it, the ESP32 prints:

```
[HWCDC.cpp:467] write(): write failed due to waiting USB Host - timeout
```

The JSON is cut off mid-field at that point. This is **cosmetic only** — MQTT publishing over WiFi is completely unaffected. The full JSON payload is always visible via `mosquitto_sub -t 'twwp/wh_001/status'`.

The TDS and water quality fields appear near the end of the JSON and are most likely to be cut off in the serial monitor.

### `[SERIAL] console connected` appearing repeatedly

If you see this line every minute or so, it means the USB CDC connection is dropping and reconnecting — a side-effect of the HWCDC write timeout above. The firmware continues running normally; only the debug serial is affected.

If serial commands appear to do nothing, press Enter once more. The firmware accepts both CR and LF line endings.

If a command appears with a `!:` prefix from PlatformIO, the firmware strips that prefix before parsing.

If the RTC is invalid, log writes use:

```text
/log/unsynced.csv
```

If the old file appears:

```text
/log/2000-00-00.csv
```

it is a harmless pre-fix artifact and can be deleted with:

```text
sdrm /log/2000-00-00.csv
```
