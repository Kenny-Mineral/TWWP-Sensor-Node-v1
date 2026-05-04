# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done

### Session 2026-05-04 — USB standalone boot fix + OTA plumbing

**Problem diagnosed:** Device worked only when USB plugged into a computer. On 12V-only power, it rebooted in a boot loop, never staying online long enough to publish to HA. Root cause: `ARDUINO_USB_CDC_ON_BOOT=1` + `ARDUINO_USB_MODE=1` puts Serial into HWCDC mode. Without a USB host connected, every `Serial.println()` in `setup()` blocks waiting for the host — accumulated blocking time caused the hardware watchdog to fire before `setup()` completed.

**Relay brownout (secondary issue):** User had briefly bridged IN1 → GND on the relay module to test it. This forced the relay coil on continuously, drawing enough current to cause a brownout/reset. Unplugging that bridge resolved it.

**Flow data confirmed correct:** After device came back online, user ran water through sensor 2 (inlet). `flow_today_2 = 0.120628 L` appeared correctly in HA status payload. Sensor 1 (outlet) correctly showed 0 — water only passed through one sensor. No bug.

**NVS k_factor_2 corruption noted (not yet fixed):** NVS stored value is 2000 (corrupted); firmware correctly overrides with k-table from `node.json` (k_applied_2 = 20700). Impact: zero for now, but should be corrected via MQTT cmd `{"k_factor_2": 20700}`.

**Changes made:**

1. **`src/main.cpp`** — Added `Serial.setTxTimeoutMs(0)` immediately after `Serial.begin(115200)`:
   ```cpp
   Serial.begin(115200);
   Serial.setTxTimeoutMs(0);  // HWCDC: don't block TX if no USB host connected
   delay(500);                 // was delay(1000)
   ```
   This makes all Serial writes non-blocking (data dropped instead of blocking) so setup() completes even with no USB host.

2. **`platformio.ini`** — Two fixes to the `[env:ota]` section:
   - `extends = waveshare-esp32-s3-rs485-can` → `extends = env:waveshare-esp32-s3-rs485-can` (PlatformIO requires the `env:` prefix)
   - `upload_port = twwp-wh_001.local` → `upload_port = 192.168.20.18` (mDNS not resolving on this network; IP obtained from MQTT status payload)

**OTA flash:** Build succeeded (RAM 18.5%, Flash 22.0%). OTA upload to `192.168.20.18` was initiated. Whether it completed successfully was not confirmed before context ended.

### Build 1 — M3 actuator_valve relay driver (2026-05-03)
Replaced `actuator_solenoid_stub.{h,cpp}` with a full `actuator_valve.{h,cpp}` implementation (RAM 18.3%, Flash 22.0%):

- **Active-low relay driver** on `PIN_VALVE` (GPIO8). `LOW` = relay ON (valve open / LED on). Boot-safe: `HIGH` on startup before flow sensor initialises.
- **Auto mode** (default): `actuatorValve_loop()` reads `sensorFlow_getRateLpm(1)` and opens/closes relay against `FLOW_ACTIVE_THRESHOLD_LPM` (0.05 L/min). Currently driving a 12V LED simulating a ball valve.
- **Manual MQTT override**: `{"valve_open": true/false}` disables auto mode; `{"valve_auto": true}` re-enables it.
- **HA discovery**: `binary_sensor` for `valve_open` (device_class=opening), backed by `twwp/<id>/status`.
- **Renamed** `PIN_SOLENOID` → `PIN_VALVE` in `include/pins.h`. Updated `docs/PIN_ALLOCATION.md`, `docs/MQTT_TOPIC_MAP.md`, `docs/FIRMWARE_ARCHITECTURE.md`.

### Build 2 — Session flow + idle time tracking (2026-05-03)
Extended `session_flow.{h,cpp}` to track actual flow time vs total session time (RAM 18.5%, Flash 22.0%):

- **`flow_dur_s`** — accumulated seconds water was actually flowing within the session (sum of all ACTIVE segments, excludes idle gaps).
- **`idle_s`** — `dur_s - flow_dur_s` — time the tap was off mid-session before idle timeout fired. Zero for normal tap-on → tap-off sessions.
- Tracked via `flowSegmentStartMs`/`flowDurationMs` static vars, accumulated at each ACTIVE → ENDING transition, reset on IDLE → ACTIVE.
- Both fields added to: `sessions_recent` MQTT array, `session` event topic, `status` heartbeat (`session_last_flow_dur_s`, `session_last_idle_s`), HA discovery sensors, SD sessions.csv log.
- `SessionRecord` struct expanded; SD JSON keys `fds`/`its`; publish buffer grown to 2048 bytes.
- HA Markdown card YAML provided to render sessions list with all details.

## Last done

### Session 2026-05-05 — HA Lovelace dashboard

**Verified:** USB unplugged, device stayed online on 12V-only power. HA remained green. `Serial.setTxTimeoutMs(0)` fix confirmed effective.

**flex-table-card installed:** Downloaded v1.4 JS (69 KB) via local curl → SCP to server → placed in `/home/kenny/projects/homeassistant/config/www/flex-table-card.js`. Registered as Lovelace module resource via HA WebSocket API (`lovelace/resources/create`).

**Full Lovelace dashboard deployed** to existing `wh-001` dashboard (`/wh-001` URL path) via WebSocket `lovelace/config/save`. Entity IDs verified against live HA state before writing. 4 views:
- **Overview** — live flow rates, today totals, leak/valve status, battery, WiFi signal
- **Flow Data** — both channels: rate + today/week/month/year/total + all reset buttons
- **Sessions** — flex-table-card last 10 sessions, last session detail, session enable/timeout/threshold
- **System** — network diagnostics, power/battery cal, K factors, OTA state, factory reset

Dashboard YAML source of truth: `docs/LOVELACE_DASHBOARD.yaml` (committed).

## In progress
- Nothing active.

## Next step
- Remaining M3: confirm final valve/actuator type and purchase; add auto-close safety timeout.
- Optional: fix corrupted NVS k_factor_2 — send `{"k_factor_2": 20700}` via MQTT on `twwp/wh_001/cmd`.
- M2: confirm pressure transducer model and PSI range before ordering.

## Tool last used
claude-code

## Updated
2026-05-04
