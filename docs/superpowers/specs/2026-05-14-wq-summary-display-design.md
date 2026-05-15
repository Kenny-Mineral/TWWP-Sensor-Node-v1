# WQ Summary Display + HA-Configurable Thresholds — Design Spec

**Date:** 2026-05-14
**Status:** Approved

---

## Overview

Replace the individual Pre-RO and Post-RO OLED frames with a single Water Quality Summary frame showing all three filter zones side-by-side with status indicators. All threshold values, zone names, and status labels are configurable from Home Assistant via MQTT. Update the persistent header to show live RTC time and current tap session volume.

---

## Scope

1. New OLED frame: WQ Summary (replaces Pre-RO and Post-RO frames)
2. New module: `wq_config.{h,cpp}` — thresholds, names, labels, NVS persistence, HA discovery
3. Header update: live time (HH:MM) + live session volume
4. `session_flow` getter: `sessionFlow_getCurrentVolumeOut()`
5. Docs: `MQTT_TOPIC_MAP.md`, `USER_OPERATIONS.md`

---

## OLED Carousel

### New frame order (6 frames, down from 7)

| # | Frame | Notes |
|---|---|---|
| 0 | WQ Summary | NEW — replaces Pre-RO (0) and Post-RO (1) |
| 1 | Remin | Unchanged |
| 2 | Flow & Waste | Unchanged |
| 3 | Storage Tank | Unchanged |
| 4 | Sys Health | Unchanged |
| 5 | Branding | Unchanged |

`s_ui.setFrames(s_frames, 6)` in `displayOled_begin()`.

### WQ Summary frame layout

Usable area: 128×52px (below 12px header). Three equal rows, ~17px each.

```
PRE-RO    142ppm    WARN
POST-RO     3ppm    GOOD
REMIN      22ppm      OK
```

- Zone name: left-aligned, 10pt, from `wqConfig_getZoneName(zone)`
- TDS value: right-aligned to centre column, 10pt — `sensorTdsMeter_getTds()` for Pre-RO and Post-RO zones; `sensorYieryi_getTdsPpm()` for Remin. Shows `---` if sensor offline.
- Status label: right-aligned, 10pt, from `wqConfig_evalStatus(zone, tds_ppm)`
- Row y positions (from display top): 14, 31, 48

### Status evaluation logic

**Pre-RO** (single threshold `pre_ro_max`):
- `tds <= pre_ro_max` → label from `wqConfig_getPreRoOkLabel()` (default "OK")
- `tds > pre_ro_max` → label from `wqConfig_getPreRoWarnLabel()` (default "WARN")

**Post-RO** (two thresholds `post_ro_good_max`, `post_ro_check_max`):
- `tds <= post_ro_good_max` → `wqConfig_getPostRoGoodLabel()` (default "GOOD")
- `tds <= post_ro_check_max` → `wqConfig_getPostRoCheckLabel()` (default "CHECK")
- `tds > post_ro_check_max` → `wqConfig_getPostRoChangeLabel()` (default "CHANGE")

**Remin** (two thresholds `remin_min`, `remin_max`):
- `tds < remin_min` → `wqConfig_getReminLowLabel()` (default "LOW")
- `tds <= remin_max` → `wqConfig_getReminOkLabel()` (default "OK")
- `tds > remin_max` → `wqConfig_getReminHighLabel()` (default "HIGH")

---

## Header Update

### Current layout
```
14.2L         TWWP          WM
```

### New layout
```
10:34 2.3L    TWWP          WM
```

- **Left:** `HH:MM V.VL` — RTC time extracted from `timeRtc_getISOTimestamp()` chars [11..15] (`HH:MM`) + live session volume from `sessionFlow_getCurrentVolumeOut()`. Shows `0.0L` when no session active.
- **Centre:** `TWWP` (normal) / `!LEAK!` (blinking at 500ms, when leak detected) — unchanged
- **Right:** `WM`/`!!`/`!!B` connectivity flags — unchanged

The left string will be ~10 chars (`10:34 2.3L`). At 10pt on a 128px display this fits alongside the centred and right-aligned strings.

---

## New Module: `wq_config.{h,cpp}`

### Responsibilities
- Load config from NVS on boot (namespace `wq_cfg`)
- Expose typed getters for all thresholds, zone names, status labels
- Evaluate zone status given a TDS reading
- Parse incoming MQTT cmd keys and persist changes to NVS immediately
- Publish HA discovery on MQTT connect (called from `net_mqtt` on-connect callback, same pattern as other drivers)
- Publish current config state to `twwp/<id>/wq_config` (retained JSON) after any change and on connect

### NVS keys (namespace `wq_cfg`)

| Key | Type | Default |
|---|---|---|
| `pre_ro_max` | float | 110.0 |
| `post_ro_good` | float | 5.0 |
| `post_ro_chk` | float | 8.0 |
| `remin_min` | float | 15.0 |
| `remin_max` | float | 30.0 |
| `pre_ro_name` | string | "PRE-RO" |
| `post_ro_name` | string | "POST-RO" |
| `remin_name` | string | "REMIN" |
| `pre_ok_lbl` | string | "OK" |
| `pre_wrn_lbl` | string | "WARN" |
| `post_gd_lbl` | string | "GOOD" |
| `post_chk_lbl` | string | "CHECK" |
| `post_chg_lbl` | string | "CHANGE" |
| `rem_lo_lbl` | string | "LOW" |
| `rem_ok_lbl` | string | "OK" |
| `rem_hi_lbl` | string | "HIGH" |

String values truncated to 15 chars max (OLED display constraint).

### Public API (`wq_config.h`)

```cpp
bool        wqConfig_begin();           // load NVS, called from setup()
void        wqConfig_onMqttConnect();   // publish discovery + state
bool        wqConfig_handleCmd(const char* key, const char* value); // returns true if key was consumed

// Names (shown on OLED and in HA)
const char* wqConfig_getPreRoName();
const char* wqConfig_getPostRoName();
const char* wqConfig_getReminName();

// Thresholds
float       wqConfig_getPreRoMax();
float       wqConfig_getPostRoGoodMax();
float       wqConfig_getPostRoCheckMax();
float       wqConfig_getReminMin();
float       wqConfig_getReminMax();

// Status evaluation — returns the appropriate label string given a TDS reading
const char* wqConfig_evalPreRo(float tds_ppm);
const char* wqConfig_evalPostRo(float tds_ppm);
const char* wqConfig_evalRemin(float tds_ppm);
```

No zone indices. The OLED frame calls `sensorTdsMeter_getTds(TDS_ZONE_PRE_RO)` etc. directly and passes the result to the eval functions.

### MQTT cmd keys accepted

Incoming on `twwp/<id>/cmd` JSON. Keys:

| JSON key | Maps to NVS |
|---|---|
| `wq_pre_ro_max` | `pre_ro_max` |
| `wq_post_ro_good_max` | `post_ro_good` |
| `wq_post_ro_check_max` | `post_ro_chk` |
| `wq_remin_min` | `remin_min` |
| `wq_remin_max` | `remin_max` |
| `wq_pre_ro_name` | `pre_ro_name` |
| `wq_post_ro_name` | `post_ro_name` |
| `wq_remin_name` | `remin_name` |
| `wq_pre_ro_ok_label` | `pre_ok_lbl` |
| `wq_pre_ro_warn_label` | `pre_wrn_lbl` |
| `wq_post_ro_good_label` | `post_gd_lbl` |
| `wq_post_ro_check_label` | `post_chk_lbl` |
| `wq_post_ro_change_label` | `post_chg_lbl` |
| `wq_remin_low_label` | `rem_lo_lbl` |
| `wq_remin_ok_label` | `rem_ok_lbl` |
| `wq_remin_high_label` | `rem_hi_lbl` |

After any change: persist to NVS, re-publish `wq_config` state topic.

### HA Discovery entities

All published to `homeassistant/<platform>/<node_id>/<unique_id>/config` on connect, retained.

**Number entities (5)** — platform `number`:

| HA name | unique_id suffix | min | max | step | cmd key |
|---|---|---|---|---|---|
| Pre-RO Max TDS | `wq_pre_ro_max` | 0 | 500 | 1 | `wq_pre_ro_max` |
| Post-RO Good Max | `wq_post_ro_good_max` | 0 | 50 | 0.5 | `wq_post_ro_good_max` |
| Post-RO Check Max | `wq_post_ro_check_max` | 0 | 50 | 0.5 | `wq_post_ro_check_max` |
| Remin Min TDS | `wq_remin_min` | 0 | 100 | 1 | `wq_remin_min` |
| Remin Max TDS | `wq_remin_max` | 0 | 100 | 1 | `wq_remin_max` |

**Text entities (11)** — platform `text`:

| HA name | unique_id suffix | max length |
|---|---|---|
| Pre-RO Zone Name | `wq_pre_ro_name` | 15 |
| Post-RO Zone Name | `wq_post_ro_name` | 15 |
| Remin Zone Name | `wq_remin_name` | 15 |
| Pre-RO OK Label | `wq_pre_ro_ok_label` | 15 |
| Pre-RO Warn Label | `wq_pre_ro_warn_label` | 15 |
| Post-RO Good Label | `wq_post_ro_good_label` | 15 |
| Post-RO Check Label | `wq_post_ro_check_label` | 15 |
| Post-RO Change Label | `wq_post_ro_change_label` | 15 |
| Remin Low Label | `wq_remin_low_label` | 15 |
| Remin OK Label | `wq_remin_ok_label` | 15 |
| Remin High Label | `wq_remin_high_label` | 15 |

All number and text entities use:
- `state_topic`: `twwp/<id>/wq_config`
- `command_topic`: `twwp/<id>/cmd`
- `value_template`: `{{ value_json.<key> }}`
- `command_template`: `{"<cmd_key>": {{ value }}}` (numbers) / `{"<cmd_key>": "{{ value }}"}` (text)
- `device`: same device block as all other TWWP entities (name, identifiers, manufacturer, model)

### State topic payload (`twwp/<id>/wq_config`)

Retained JSON snapshot published on connect and after every change:

```json
{
  "pre_ro_max": 110.0,
  "post_ro_good_max": 5.0,
  "post_ro_check_max": 8.0,
  "remin_min": 15.0,
  "remin_max": 30.0,
  "pre_ro_name": "PRE-RO",
  "post_ro_name": "POST-RO",
  "remin_name": "REMIN",
  "pre_ro_ok_label": "OK",
  "pre_ro_warn_label": "WARN",
  "post_ro_good_label": "GOOD",
  "post_ro_check_label": "CHECK",
  "post_ro_change_label": "CHANGE",
  "remin_low_label": "LOW",
  "remin_ok_label": "OK",
  "remin_high_label": "HIGH"
}
```

---

## `session_flow` Change

Add one getter to `session_flow.h` and `session_flow.cpp`:

```cpp
float sessionFlow_getCurrentVolumeOut();
// Returns live accumulating litres on sensor 1 for the active session.
// Returns 0.0f when no session is active (state == IDLE).
// Implementation: sensorFlow_getTotalL(1) - sessionStartTotal1, clamped >= 0.
// sessionStartTotal1 is already a file-scoped static in session_flow.cpp.
```

No other changes to session_flow.

---

## Docs Changes

### `MQTT_TOPIC_MAP.md`
Add row:

| Topic | Direction | QoS | Retain | Content |
|---|---|---|---|---|
| `twwp/<id>/wq_config` | node → broker | 1 | yes | JSON snapshot of all WQ threshold/label/name config values |

### `USER_OPERATIONS.md`
- Update OLED frame table: remove Pre-RO and Post-RO rows, add WQ Summary row at position 0
- Update header description: left column now shows `HH:MM V.VL` (time + live session volume)
- Add WQ Summary frame description with status label meanings

---

## Files Changed

| File | Change |
|---|---|
| `src/display_oled.cpp` | Remove `framePreRO`, `framePostRO`; add `frameWqSummary`; update frame array to 6; update header draw |
| `src/wq_config.h` | New file |
| `src/wq_config.cpp` | New file |
| `src/session_flow.h` | Add `sessionFlow_getCurrentVolumeOut()` declaration |
| `src/session_flow.cpp` | Implement `sessionFlow_getCurrentVolumeOut()` |
| `docs/MQTT_TOPIC_MAP.md` | Add `wq_config` topic row |
| `docs/USER_OPERATIONS.md` | Update OLED section |

---

## Constraints (from firmware skill)

- No `delay()` anywhere
- All strings stack-allocated (no heap in display callbacks)
- NVS writes only on value change, not every loop tick
- New MQTT topics added to `MQTT_TOPIC_MAP.md` in same commit
- ArduinoJson v7 API only (`JsonDocument`, not `DynamicJsonDocument`)
- MQTT TLS only, port 8883
