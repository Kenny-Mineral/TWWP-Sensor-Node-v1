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
| `twwp/<id>/session` | no | node → broker | Session-end event. Published when the idle timeout expires. JSON payload with session_id, start_ts, end_ts, duration_s, flow_duration_s, idle_time_s, volume_out_L, volume_in_L, peak_rate_out, peak_rate_in. |
| `twwp/<id>/sessions_recent` | yes | node → broker | Retained JSON array of the last 10 sessions (newest-first). Each session object includes: id, start_ts, end_ts, dur_s, flow_dur_s, idle_s, vol_out, vol_in, peak_out, peak_in. Republished after each session ends and on MQTT reconnect. |
| `twwp/<id>/cmd` | no | broker → node | Command channel. Parsed in firmware (actuator commands in M3, OTA in M4, upload-portal control in M-Upload). |
| `twwp/<id>/ota_state` | yes | node → broker | Dedicated OTA state topic for high-frequency OTA progress/status updates. Reserved for OTA telemetry. |
| `twwp/<id>/wq_config` | yes | node → broker | Retained JSON of all water quality threshold, label, and name config values. Published on MQTT connect and after any cmd change. |
| `twwp/register` | no | node → broker | First-connect registration payload (M8 — not yet implemented). |

---

## Upload portal command keys

These JSON keys are handled on `twwp/<id>/cmd`:

| Key | Type | Meaning |
|---|---|---|
| `start_ap` | bool | Starts or extends the local upload AP. Pair with `duration_s`. |
| `duration_s` | int | AP lifetime in seconds for `start_ap`. Constrained to 30–3600. |
| `rotate_upload_token` | bool | Rotates the local relay token stored at `/config/upload_token.json`. |

Upload-portal status fields added to `twwp/<id>/status`:

| Field | Type | Meaning |
|---|---|---|
| `ap_active` | bool | `true` while the local upload AP is active. |
| `ap_ssid` | string | Active upload AP SSID, normally `twwp-<node_id>`. |
| `ap_clients` | int | Number of phones currently associated to the upload AP. |
| `ap_expires_s` | int | Seconds remaining before the AP auto-stops. |
| `wifi_uptime_s` | int | Continuous STA WiFi uptime in seconds. |

The phone-facing upload portal itself is local HTTP on `http://192.168.4.1/`; it does not publish its own MQTT topic.

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

### Battery voltage monitor (M2.5)

Three read sensors and three writable config numbers. All sensors read from `twwp/<id>/status`.

| Topic | Entity type | Unit | Notes |
|---|---|---|---|
| `homeassistant/sensor/twwp_<id>_supply_voltage/config` | sensor | V | device_class=voltage, state_class=measurement |
| `homeassistant/sensor/twwp_<id>_supply_voltage_pct/config` | sensor | % | device_class=battery, state_class=measurement |
| `homeassistant/sensor/twwp_<id>_supply_voltage_divider/config` | sensor (diagnostic) | V | Raw ADS1115 divider-node voltage before the 4.0303x battery scaling |
| `homeassistant/sensor/twwp_<id>_supply_voltage_state/config` | sensor | — | "Charging" / "Discharging" / "Stable" |
| `homeassistant/number/twwp_<id>_voltage_v_min/config` | number (config) | V | Empty-battery threshold. Range 9–13, step 0.1. cmd key: `set_v_min` |
| `homeassistant/number/twwp_<id>_voltage_v_max/config` | number (config) | V | Full-battery threshold. Range 12–16, step 0.1. cmd key: `set_v_max` |
| `homeassistant/number/twwp_<id>_voltage_cal_factor/config` | number (config) | — | Calibration multiplier. Range 0.800–1.200, step 0.001. cmd key: `set_voltage_cal` |

---

### Valve relay (M3)

Binary sensor reporting the relay state (open = energised = LED/valve on).

| Topic | Entity type | Notes |
|---|---|---|
| `homeassistant/binary_sensor/twwp_<id>_valve_open/config` | binary_sensor | device_class=opening. `ON` when relay energised (valve open). |
| `homeassistant/select/twwp_<id>_valve_type/config` | select | Valve hardware type: `test` / `solenoid` / `ball_valve`. |
| `homeassistant/select/twwp_<id>_trigger_source/config` | select | What opens the valve: `flow` / `manual`. |
| `homeassistant/number/twwp_<id>_valve_idle_timeout/config` | number | Idle safety timeout in seconds (0 = off). |
| `homeassistant/number/twwp_<id>_valve_max_open/config` | number | Max-open safety timeout in seconds (0 = off). |
| `homeassistant/switch/twwp_<id>_valve_timeout_disable_auto/config` | switch | Disable auto mode after safety close. |
| `homeassistant/switch/twwp_<id>_valve_timeout_alert/config` | switch | Publish alert on safety close. |

Status fields added to `twwp/<id>/status`:

| Field | Type | Meaning |
|---|---|---|
| `valve_open` | bool | `true` = relay energised (valve open / LED on) |
| `valve_auto` | bool | `true` = flow-driven auto mode; `false` = manual MQTT override |
| `valve_type` | string | Active hardware type (`test` / `solenoid` / `ball_valve`) |
| `trigger_source` | string | Active trigger source (`flow` / `manual`) |
| `valve_idle_timeout_s` | int | Idle safety timeout in seconds (0 = disabled) |
| `valve_max_open_s` | int | Max-open safety timeout in seconds (0 = disabled) |
| `valve_timeout_disable_auto` | bool | Whether safety close also disables auto mode |
| `valve_timeout_alert` | bool | Whether safety close publishes an alert |

---

### Water quality — RS485-3177/3178 × 3 zones (M5)

Fields are published in `twwp/<id>/status`. Zone naming is locked in — InfluxDB schema and HA include list are already configured for these exact names.

| Status field | Type | Zone | Unit | Notes |
|---|---|---|---|---|
| `wq_pre_ro_ph` | float | Pre-RO filter | pH | 0–14 |
| `wq_pre_ro_orp` | int | Pre-RO filter | mV | ORP |
| `wq_pre_ro_ec` | float | Pre-RO filter | µS/cm | EC |
| `wq_pre_ro_temp` | float | Pre-RO filter | °C | Water temp |
| `wq_post_ro_ph` | float | Post-RO filter | pH | |
| `wq_post_ro_orp` | int | Post-RO filter | mV | |
| `wq_post_ro_ec` | float | Post-RO filter | µS/cm | |
| `wq_post_ro_temp` | float | Post-RO filter | °C | |
| `wq_remin_ph` | float | Remineralised | pH | |
| `wq_remin_orp` | int | Remineralised | mV | |
| `wq_remin_ec` | float | Remineralised | µS/cm | |
| `wq_remin_temp` | float | Remineralised | °C | |

Diagnostic fields are also published per zone:

| Status field | Type | Meaning |
|---|---|---|
| `wq_<zone>_humidity` | int/null | Meter humidity field from the vendor response |
| `wq_<zone>_online` | bool | `true` when a CRC-valid frame was received within 60s |
| `wq_<zone>_fail_count` | int | Read timeout / CRC failure counter |
| `wq_<zone>_last_error` | string | Last driver state (`ok`, `read timeout`, `read crc mismatch`, etc.) |
| `wq_<zone>_raw_hex` | string | Last accepted 16-byte Modbus response |

HA discovery topics (one per metric per zone):
`homeassistant/sensor/twwp_<id>_wq_<zone>_<metric>/config`

HA entities (12 total, e.g.):
- `sensor.wh_001_wq_pre_ro_ph`, `sensor.wh_001_wq_pre_ro_orp`, `sensor.wh_001_wq_pre_ro_ec`, `sensor.wh_001_wq_pre_ro_temp`
- `sensor.wh_001_wq_post_ro_ph`, ..., `sensor.wh_001_wq_post_ro_temp`
- `sensor.wh_001_wq_remin_ph`, ..., `sensor.wh_001_wq_remin_temp`

#### Dual EC/TDS meter (M6 — ASCII RS485)

Standalone ESP32+ADS1115 EC/TDS meter on same RS485 bus. Frames parsed by rs485_mux; no polling. Fields use `tds_` prefix to avoid collision with `wq_` fields above.

| Status field | Type | Zone | Unit | Notes |
|---|---|---|---|---|
| `tds_pre_ro_ec` | float/null | Pre-RO | µS/cm | Null if offline or stale >60s |
| `tds_pre_ro_temp` | float/null | Pre-RO | °C | 1 decimal place |
| `tds_pre_ro_ppm` | float/null | Pre-RO | ppm | |
| `tds_pre_ro_online` | bool | Pre-RO | — | `true` if frame received within 60s |
| `tds_pre_ro_fail_count` | int | Pre-RO | — | Parse failure counter |
| `tds_pre_ro_last_error` | string | Pre-RO | — | `""` ok, `"bad frame"` on parse failure |
| `tds_post_ro_ec` | float/null | Post-RO | µS/cm | |
| `tds_post_ro_temp` | float/null | Post-RO | °C | |
| `tds_post_ro_ppm` | float/null | Post-RO | ppm | |
| `tds_post_ro_online` | bool | Post-RO | — | |
| `tds_post_ro_fail_count` | int | Post-RO | — | |
| `tds_post_ro_last_error` | string | Post-RO | — | |

HA discovery topics: `homeassistant/sensor/twwp_<id>_tds_<zone>_<metric>/config`

HA entities (6 total):
- `sensor.wh_001_tds_pre_ro_ec`, `sensor.wh_001_tds_pre_ro_temp`, `sensor.wh_001_tds_pre_ro_ppm`
- `sensor.wh_001_tds_post_ro_ec`, `sensor.wh_001_tds_post_ro_temp`, `sensor.wh_001_tds_post_ro_ppm`

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
| `set_v_min` | float (9.0–13.0) | Set empty-battery voltage for % calculation. Persisted to NVS. |
| `set_v_max` | float (12.0–16.0) | Set full-battery voltage for % calculation. Persisted to NVS. |
| `set_voltage_cal` | float (0.8–1.2) | Set voltage calibration multiplier. Persisted to NVS. |
| `valve_open` | bool | Open (`true`) or close (`false`) relay. Disables auto mode. |
| `valve_auto` | bool | Re-enable (`true`) or disable (`false`) flow-driven auto mode. |
| `set_valve_type` | string | Set valve hardware type (`test`/`solenoid`/`ball_valve`). Persisted to NVS. |
| `set_trigger_source` | string | Set trigger source (`flow`/`manual`). Persisted to NVS. |
| `set_valve_idle_timeout` | int (0–3600) | Safety close N seconds after last flow while open. 0 = disabled. Persisted. |
| `set_valve_max_open` | int (0–3600) | Safety close N seconds after valve opened. 0 = disabled. Persisted. |
| `set_valve_timeout_disable_auto` | bool | If true, disable auto mode when safety close fires. Persisted. |
| `set_valve_timeout_alert` | bool | If true, publish to `twwp/<id>/alert` when safety close fires. Persisted. |
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
