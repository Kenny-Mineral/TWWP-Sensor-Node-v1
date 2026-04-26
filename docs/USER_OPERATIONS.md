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
