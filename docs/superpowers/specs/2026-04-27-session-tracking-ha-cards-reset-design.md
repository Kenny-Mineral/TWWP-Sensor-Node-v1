# Design: Session Tracking, HA Device Cards, Flow Reset

Date: 2026-04-27

## Overview

Three related features for the TWWP sensor node:

1. **Session tracking** — detect and log discrete RO tap usage events with a 90-second idle timeout
2. **HA device card reorganisation** — split all entities into four separate HA device cards instead of one flat list
3. **Flow reset commands** — per-channel and combined resets for period subtotals and lifetime totals

---

## 1. Session Tracking

### Context

- Sensor 1 = RO purified output (what the user gets from the filter tap)
- Sensor 2 = RO total water input (purified + waste water)
- Waste = Sensor 2 − Sensor 1 (efficiency metric, calculated later)
- A "session" = one usage event at the RO tap — filling multiple bottles with short pauses counts as one session

### New module: `src/session_flow.cpp` / `src/session_flow.h`

Follows the existing `_begin()` / `_loop()` driver pattern. Reads from `sensorFlow_getRateLpm()` — no changes to sensor_flow.

### State Machine

```
IDLE ──(any channel > 0.05 L/min)──► ACTIVE ──(both channels = 0)──► ENDING
                                      ◄──(flow resumes < 90s)─────────┘  │
IDLE ◄──────────────────────────────────(90s timeout expires)────────────┘
```

- **IDLE → ACTIVE**: either channel rate exceeds 0.05 L/min threshold — records `start_ts`, resets per-session accumulators
- **ACTIVE → ENDING**: both channels drop to 0 — starts 90-second countdown
- **ENDING → ACTIVE**: flow resumes on either channel before timeout — countdown cancelled, same session continues
- **ENDING → IDLE**: 90-second timeout expires — session finalised, MQTT published, SD row appended

### Session Data

| Field | Type | Description |
|---|---|---|
| `session_id` | uint32 | Incrementing counter, persisted in NVS namespace "session", key "sid" |
| `start_ts` | uint32 | RTC Unix timestamp at session start |
| `end_ts` | uint32 | RTC Unix timestamp at session end |
| `duration_s` | uint32 | `end_ts - start_ts` |
| `volume_out_L` | float | Litres through sensor 1 accumulated during session |
| `volume_in_L` | float | Litres through sensor 2 accumulated during session |
| `peak_rate_out` | float | Peak L/min on channel 1 during session |
| `peak_rate_in` | float | Peak L/min on channel 2 during session |

### MQTT

Topic: `twwp/<id>/session` — QoS 0, no retain

Payload (JSON):
```json
{
  "session_id": 42,
  "start_ts": 1745700000,
  "end_ts": 1745700180,
  "duration_s": 180,
  "volume_out_L": 1.85,
  "volume_in_L": 3.20,
  "peak_rate_out": 1.2,
  "peak_rate_in": 2.1
}
```

Add row to `docs/MQTT_TOPIC_MAP.md`: `twwp/<id>/session`.

### SD Logging

File: `/log/sessions.csv`

Header (written once on file creation):
```
session_id,start_ts,end_ts,duration_s,volume_out_L,volume_in_L,peak_rate_out,peak_rate_in
```

Uses existing `storeSd_logDataRow()` mechanism. Pruned by `sdprune` per `retention_days` setting.

### HA Session Entities

Session entities appear on the **main node device card** (node-level events). Last session fields are added to the existing `twwp/<id>/status` heartbeat payload — no new topic needed. HA entities use `value_template` to extract them from the status JSON, same as flow entities.

Entities:
- Last Session Volume Out (L)
- Last Session Volume In (L)
- Last Session Duration (s)
- Last Session ID

### Future Extension Point

Session volume limiting for authenticated vs non-authenticated users is a planned future feature. The session state machine should be designed so a `volume_limit_L` threshold can be injected without restructuring the state machine. No implementation now — just keep the session accumulator accessible.

---

## 2. HA Device Card Reorganisation

Each logical sensor group gets its own `device` block in discovery payloads with a unique identifier, linked to the main node via `via_device: "twwp_<id>"`.

| HA Device Name | Identifier | via_device | Entities |
|---|---|---|---|
| TWWP wh_001 | `twwp_<id>` | — (root) | WiFi status, SSID, BSSID, IP, signal dB, signal %, uptime, Restart WiFi button, reset buttons (both-channel), last session entities |
| RO Output | `twwp_<id>_flow1` | `twwp_<id>` | Rate 1, today 1, week 1, month 1, year 1, total 1, K factor 1 (number), Reset Today 1 button, Reset Totals 1 button |
| RO Input | `twwp_<id>_flow2` | `twwp_<id>` | Rate 2, today 2, week 2, month 2, year 2, total 2, K factor 2 (number), Reset Today 2 button, Reset Totals 2 button |
| Leak Sensor | `twwp_<id>_leak` | `twwp_<id>` | Leak binary sensor |

Implementation: update `publishHaDiscoveryFlow()` and `publishHaDiscoveryDiagnostics()` in `main.cpp` to use per-group device blocks. Existing entity unique_ids are unchanged — HA will re-associate them to the new sub-devices without losing history.

---

## 3. Flow Reset Commands

### MQTT Commands (`twwp/<id>/cmd`)

| Key | Type | Effect |
|---|---|---|
| `reset_flow_today_1` | bool | Zero today/week/month/year for channel 1 only. Save to SD. |
| `reset_flow_today_2` | bool | Zero today/week/month/year for channel 2 only. Save to SD. |
| `reset_flow_today` | bool | Zero today/week/month/year for both channels. Save to SD. |
| `reset_flow_totals_1` | bool | Zero lifetime total + all subtotals for channel 1. Clear NVS ch1. Clear SD. Log event. |
| `reset_flow_totals_2` | bool | Zero lifetime total + all subtotals for channel 2. Clear NVS ch2. Clear SD. Log event. |
| `reset_flow_totals` | bool | Zero everything for both channels. Clear NVS. Clear SD. Log event. |

All six commands also available as serial commands via the existing serial command parser.

### HA Buttons

Reset buttons are `button` entities published via HA discovery. Payload on press: `{"reset_flow_today_1": true}` etc.

Per-channel reset buttons appear on the respective sub-device card (RO Output / RO Input). Both-channel reset buttons appear on the main node card.

### New public API in `sensor_flow.h`

```cpp
void sensorFlow_resetToday(uint8_t ch);   // ch = 1, 2, or 0 for both
void sensorFlow_resetTotals(uint8_t ch);  // ch = 1, 2, or 0 for both
```

`ch = 0` resets both channels. Saves state to SD and NVS after reset. Logs the event via `storeSd_logEvent()`.

---

## Files Changed

| File | Change |
|---|---|
| `src/session_flow.h` | New — public API (`sessionFlow_begin`, `sessionFlow_loop`, getters for last session fields) |
| `src/session_flow.cpp` | New — session state machine, MQTT publish, SD log. Depends on `sensorFlow_getRateLpm()`, `timeRtc_getUnixTime()`, `netMqtt_publishSub()`, `storeSd_logDataRow()` |
| `src/sensor_flow.h` | Add `sensorFlow_resetToday()`, `sensorFlow_resetTotals()` |
| `src/sensor_flow.cpp` | Implement reset functions |
| `src/main.cpp` | Update HA discovery (sub-devices), add reset cmd handling, add session last-state entities, call `sessionFlow_begin()` / `sessionFlow_loop()` |
| `docs/MQTT_TOPIC_MAP.md` | Add `twwp/<id>/session` row, add reset cmd rows |
| `docs/USER_OPERATIONS.md` | Document reset commands and session log |

---

## Out of Scope

- Session volume limiting / auth — future feature, extension point noted above
- Efficiency ratio calculation (S2 − S1) — future feature
- Cloud upload of session log — future feature (M8)
