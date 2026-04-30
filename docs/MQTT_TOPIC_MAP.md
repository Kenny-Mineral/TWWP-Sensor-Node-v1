# MQTT Topic Map

All topics use `<id>` = the value of `NODE_ID` from `include/secrets.h` (e.g. `wh_001`).

QoS is 0 for all topics. Retain and direction as noted.

---

## Data topics

| Topic | Retain | Direction | Description |
|---|---|---|---|
| `twwp/<id>/status` | yes | node → broker | Heartbeat JSON published every 10 s and on MQTT reconnect. Contains all sensor readings, network info, and counters. |
| `twwp/<id>/alert` | no | node → broker | Leak state-change events (`LEAK_STATE` type). |
| `twwp/<id>/log` | no | node → broker | SD failure notifications (rate-limited 1/min). |
| `twwp/<id>/lwt` | yes | node → broker | Last-will / availability: `online` when connected, broker publishes `offline` on unexpected disconnect. |
| `twwp/<id>/session` | no | node → broker | Session-end event. Published when the idle timeout expires. JSON payload with session_id, start_ts, end_ts, duration_s, volume_out_L, volume_in_L, peak_rate_out, peak_rate_in. |
| `twwp/<id>/sessions_recent` | yes | node → broker | Retained JSON array of the last 10 sessions (newest-first). Republished after each session ends and on MQTT reconnect. |
| `twwp/<id>/cmd` | no | broker → node | Command channel. Parsed in firmware (actuator commands in M3, OTA in M4). |
| `twwp/<id>/ota_state` | yes | node → broker | Dedicated OTA state topic for high-frequency OTA progress/status updates. Reserved for OTA telemetry. |
| `twwp/register` | no | node → broker | First-connect registration payload (M8 — not yet implemented). |

---

## Home Assistant auto-discovery topics

All discovery payloads are JSON, retain = yes. HA reads these once and creates entities automatically.

### Leak sensor

| Topic | Entity type | HA entity |
|---|---|---|
| `homeassistant/binary_sensor/twwp_<id>_leak/config` | binary_sensor | Moisture — `ON` when wet |

### Flow sensors (M1)

| Topic | Entity type | Unit | HA state class |
|---|---|---|---|
| `homeassistant/sensor/twwp_<id>_flow_rate_1/config` | sensor | L/min | measurement |
| `homeassistant/sensor/twwp_<id>_flow_rate_2/config` | sensor | L/min | measurement |
| `homeassistant/sensor/twwp_<id>_flow_total_1/config` | sensor | L | total_increasing |
| `homeassistant/sensor/twwp_<id>_flow_total_2/config` | sensor | L | total_increasing |
| `homeassistant/sensor/twwp_<id>_flow_today_1/config` | sensor | L | measurement |
| `homeassistant/sensor/twwp_<id>_flow_today_2/config` | sensor | L | measurement |
| `homeassistant/sensor/twwp_<id>_flow_week_1/config` | sensor | L | measurement |
| `homeassistant/sensor/twwp_<id>_flow_week_2/config` | sensor | L | measurement |
| `homeassistant/sensor/twwp_<id>_flow_month_1/config` | sensor | L | measurement |
| `homeassistant/sensor/twwp_<id>_flow_month_2/config` | sensor | L | measurement |
| `homeassistant/sensor/twwp_<id>_flow_year_1/config` | sensor | L | measurement |
| `homeassistant/sensor/twwp_<id>_flow_year_2/config` | sensor | L | measurement |
| `homeassistant/sensor/twwp_<id>_pulses_raw_1/config` | sensor (diagnostic) | pulses | total_increasing |
| `homeassistant/sensor/twwp_<id>_pulses_raw_2/config` | sensor (diagnostic) | pulses | total_increasing |
| `homeassistant/sensor/twwp_<id>_k_applied_1/config` | sensor (diagnostic) | pulses/L | measurement |
| `homeassistant/sensor/twwp_<id>_k_applied_2/config` | sensor (diagnostic) | pulses/L | measurement |
| `homeassistant/sensor/twwp_<id>_flow_avg_window_1/config` | sensor (diagnostic) | L/min | measurement |
| `homeassistant/sensor/twwp_<id>_flow_avg_window_2/config` | sensor (diagnostic) | L/min | measurement |

All flow discovery payloads set `state_topic` to `twwp/<id>/status` and extract their value via `value_template`.

---

### K factor (writable, M1)

K factor entities are `number` entities — the user can change them directly in HA. When changed, HA publishes to `twwp/<id>/cmd` and the firmware updates the value in RAM and saves it to `/config/node.json` on the SD card.

| Topic | Entity type | Unit | Range |
|---|---|---|---|
| `homeassistant/number/twwp_<id>_k_factor_1/config` | number | pulses/L | 1–99999 |
| `homeassistant/number/twwp_<id>_k_factor_2/config` | number | pulses/L | 1–99999 |

---

### K-table (writable, M4)

K-table entities are `text` entities that accept a JSON array string. When changed, HA publishes `{"set_k_table_1": <value>}` (or `_2`) to `twwp/<id>/cmd` and the firmware updates the interpolation table in RAM and persists to `/config/node.json`.

| Topic | Entity type | Description |
|---|---|---|
| `homeassistant/text/twwp_<id>_k_table_1/config` | text | Calibration points for channel 1 (RO Output) |
| `homeassistant/text/twwp_<id>_k_table_2/config` | text | Calibration points for channel 2 (RO Input) |

Each value is a JSON array of `{"flow_lpm": <float>, "k": <float>}` objects, e.g.:

```json
[{"flow_lpm": 0, "k": 5500}, {"flow_lpm": 2, "k": 5300}]
```

---

### Debounce (writable, M4)

Debounce entities are `number` entities. When changed, HA publishes `{"set_debounce_us_1": <value>}` (or `_2`) to `twwp/<id>/cmd` and the firmware updates the debounce timer in RAM and persists to `/config/node.json`.

| Topic | Entity type | Unit | Range | Step |
|---|---|---|---|---|
| `homeassistant/number/twwp_<id>_debounce_us_1/config` | number | µs | 100–10000 | 100 |
| `homeassistant/number/twwp_<id>_debounce_us_2/config` | number | µs | 100–10000 | 100 |

---

### Flow average window (writable, M4)

A `number` entity that controls the moving-average window size (shared across both flow channels). When changed, HA publishes `{"set_flow_avg_window": <value>}` to `twwp/<id>/cmd`.

| Topic | Entity type | Unit | Range | Step |
|---|---|---|---|---|
| `homeassistant/number/twwp_<id>_flow_avg_window/config` | number | samples | 1–20 | 1 |

---

### OTA diagnostics (M4)

OTA discovery entities are read-only diagnostic sensors backed by [`twwp/<id>/status`](docs/MQTT_TOPIC_MAP.md).

| Topic | Entity type | Unit | HA state class |
|---|---|---|---|
| `homeassistant/sensor/twwp_<id>_ota_state/config` | sensor (diagnostic) |  | measurement |
| `homeassistant/sensor/twwp_<id>_ota_progress/config` | sensor (diagnostic) | % | measurement |

Mapped status fields:

| Status field | Meaning |
|---|---|
| `ota_state` | OTA state enum: `0=IDLE`, `1=DOWNLOADING`, `2=VERIFYING`, `3=APPLYING`, `4=SUCCESS`, `5=FAILED` |
| `ota_progress_pct` | OTA download progress percentage |
| `ota_error` | Last OTA failure string, if present |

---

## Command topic payload format

The node subscribes to `twwp/<id>/cmd`. Payload must be valid JSON. Supported keys:

| Key | Type | Effect |
|---|---|---|
| `set_k_factor_1` | int | Set K factor for flow channel 1. Saved to `node.json`. |
| `set_k_factor_2` | int | Set K factor for flow channel 2. Saved to `node.json`. |
| `reset_flow_today_1` | bool | Zero today subtotal only for channel 1. Saved to SD. |
| `reset_flow_today_2` | bool | Zero today subtotal only for channel 2. Saved to SD. |
| `reset_flow_today` | bool | Zero today subtotal only for both channels. |
| `reset_flow_week_1` | bool | Zero this-week subtotal only for channel 1. |
| `reset_flow_week_2` | bool | Zero this-week subtotal only for channel 2. |
| `reset_flow_week` | bool | Zero this-week subtotal only for both channels. |
| `reset_flow_month_1` | bool | Zero this-month subtotal only for channel 1. |
| `reset_flow_month_2` | bool | Zero this-month subtotal only for channel 2. |
| `reset_flow_month` | bool | Zero this-month subtotal only for both channels. |
| `reset_flow_year_1` | bool | Zero this-year subtotal only for channel 1. |
| `reset_flow_year_2` | bool | Zero this-year subtotal only for channel 2. |
| `reset_flow_year` | bool | Zero this-year subtotal only for both channels. |
| `reset_flow_totals_1` | bool | Zero lifetime total + all subtotals for channel 1. Clears NVS + SD. |
| `reset_flow_totals_2` | bool | Zero lifetime total + all subtotals for channel 2. Clears NVS + SD. |
| `reset_flow_totals` | bool | Zero all flow data for both channels. Clears NVS + SD. |
| `factory_reset_flow` | bool | Wipe all flow data (both channels, all periods) + reset session ID. Full NVS clear. |
| `set_session_enabled` | bool | Enable (`true`) or disable (`false`) session tracking. Persisted to NVS. |
| `set_session_idle_timeout` | int (5–100) | Idle gap in seconds before a session is finalised. Persisted to NVS. |
| `set_flow_threshold` | float (0.01–0.5) | Flow detection threshold in L/min. Flow below this is treated as off (and flagged as leak suspect if non-zero). Persisted to NVS. |
| `set_k_table_1` | string | JSON array of calibration points for channel 1. Persisted to `node.json`. Example: `[{"flow_lpm":0,"k":5500},{"flow_lpm":2,"k":5300}]` |
| `set_k_table_2` | string | JSON array of calibration points for channel 2. Persisted to `node.json`. |
| `set_debounce_us_1` | int (100–10000) | Debounce period in microseconds for channel 1. Persisted to `node.json`. |
| `set_debounce_us_2` | int (100–10000) | Debounce period in microseconds for channel 2. Persisted to `node.json`. |
| `set_flow_avg_window` | int (1–20) | Moving average window size (shared across both channels). Persisted to `node.json`. |
| `restart_wifi` | bool | Trigger WiFi reconnect. |
| `ota_url` | string | Start HTTPS OTA update from the given firmware URL. |
| `ota_md5` | string | Optional expected MD5 hash for the firmware image. Used only with `ota_url`. |

Example (sent automatically by HA when user changes the K Factor 1 number entity to 200):

```json
{"set_k_factor_1": 200}
```

Multiple keys can be set in one message. Unknown keys are silently ignored.

---

## Notes

- All discovery topics for `twwp/<id>/status`-backed entities use `availability_topic: twwp/<id>/lwt`.
- `flow_total_*` uses `device_class: water` — HA tracks these in the Energy dashboard.
- `flow_rate_*` uses `device_class: volume_flow_rate`.
- Period subtotals (today/week/month/year) have no device class — they reset at boundaries and are not suitable for `total_increasing`.
- K-factor entities are writable `number` entities in HA — change them directly on the device card without touching the SD card or reflashing.
- K-table entities are writable `text` entities that accept a JSON array of calibration points — change them directly in HA.
- Debounce and flow average window entities are writable `number` entities — runtime-configurable without reflashing.
- All writable entities persist changes to `/config/node.json` on the SD card immediately.
- OTA status is exposed both in the regular retained status heartbeat and in the dedicated retained `twwp/<id>/ota_state` topic for future live-progress dashboards.
