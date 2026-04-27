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
| `twwp/<id>/session` | no | node → broker | Session-end event. Published when the 90 s idle timeout expires. JSON payload with session_id, start_ts, end_ts, duration_s, volume_out_L, volume_in_L, peak_rate_out, peak_rate_in. |
| `twwp/<id>/cmd` | no | broker → node | Command channel. Parsed in firmware (actuator commands in M3, OTA in M4). |
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
| `homeassistant/sensor/twwp_<id>_k_factor_1/config` | sensor (diagnostic) | pulses/L | measurement |
| `homeassistant/sensor/twwp_<id>_k_factor_2/config` | sensor (diagnostic) | pulses/L | measurement |

All flow discovery payloads set `state_topic` to `twwp/<id>/status` and extract their value via `value_template`.

---

### K factor (writable, M1)

K factor entities are `number` entities — the user can change them directly in HA. When changed, HA publishes to `twwp/<id>/cmd` and the firmware updates the value in RAM and saves it to `/config/node.json` on the SD card.

| Topic | Entity type | Unit | Range |
|---|---|---|---|
| `homeassistant/number/twwp_<id>_k_factor_1/config` | number | pulses/L | 1–9999 |
| `homeassistant/number/twwp_<id>_k_factor_2/config` | number | pulses/L | 1–9999 |

---

## Command topic payload format

The node subscribes to `twwp/<id>/cmd`. Payload must be valid JSON. Supported keys:

| Key | Type | Effect |
|---|---|---|
| `set_k_factor_1` | int | Set K factor for flow channel 1. Saved to `node.json`. |
| `set_k_factor_2` | int | Set K factor for flow channel 2. Saved to `node.json`. |
| `reset_flow_today_1` | bool | Zero today/week/month/year subtotals for channel 1. Saved to SD. |
| `reset_flow_today_2` | bool | Zero today/week/month/year subtotals for channel 2. Saved to SD. |
| `reset_flow_today` | bool | Zero today/week/month/year subtotals for both channels. |
| `reset_flow_totals_1` | bool | Zero lifetime total + all subtotals for channel 1. Clears NVS + SD. |
| `reset_flow_totals_2` | bool | Zero lifetime total + all subtotals for channel 2. Clears NVS + SD. |
| `reset_flow_totals` | bool | Zero all flow data for both channels. Clears NVS + SD. |

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
- K factor entities are writable `number` entities in HA — change them directly on the device card without touching the SD card or reflashing.
