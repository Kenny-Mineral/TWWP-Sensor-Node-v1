# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done

### Session 2026-05-12 — M3 valve configuration system

**Scope:** Full M3 implementation — valve_type, trigger_source, safety timers (idle_timeout_s, max_open_s), timeout_disable_auto, timeout_alert. All firmware, tests, and docs complete.

**What was built:**
- `src/actuator_valve.cpp/h` — 6 config fields, 12 getter/setters, two independent safety timers, safety close sequence (close + optional disable_auto + optional alert + SD log always), trigger source dispatch (flow/manual/unknown), `actuatorValve_loadConfig()` (node.json + NVS overlay), `actuatorValve_saveToNvs()`
- `src/main.cpp` — boot call to `actuatorValve_loadConfig()`, 6 new MQTT cmd keys, 6 new heartbeat fields, `publishHaDiscoveryValveConfig()` (2 select + 2 number + 2 switch HA entities)
- `test/test_valve_config/` — 23 unit tests, all passing
- `platformio.ini` — ArduinoJson added to native lib_deps for JSON-aware tests
- Docs: `MQTT_TOPIC_MAP.md` and `USER_OPERATIONS.md` updated

**Commits this session:**
- `f81ab1d` test scaffold
- `4fb6bc5` config state + API
- `4091917` trigger dispatch
- `8c2b42b` safety timers + close sequence
- `87c1e36` boot load, NVS, MQTT cmds, heartbeat, HA discovery, docs

---

## In progress
none

## Next step — M3 bench test

Flash firmware and run the 9 verification checks:
1. `set_trigger_source: manual` → flow no longer opens valve
2. `set_trigger_source: flow` → flow opens valve again
3. `set_valve_idle_timeout: 10` → safety close 10s after flow stops
4. `set_valve_max_open: 10` → safety close 10s after open even with flow
5. `set_valve_timeout_disable_auto: true` → `valve_auto` → false after safety close
6. `set_valve_timeout_alert: true` → `VALVE_SAFETY_CLOSE` alert on `twwp/wh_001/alert`
7. `set_valve_type: ball_valve` → warning logged, solenoid fallback behaviour
8. All 6 new fields present in heartbeat JSON
9. All 6 new HA entities on device card (2 select + 2 number + 2 switch)

## Tool last used
claude-code

## Updated
2026-05-12 18:45
