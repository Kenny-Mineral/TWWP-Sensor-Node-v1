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
[MQTT] connected
[MQTT] HA discovery published
[M0] status ... "ts":<non-zero>
```

## SD Serial Commands

Open the serial monitor:

```bash
pio device monitor
```

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
| `session_last_id` | number | ID of the most recently completed session. |
| `session_last_start_ts` | number | Unix timestamp when the last session started. |
| `session_last_end_ts` | number | Unix timestamp when the last session ended. |
| `session_last_dur_s` | number | Duration of the last session in seconds. |
| `session_last_vol_out` | number | Volume through RO Output (sensor 1) during last session, in litres. |
| `session_last_vol_in` | number | Volume through RO Input (sensor 2) during last session, in litres. |

Note: period subtotals (today/week/month/year) reset correctly across RTC date boundaries. If the node is powered off across a boundary, the subtotals resume from the saved value for that day but reset on the next boundary. The lifetime total (`flow_total_*`) always survives reboots — it is persisted to `/config/flow_total.json` on the SD card every 60 seconds.

## Time-Series Data Log

Every 60 seconds the node appends a row to:

```text
/data/YYYY-MM-DD.csv
```

Each file starts with a header row on creation:

```text
ts,flow_rate_1,flow_total_1,flow_today_1,flow_rate_2,flow_total_2,flow_today_2,leak
```

Example rows:

```text
1745700000,1.234,456.789,5.234,0.000,123.456,0.000,0
1745700060,1.189,457.978,6.423,0.000,123.456,0.000,0
```

Units: `ts` = Unix timestamp, flow values = litres, `leak` = 0 or 1.

The data log writes regardless of WiFi or MQTT status — it is the offline record. New columns will be added as sensors come online (pressure, water quality). Existing files keep their original columns.

View or export via serial:

```text
sdls /data
sdcat /data/2026-04-27.csv
```

Pruning applies to `/data/` the same as `/log/` — files older than `sd.retention_days` are removed by `sdprune`.

## HA Diagnostic Entities

The following diagnostic entities appear automatically on the device card in HA:

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

A "session" is a continuous usage event at the RO tap. A session starts when flow on either sensor exceeds 0.05 L/min and ends 90 seconds after flow stops on both sensors. If flow resumes within the 90-second window, it extends the same session.

When a session ends, the node publishes to `twwp/<node_id>/session` and appends a row to:

```
/log/sessions.csv
```

Session log columns:

```
session_id,start_ts,end_ts,duration_s,volume_out_L,volume_in_L,peak_rate_out,peak_rate_in
```

View via serial:

```
sdcat /log/sessions.csv
```

The last session fields also appear in the heartbeat status (`session_last_id`, `session_last_dur_s`, `session_last_vol_out`, `session_last_vol_in`) and are visible as diagnostic sensors on the main TWWP device card in HA.

Session IDs survive reboots (persisted in NVS).

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

## Useful Troubleshooting Notes

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
