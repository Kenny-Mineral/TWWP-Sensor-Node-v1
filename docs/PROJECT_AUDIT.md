# TWWP Project — Full Audit Document
_Generated 2026-04-29. For agent review of the full build, architecture, and state._

---

## What this project is

TWWP (The Water Water Project) is a self-hosted IoT node for monitoring a reverse-osmosis water filtration system. One ESP32-S3 node on a DIN rail monitors:
- Two Hall-effect flow sensors (RO purified output + raw input)
- One capacitive leak sensor beneath the system
- (future) One pressure transducer, one YiErYi 3788 RS485 multi-parameter water quality sensor, one solenoid/ball valve actuator

The node publishes to a self-hosted Mosquitto MQTT broker on Hetzner (TLS port 8883 only) and auto-configures Home Assistant entities via MQTT discovery. All data is also logged to a microSD card. The node works fully offline — it buffers to SD and drains to MQTT on reconnect.

---

## Infrastructure

| Component | Detail |
|---|---|
| Broker | Mosquitto on Hetzner, `twwp-iot.duckdns.org:8883`, TLS (Let's Encrypt), `allow_anonymous false` |
| VPN | Tailscale for admin SSH access (`kenny@100.67.244.37`) when direct Hetzner IP is blocked |
| Home Assistant | Docker container on Hetzner (`docker ps` name: `homeassistant`), config at `/home/kenny/projects/homeassistant/config/` |
| Node ID | `wh_001` (set in `include/secrets.h`, never hardcoded elsewhere) |
| MQTT credentials | Per-device: user `twwp_wh_001`, password in `/etc/mosquitto/passwd` |
| Board | Waveshare ESP32-S3-RS485-CAN (ESP32-S3-WROOM-1, 16 MB flash, 8 MB OPI PSRAM) |
| Toolchain | PlatformIO + Arduino framework, project at `/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/` |

---

## Milestone status

| Milestone | Status |
|---|---|
| M0 — Bring-up (WiFi, MQTT TLS, SD, leak sensor, offline buffer) | **Complete** |
| M0.5 — TLS + security hardening | **Complete** |
| M1 — Hall flow sensors (dual channel, K-factor, NVS+SD persistence, HA entities, CSV log) | **Complete** |
| Session tracking (configurable idle timeout, flow threshold, ring buffer, HA card) | **Complete — not yet flashed** |
| M2 — Pressure transducer | Blocked — sensor not purchased, model not confirmed |
| M3 — Actuator command channel | Blocked — actuator type not decided, not purchased |
| M4 — OTA over MQTT | Not started |
| M5 — YiErYi 3788 RS485 (pH, ORP, EC, TDS, water temp) | Blocked — hardware debug pending |
| M6 — HealthService + CalibrationService | Not started |
| M7 — AlertService + TelemetryService | Not started |
| M8 — Device lifecycle (registration, decommission, rate-limit) | Not started |

---

## Source file inventory

| File | Purpose | State |
|---|---|---|
| `src/main.cpp` | setup() + loop() — orchestrates all drivers, HA discovery publish, MQTT reconnect handler, command dispatch | Modified, uncommitted |
| `src/net_wifi.{h,cpp}` | WiFiManager captive portal + reconnect + credential reset | Stable |
| `src/net_mqtt.{h,cpp}` | MQTT/TLS client (WiFiClientSecure), offline SD buffer, HA discovery helpers | Stable |
| `src/time_rtc.{h,cpp}` | DS3231 RTC + NTP sync + Unix timestamp getter | Stable |
| `src/store_sd.{h,cpp}` | SD event log, time-series CSV, FIFO buffer queue, JSON file read/write helpers | Stable |
| `src/watchdog.{h,cpp}` | Hardware WDT + crash log writer | Stable |
| `src/status_led.{h,cpp}` | WS2812 RGB status LED via FastLED | Stable |
| `src/sensor_leak.{h,cpp}` | MH-RD digital leak sensor — LOW=wet, state-change event log | Stable |
| `src/sensor_flow.{h,cpp}` | Dual Hall pulse counter GPIO4/5 — K-factor, NVS+SD persistence, rate/total/subtotals per channel | Modified, uncommitted |
| `src/session_flow.{h,cpp}` | Tap session state machine (IDLE/ACTIVE/ENDING), configurable idle timeout + flow threshold (NVS), 10-session ring buffer, SD persistence, retained MQTT publish | Modified, uncommitted |
| `src/sensor_pressure.{h,cpp}` | Stub (M2) |  |
| `src/sensor_temp.{h,cpp}` | Stub (M5) |  |
| `src/sensor_yieryi.{h,cpp}` | Implemented (M5) | Hardware response validation pending |
| `src/actuator_solenoid.{h,cpp}` | Stub (M3) |  |
| `include/config.h` | All compile-time constants: topic strings, SD paths, timeouts, thresholds, buffer sizes | Modified, uncommitted |
| `include/pins.h` | GPIO assignments only — no logic | Modified, uncommitted |
| `include/secrets.h` | WiFi SSID/pass, MQTT host/port/user/pass/CA cert, NODE_ID — gitignored | Not in repo |
| `include/secrets.h.sample` | Template for secrets.h | In repo |

---

## Session tracking — design detail

This is the most recently completed feature. Key facts for a reviewer:

**State machine:** IDLE → ACTIVE (any flow > threshold) → ENDING (no flow) → IDLE (after idle timeout). If flow resumes during ENDING, transitions back to ACTIVE — this handles bottle-switching without splitting one fill into multiple sessions.

**Runtime-configurable values (NVS-persisted, survive power loss):**
- Idle timeout: 5–100 s (default 90 s). HA number entity `number.twwp_wh001_session_idle_timeout`.
- Flow threshold: 0.01–0.5 L/min (default 0.05). HA number entity `number.twwp_wh001_flow_threshold`.

**Session ring buffer:** 10 slots, oldest-first circular buffer. Persisted to `/config/sessions_recent.json` on SD. Restored on boot before first MQTT connect so the retained `twwp/<id>/sessions_recent` topic is repopulated even after power loss.

**Leak suspect:** If session is IDLE and flow rate is > 0.001 L/min but < threshold — flagged as potential slow leak. Exposed as two HA binary sensors (`binary_sensor.twwp_wh001_leak_suspect_1/2`).

**HA entities created by this feature:**
- `switch.twwp_wh001_session_enabled` — enable/disable session tracking
- `number.twwp_wh001_session_idle_timeout` — 5–100 s, config category
- `number.twwp_wh001_flow_threshold` — 0.01–0.5 L/min, config category
- `sensor.twwp_wh001_sessions_recent` — state = session count, attributes = full sessions JSON array
- `binary_sensor.twwp_wh001_leak_suspect_1` — moisture class
- `binary_sensor.twwp_wh001_leak_suspect_2` — moisture class

---

## MQTT command channel — full supported keys

All sent to `twwp/<id>/cmd` as JSON. Multiple keys per message supported.

| Key | Type | Effect |
|---|---|---|
| `set_k_factor_1/2` | int | K factor for flow channel. Saved to node.json. |
| `reset_flow_today/week/month/year[_1/_2]` | bool | Zero period subtotal. |
| `reset_flow_totals[_1/_2]` | bool | Zero lifetime total + all subtotals. Clears NVS + SD. |
| `factory_reset_flow` | bool | Wipe all flow data both channels + reset session ID. |
| `set_session_enabled` | bool | Enable/disable session tracking. NVS. |
| `set_session_idle_timeout` | int 5–100 | Idle gap before session finalises. NVS. |
| `set_flow_threshold` | float 0.01–0.5 | Flow detection threshold L/min. NVS. |
| `restart_wifi` | bool | Trigger WiFi reconnect. |

---

## HA device structure

All entities registered under a single HA device `TWWP wh_001` (identifiers: `["twwp", "wh_001"]`). Sub-device links used for logical grouping:

| Sub-device | Entities |
|---|---|
| Main node | Flow rates, flow totals (all periods), K-factor (writable), session entities, leak suspect sensors, uptime, WiFi RSSI, IP |
| RO Output (ch1) | flow_rate_1, flow_total_1, flow_today_1, flow_week_1, flow_month_1, flow_year_1, k_factor_1 |
| RO Input (ch2) | flow_rate_2, flow_total_2, flow_today_2, flow_week_2, flow_month_2, flow_year_2, k_factor_2 |
| Leak Sensor | binary_sensor leak (moisture class) |

---

## Data persistence — full picture

| Data | RAM | NVS (≤10 s gap) | SD (≤60 s gap) | MQTT retained |
|---|---|---|---|---|
| Flow totals per channel | Yes | Yes (every 10 s) | Yes (`flow_total.json`, 60 s) | Via status heartbeat |
| Flow subtotals (today/week/month/year) | Yes | No | Yes (`flow_total.json`) | Via status heartbeat |
| Session ring buffer (last 10) | Yes | Session ID only | Yes (`sessions_recent.json`) | Yes (`sessions_recent`, retained) |
| Session settings (idle timeout, threshold) | Yes | Yes (on change) | No | Via status heartbeat |
| K factors | Yes | Yes (on change) | Yes (`node.json`) | Via status heartbeat |
| Leak state | Yes | No | Event log only | Via alert topic |

---

## What is NOT yet committed

These files have been modified/created but not staged or committed:

```
M  docs/MQTT_TOPIC_MAP.md
M  include/config.h
M  include/pins.h
M  src/main.cpp
M  src/sensor_flow.cpp
M  src/sensor_flow.h
M  src/session_flow.cpp
M  src/session_flow.h
?? docs/LOVELACE_SESSIONS_CARD.yaml
```

The firmware has been compiled successfully (RAM 17.2%, Flash 21.2%) but **has not been flashed to the physical device yet**.

---

## What is NOT yet done (immediate next steps)

1. **Flash firmware** — `pio run --target upload` from the project root
2. **HACS UI setup** — HA → Settings → Devices & Services → Add Integration → HACS → GitHub login + accept terms. (HACS files are installed on the server at `/home/kenny/projects/homeassistant/config/custom_components/hacs/` via `docker cp`.)
3. **Install flex-table-card** — HACS → Frontend → search "flex-table-card" → Install → restart HA frontend
4. **Add Lovelace session card** — Dashboard → Edit → Add Card → Manual Card → paste contents of `docs/LOVELACE_SESSIONS_CARD.yaml`
5. **Verify session entities in HA** — confirm `Session Idle Timeout` and `Flow Detection Threshold` appear on main device card (not Diagnostics)
6. **End-to-end test** — run water, trigger session, confirm `twwp/wh_001/sessions_recent` appears in MQTT Explorer and table populates in Lovelace card
7. **Commit all modified files** — no commit has been made for the session tracking feature yet

---

## Known issues / design notes

- **`session_last_*` entities were previously hidden in Diagnostics** — fixed by removing `entity_category: diagnostic` from `publishHaDiscoverySession()` in main.cpp.
- **"HA gets weird after power loss"** — fixed: `publishOnlineState()` now calls `publishM0Status(true, false)` and `sessionFlow_republishRecentSessions()` immediately on MQTT reconnect instead of waiting for the next 10 s heartbeat.
- **M5 (YiErYi 3788)** is blocked on hardware — the RS485 sensor is connected but not responding. ESPHome prototyping recommended to validate Modbus register map before writing the C++ driver.
- **PSRAM** is available but not currently used. Large buffers (future telemetry payloads, OTA) should use `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`.
- **Sessions ring buffer is 10-slot** (constant `SESSIONS_RECENT_MAX` in `config.h`). Increasing it requires verifying the 1500-byte static MQTT publish buffer in `session_flow.cpp:publishRecentSessions()` is still sufficient (~120 bytes/session × 10 = 1200 bytes currently).

---

## Reference documents

| Doc | Contents |
|---|---|
| `docs/FIRMWARE_ARCHITECTURE.md` | Design principles, layered architecture, board facts, driver inventory, SD layout, persistence strategy |
| `docs/MQTT_TOPIC_MAP.md` | Every MQTT topic with QoS, retain, direction, HA discovery topics, command payload format |
| `docs/TASK_QUEUE.md` | Ordered milestone task list — first unchecked `[ ]` is the next action |
| `docs/SESSION.md` | Current session state: last done, in progress, next step |
| `docs/LOVELACE_SESSIONS_CARD.yaml` | Ready-to-paste Lovelace YAML for session history table card |
| `docs/COMPONENTS.md` | Sensor library with part numbers, specs, purchase links |
| `docs/PIN_ALLOCATION.md` | GPIO assignment table |
| `docs/USER_OPERATIONS.md` | End-user operations: SD commands, MQTT commands, HA usage |
