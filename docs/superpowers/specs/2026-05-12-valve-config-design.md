# Design: Valve Configuration System (M3)

**Date:** 2026-05-12
**Status:** Approved

---

## Context

The valve driver currently has one mode: auto-open when `flow_rate_1 > 0.05 L/min`, auto-close when it drops. This works as a test proxy (a relay driving an LED, buzzer, or multimeter). The real valve will be a solenoid or motorised ball actuator triggered by a future mechanism (HA button, QR, app+proximity, QR+auth). This design formalises the valve configuration system to handle the current test state cleanly and lay the groundwork for future triggers.

---

## Config block (`node.json` → `"valve"` key)

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

All fields are optional. Defaults above apply if absent.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `valve_type` | string | `"test"` | Hardware wiring model: `"test"` / `"solenoid"` / `"ball_valve"` |
| `trigger_source` | string | `"flow"` | What opens the valve: `"flow"` / `"manual"` |
| `idle_timeout_s` | int | 0 | Safety close after N seconds with no flow while open. 0 = disabled. |
| `max_open_s` | int | 0 | Safety close N seconds after valve opened. 0 = disabled. |
| `timeout_disable_auto` | bool | false | If true, disable auto mode when a safety close fires. |
| `timeout_alert` | bool | true | If true, publish to `twwp/<id>/alert` when a safety close fires. |

**Persistence:** loaded from `node.json` at boot, overlaid by NVS values if present. MQTT cmd writes go to NVS — survive reboots without touching the SD card.

---

## Valve type behaviour

| Type | `open()` | `close()` | Notes |
|---|---|---|---|
| `test` | `digitalWrite(PIN_VALVE, LOW)` | `digitalWrite(PIN_VALVE, HIGH)` | Current NC-wired relay + LED/indicator |
| `solenoid` | same | same | Production solenoid — sustained energise to hold open |
| `ball_valve` | log warning, fall back to solenoid | same | Stub — not implemented until hardware confirmed |

When `valve_type == "test"`, the HA device card shows a diagnostic note: _"Test mode — relay driving indicator only."_

---

## Trigger source behaviour

| Source | `loop()` behaviour |
|---|---|
| `flow` | Open when `flow_rate_1 > FLOW_ACTIVE_THRESHOLD_LPM`, close when it drops. Current behaviour. |
| `manual` | Loop does nothing. Valve responds to MQTT `valve_open` cmd only. |
| unknown | Log warning once at boot. Fall back to `manual`. |

Future sources (`qr`, `proximity_app`, `qr_auth`) are accepted in config without error, treated as `manual` until implemented.

Safety timers run regardless of trigger source.

---

## Safety timer logic

Two independent timers in `actuatorValve_loop()`:

**Idle timer (`idle_timeout_s`)**
- Resets each loop tick where `flow_rate_1 > FLOW_ACTIVE_THRESHOLD_LPM`
- Counts up while valve is open and flow is absent
- Fires when elapsed ≥ `idle_timeout_s` (and `idle_timeout_s > 0`)

**Max-open timer (`max_open_s`)**
- Starts on valve open transition (closed → open), any trigger
- Fires when elapsed ≥ `max_open_s` (and `max_open_s > 0`)
- Resets on any valve close

**Safety close sequence (either timer):**
1. `actuatorValve_close()`
2. If `timeout_disable_auto`: `actuatorValve_setAuto(false)`
3. If `timeout_alert`: publish to `twwp/<id>/alert`:
   ```json
   {"type":"VALVE_SAFETY_CLOSE","reason":"idle_timeout","timeout_s":300}
   ```
4. Always: `storeSd_logEvent("[VALVE] safety close: reason=idle_timeout, timeout_s=300")`

Both timers reset when the valve closes for any reason.

---

## New driver API (`actuator_valve.h`)

```cpp
// Existing (unchanged)
bool actuatorValve_begin();
void actuatorValve_loop();
void actuatorValve_open();
void actuatorValve_close();
bool actuatorValve_isOpen();
void actuatorValve_setAuto(bool enable);
bool actuatorValve_isAuto();

// New
void actuatorValve_setValveType(const char* type);     // "test"|"solenoid"|"ball_valve"
void actuatorValve_setTriggerSource(const char* src);  // "flow"|"manual"
void actuatorValve_setIdleTimeoutS(uint32_t s);
void actuatorValve_setMaxOpenS(uint32_t s);
void actuatorValve_setTimeoutDisableAuto(bool v);
void actuatorValve_setTimeoutAlert(bool v);

const char* actuatorValve_getValveType();
const char* actuatorValve_getTriggerSource();
uint32_t    actuatorValve_getIdleTimeoutS();
uint32_t    actuatorValve_getMaxOpenS();
bool        actuatorValve_getTimeoutDisableAuto();
bool        actuatorValve_getTimeoutAlert();
```

---

## MQTT command keys (new, on `twwp/<id>/cmd`)

| Key | Type | Effect |
|---|---|---|
| `set_valve_type` | string | Set valve hardware type. Persisted to NVS. |
| `set_trigger_source` | string | Set trigger source. Persisted to NVS. |
| `set_valve_idle_timeout` | int (0–3600) | Set idle timeout in seconds. 0 = off. Persisted. |
| `set_valve_max_open` | int (0–3600) | Set max-open timeout in seconds. 0 = off. Persisted. |
| `set_valve_timeout_disable_auto` | bool | Set timeout-disables-auto flag. Persisted. |
| `set_valve_timeout_alert` | bool | Set timeout-publishes-alert flag. Persisted. |

---

## HA entities (new discovery)

| Entity | Type | Range | Persisted |
|---|---|---|---|
| Valve Type | select | `test` / `solenoid` / `ball_valve` | NVS |
| Trigger Source | select | `flow` / `manual` | NVS |
| Valve Idle Timeout | number | 0–3600 s | NVS |
| Valve Max Open Time | number | 0–3600 s | NVS |
| Timeout Disables Auto | switch | on/off | NVS |
| Timeout Publishes Alert | switch | on/off | NVS |

All writable. Test-mode diagnostic text appears as a sensor entity when `valve_type == "test"`.

---

## Heartbeat additions (`twwp/<id>/status`)

```json
"valve_type":                 "test",
"trigger_source":             "flow",
"valve_idle_timeout_s":       0,
"valve_max_open_s":           0,
"valve_timeout_disable_auto": false,
"valve_timeout_alert":        true
```

---

## Files to change

| File | Change |
|---|---|
| `src/actuator_valve.cpp` | Add config state, timer logic, new API functions |
| `src/actuator_valve.h` (in src/) | Declare new API |
| `src/net_mqtt.cpp` | Handle new cmd keys; add new HA discovery topics; publish new heartbeat fields |
| `src/store_sd.cpp` or `src/main.cpp` | Load `valve` block from `node.json` at boot |
| `docs/MQTT_TOPIC_MAP.md` | Add 6 new cmd keys |
| `docs/USER_OPERATIONS.md` | Add valve config section (type, trigger, timeouts) |

---

## Testing

1. Set `trigger_source: manual` via MQTT → confirm flow no longer opens valve
2. Set `trigger_source: flow` → confirm flow opens valve again
3. Set `idle_timeout_s: 10`, open valve manually, wait — confirm safety close at 10s
4. Set `max_open_s: 10`, open valve — confirm safety close at 10s even with flow present
5. Set `timeout_disable_auto: true` — confirm auto mode is off after safety close
6. Set `timeout_alert: true` — confirm alert published to `twwp/wh_001/alert`
7. Set `valve_type: ball_valve` — confirm warning logged, solenoid fallback behaviour
8. Confirm all 6 new fields appear in heartbeat JSON
9. Confirm all 6 HA entities appear on device card and are writable
