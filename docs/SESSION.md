# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done

### Session 2026-05-08 — M5 YiErYi RS485 water-quality firmware

**Implemented:** Added `sensor_yieryi.{h,cpp}` for YiErYi 3178/3788 water-quality meters on the Waveshare onboard RS485 port. The driver uses UART1 through the board RS485 transceiver (GPIO17 TX, GPIO18 RX, GPIO21 auto DE/RE), not the laptop USB-RS485 adapter.

**Protocol source:** Vendor spreadsheet `Modbus Communication Data Format-V1.01.x....xlsx` in `/home/kenny/Documents/3178 software water monitoring/`. The untested AI-created `read_meter.py` was treated as a hint only.

**Driver behaviour:**
- 9600 8N1 Modbus RTU, CRC-16 validation.
- Reads register `0x0000`, count `4`; parses the vendor 16-byte response with `00 08` byte-count field.
- Writes register `0x0005` to switch pH/ORP mode before reads.
- Non-blocking state machine; no long blocking waits in loop.
- Default config enables one meter only: pre-RO at Modbus address `1`. Post-RO and remineralised zones are disabled until unique addresses are confirmed.
- Staleness watchdog: no CRC-valid read within 60 s publishes water-quality values as `null` and marks `wq_<zone>_online=false`.

**MQTT/HA/SD updates:**
- Status payload now includes locked water-quality fields: `wq_pre_ro_*`, `wq_post_ro_*`, `wq_remin_*`.
- Adds diagnostics per zone: `online`, `fail_count`, `last_error`, `raw_hex`, and humidity.
- HA MQTT discovery publishes 12 water-quality sensors.
- SD data CSV now includes water-quality columns.
- USB serial console adds `wq_status` and `wq_poll`.

**Build:** `/home/kenny/.platformio/penv/bin/pio run` succeeded. Final size: RAM 18.7%, flash 22.4%.

**Snapshots saved:**
- Before M5 changes: `/home/kenny/Documents/twwp-firmware-snapshots/20260508-002612-before-m5-yieryi`
- Final M5 driver state: `/home/kenny/Documents/twwp-firmware-snapshots/20260508-003411-final-m5-yieryi-driver`

**Not yet verified on hardware:** `pio device list` did not show the Waveshare board, so no upload or live RS485 read was performed.

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

## Last done

### Session 2026-05-05 — Monitoring stack deployed + InfluxDB data flowing

**Deployed:** InfluxDB 3 Core + Grafana running on Hetzner VPS via `docker compose up -d`. Both containers healthy. Grafana accessible at `http://100.67.244.37:3000` (Tailscale).

**InfluxDB receiving data:** HA confirmed writing — 85 events on first flush, ~2 events every 10 s thereafter. Grafana Explore shows all TWWP entity measurements in the dropdown.

**Key issues resolved this session:**
1. **InfluxDB data dir permissions** — container runs as uid 1500; host dir was kenny-owned. Fix: `sudo chown -R 1500:1500 influxdb/data/`.
2. **HA host-network mode** — HA container uses `network_mode: host`, cannot join Docker networks. Fix: expose InfluxDB on `127.0.0.1:8181`; use `host: localhost` in HA config.
3. **Double-nested YAML key** — `influxdb.yaml` had a top-level `influxdb:` key while being included as `influxdb: !include influxdb.yaml`. Fix: removed top-level key, dedented content.
4. **YAML schema validation blocks component (root cause)** — HA deprecated YAML connection settings (host/port/token/etc.) and auto-migrated them to a UI config entry. Keeping `influxdb: !include influxdb.yaml` in `configuration.yaml` with only `include:` entities left caused silent YAML schema validation failure, blocking the entire influxdb component including the UI config entry. No errors logged — just silence. Fix: **removed `influxdb: !include influxdb.yaml` from `configuration.yaml` entirely.** InfluxDB integration is now 100% UI-managed.
5. **Grafana port binding** — was `127.0.0.1:3000`; Tailscale traffic arrives on `tailscale0` not loopback. Fix: changed to `0.0.0.0:3000` in docker-compose.

**Entity filtering:** The HA influxdb YAML `include:` list is no longer active (YAML removed). All HA entities now write to InfluxDB. Filter by entity name in Grafana queries. The `ha-config/influxdb.yaml` file in the local repo is documentation only — do not reference it from `configuration.yaml`.

**Persistent memory notes added:** server file write approach (write to /tmp → docker cp), HA influxdb YAML config gotcha, Tailscale IP + SSH user confirmed.

## In progress
- Firmware: ready to commit current combined firmware state, including prior uncommitted M3/M2.5/session changes plus M5 water-quality driver.

## Next step
- Upload firmware to the Waveshare board, wire one YiErYi meter to the onboard RS485 A/B terminals, then run serial `wq_status` or inspect MQTT status fields for `wq_pre_ro_raw_hex`, `wq_pre_ro_last_error`, and parsed pH/ORP/EC/temp.
- If multiple meters are installed, assign unique Modbus addresses before enabling `post_ro` and `remin` in `/config/node.json`.
- Create Grafana dashboards via UI: Overview (flow rates, leak, valve, battery), Flow History, Water Quality, System.
- Export dashboard JSON and commit to `grafana/provisioning/dashboards/` in the twwp-monitoring repo.
- M3: confirm final valve type and purchase. Add auto-close safety timeout to `actuator_valve.cpp`.
- Optional: fix corrupted NVS `k_factor_2` — send `{"k_factor_2": 20700}` via MQTT on `twwp/wh_001/cmd`.

## Tool last used
codex

## Updated
2026-05-08 00:38
