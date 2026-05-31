# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done

### Session 2026-06-01 — graphify knowledge graph installed

**Scope:** Installed the graphify CLI tool and built a queryable knowledge graph of the firmware project.

**Changes:**
- Installed `graphifyy` v0.8.26 via pipx; registered `/graphify` slash command with Claude Code (`graphify install`)
- Created `.graphifyignore` in both project directories (excludes `.pio/`, binaries, `.playwright-mcp/`, etc.)
- Added `graphify-out/` to `.gitignore` in the firmware project
- Added `## Knowledge graph (graphify)` section to the Waveshare build TWWP `CLAUDE.md` with build/query/update commands
- Built initial graph: 727 nodes, 2021 edges

**Graph highlights:**
- God Nodes: `handleCmd()` (64 edges), `addOperationalStatus()` (40), `loop()` (31), `netMqtt_publish()` (29)
- `handleCmd()` bridges 10 communities — TDS cal, WQ display, flow cal, OLED, valve, OTA, AP portal, and more
- Outputs in `graphify-out/` — open `graph.html` in browser, read `GRAPH_REPORT.md` for analysis

**How to query the graph (in Claude Code):**
```
/graphify query "what connects sensor_flow to MQTT publish?"
/graphify explain "session_flow"
/graphify path "main.cpp" "net_mqtt.cpp"
/graphify /home/kenny/Documents/PlatformIO/Projects/TWWP\ Sensor\ Node\ v1 --update
```

---

### Session 2026-05-26 (previous) — deployment-readiness stabilization pass

**Scope:** Reconciled the channel-mapping drift, split live telemetry from diagnostics/config state, and hardened OTA observability so the node is safer to deploy remotely.

**Changes:**
- **Canonical flow map enforced** — firmware now treats Ch1 as tap output (`DWS-MH-02`, GPIO4), Ch2 as RO output into tank/system (`USN-HS06PE`, GPIO5), and Ch3 as raw RO input / grey-water reference (`USN-HS06PS`, GPIO7). HA discovery labels, calibration labels, and tank comments updated to match.
- **Tank logic corrected** — `tank_monitor.cpp` now integrates Ch2-in minus Ch1-out and uses Ch2 stop detection for full-tank snap. `display_oled.cpp` tank ETA now reads Ch2 as the fill source.
- **OLED calibration coverage** — `drawCalMode()` now includes flow calibration activity on channel 3.
- **Telemetry split added** — `include/config.h` now defines `twwp/<id>/status`, `twwp/<id>/status_diag`, and `twwp/<id>/status_cfg`. `main.cpp` publishes operational heartbeat every 10 s, diagnostics every 60 s, and config state on connect / after commands.
- **Heavy fields moved off the live heartbeat** — flow config/calibration fields (`sensor_model_*`, `k_factor_*`, `k_table_*`, debounce, cal ref values, tank capacity, session thresholds, voltage calibration, valve config) now publish on `status_cfg`. Flow diagnostics (`pulses_raw_*`, `k_applied_*`, smoothed rates, cal state/suggested/pulses`) and network identity fields publish on `status_diag`.
- **OTA observability hardened** — `net_ota.cpp` now republishes retained `twwp/<id>/ota_state` telemetry on state/error/validation transitions and exposes `ota_validation_pending` plus `ota_rolled_back` into the operational heartbeat. `net_ota.h` adds `netOta_isAwaitingValidation()`.
- **Build verified** — `pio run` passes after the stabilization changes (`0.1.1+20260526-0549` during local build).

**Known gaps still requiring live validation:**
- No live-node verification yet for the new channel map, split status topics, or OTA validation/rollback reporting.
- Lovelace YAML still contains older `Tank Fill`/channel naming in several titles and comments and needs a cleanup pass against the new canonical map.
- `docs/MQTT_TOPIC_MAP.md`, `docs/USER_OPERATIONS.md`, `docs/COMPONENTS.md`, and `docs/PIN_ALLOCATION.md` still need final sync with the implemented topic split / channel map in this same stabilization pass.

### Session 2026-05-17 (continued) — Flow calibration safety for live units

**Scope:** Added fail-safes to the flow K-factor calibration wizard so it is safe for use by general users and technicians on live water filter units.

**Changes:**
- **Auto-abort on idle** (`sensor_flow.cpp`) — if COLLECTING state persists for 90 seconds with fewer than 100 pulses detected, the cal session is automatically aborted. `cal_state` shows `timed_out` for 5 seconds (HA and OLED pick this up) then returns to `idle`. Old K factor is never touched. Event logged to SD via `storeSd_logEvent`.
- **Minimum pulse guard on COMMIT** (`sensor_flow.cpp`) — `calCommit()` rejects commits with < 100 pulses. Returns false, sets `cal_state` to `too_few_pulses` for 5 seconds (visible in HA/OLED), then returns to `collecting` so the user can let more water through and try again. Old K factor is never touched.
- **OLED countdown** (`display_oled.cpp`) — when in COLLECTING with no pulses and < 30s until auto-abort, OLED shows `Abort in Xs` instead of the normal filling screen. On timeout: shows `!! NO FLOW !! / Auto-aborted`. On too-few-pulses: shows `TOO FEW PULSES / Got:X Need:100 / Let more water / flow then COMMIT`.
- **Fast publish extended** (`main.cpp`) — `handleFastCalPublish()` now publishes during all non-idle cal states (including error states), so HA sees `timed_out` / `too_few_pulses` transitions in real time. Also publishes `cal_secs_until_timeout_1/2` when countdown is active.
- **New getter** (`sensor_flow.h/.cpp`) — `sensorFlow_getCalSecsUntilTimeout(ch)` returns seconds until auto-abort, or -1 if not at risk (used by OLED and fast publish).
- **config.h** — `FLOW_CAL_IDLE_TIMEOUT_MS` (90s), `FLOW_CAL_MIN_PULSES` (100), `FLOW_CAL_ERROR_HOLD_MS` (5s).
- **USER_OPERATIONS.md** — safety behaviour table added to §Flow Sensor K-Factor Calibration; updated status fields table.

**Key decisions:**
- Error states (`timed_out`, `too_few_pulses`) are RAM-only display flags, not new enum values. Real state machine stays IDLE/COLLECTING/DONE — simpler, no new persist logic needed.
- `too_few_pulses` returns to COLLECTING (not IDLE) so the user can add more water and commit again without restarting.
- `timed_out` goes to IDLE — the session is truly dead, must press START again.
- 100 pulses minimum: at K≈5500 that's ~18mL — catches zero-flow commits without false-rejecting even very small containers.
- Auto-abort fires in `sensorFlow_loop()` on every 1s tick — works fully offline, no MQTT needed.

### Session 2026-05-17 — Calibration workflow: OLED + firmware + HA dashboard

**Scope:**
- **OLED sticky header** — removed "TWWP" text from centre slot. Leak warning still blinks. One-line change in `display_oled.cpp`.
- **OLED calibration display mode** — `drawCalMode()` added to `display_oled.cpp`. Auto-detects any active flow/TDS calibration (checks `sensorFlow_getCalState` and `sensorTdsMeter_getCalState`). While cal is active: stops carousel auto-scroll and renders full-screen cal view (title, current step, live pulse count / flow rate / raw EC, and suggested value on DONE). Resumes carousel automatically when cal returns to idle. Priority: flow ch1 > flow ch2 > TDS zone 0 > TDS zone 1.
- **Fast cal-state publish** — `handleFastCalPublish()` added to `main.cpp`. While flow cal state is "collecting", publishes `cal_state_1/2`, `cal_pulses_since_start_1/2`, `flow_rate_1/2` to TOPIC_STATUS every 2s (unbuffered direct publish — not stored to SD). Stops automatically when state leaves COLLECTING. Wired into `loop()`.
- **Calibration session logging** — `publishCalSession()` added to `main.cpp`. On flow_cal_accept or tds_cal_accept: publishes JSON to `twwp/<id>/cal_session` (buffered, non-retained) and appends row to `/log/cal_sessions.csv`. Captures old value before accept, new value after, reference value used, and duration since begin. Begin-time tracked via `s_flowCalBeginMs[]` and `s_tdsCalBeginMs[]` statics. WQ cal date recording (set_wq_*_cal_date) not logged as a cal_session — date is visible in status heartbeat.
- **HA discovery** — added `cal_state_1/2` and `cal_pulses_1/2` as diagnostic sensor entities (auto-discovered on next MQTT connect). These expose the live calibration state and pulse count from the heartbeat JSON.
- **config.h** — added `TOPIC_CAL_SESSION`, `SD_CAL_SESSION_LOG_PATH` (/log/cal_sessions.csv), `CAL_SESSION_LOG_HEADER`, `CAL_FAST_PUBLISH_INTERVAL_MS` (2000ms).
- **Lovelace dashboard** — pushed directly to HA via WebSocket API (url_path: wh-001). Two new tabs added: "Water Quality" (live tile cards for Pre-RO TDS, Post-RO TDS, Remin YiErYi pH/ORP/EC/temp) and "Calibration" (WQ probe physical procedure + date recording per zone; TDS EC wizard with step-by-step markdown per zone; Flow K-factor wizard per channel in phone-friendly sequential layout with live pulses; Voltage cal). Sessions tab: apexcharts-card bar chart (volume per session) added above existing flex-table. apexcharts-card v2.2.3 downloaded to `/home/kenny/projects/homeassistant/config/www/` and registered via WebSocket API.
- **Firmware deployed** — OTA triggered (node rebooted). USB flash also performed by user. Version string unchanged (`0.1.1+20260516-0749`) — version not bumped this session.
- **Docs updated** — FIRMWARE_ARCHITECTURE.md: added `/log/cal_sessions.csv` to SD layout, `cal_session` to MQTT topics table. MQTT_TOPIC_MAP.md: `twwp/<id>/cal_session` row added.

**Key decisions recorded:**
- OLED cal mode is fully auto-detected from sensor driver state — no external setCalMode() call needed. Cleaner than managing a separate flag.
- Fast cal publish is unbuffered (`netMqtt_publish` not `netMqtt_publishSub`) — time-sensitive live feedback, no value in buffering missed updates.
- WQ cal dates are not logged as cal_sessions — they're manually entered date strings, not software calibration events with old/new numeric values.
- Dashboard url_path is `wh-001` (hyphen), not `wh_001` (underscore). Different from MQTT node ID convention.

**Known gaps / follow-up:**
- Firmware version string not bumped — new binary indistinguishable from old by version alone. USB flash confirmed new code is running.
- OTA validation window (60s) blocked first OTA trigger (node had uptime <60s). Second trigger succeeded.
- `tds_pre_ro_raw_ec` entity ID on dashboard needs verification against actual HA entity name (may be `tds_pre_ro_ec_raw`).
- `sensor.twwp_wh_001_flow_rate_output` and `sensor.twwp_wh_001_flow_rate_input` entity IDs in calibration cards need verification — may differ from actual discovery names.

### Session 2026-05-16 — Docs pass + v2 mock data restructure

**Scope:**
- **Docs pass completed** — FIRMWARE_ARCHITECTURE.md updated: driver inventory (net_ap, display_oled, rs485_mux, sensor_tds_meter added), SD layout (upload_token.json + upload.html added to /config/), node.json schema (ap block added), data CSV column header expanded (voltage + WQ + TDS columns), MQTT topics summary (ota_state + wq_config added). MQTT_TOPIC_MAP.md updated: 30 missing command keys added to main table (upload portal, flow cal wizard, sensor model, WQ cal dates, TDS cal wizard). USER_OPERATIONS.md updated: M-Upload.5 moved from "in progress" to "implemented" (v2 frontend).
- **v2 mock data restructure** — Node IDs standardised to `wh-001` format (hyphen, 3-digit zero-padded) across all frontend files (mockData.js, App.jsx, ChatView.jsx, TapMapView.jsx, AuthContext.jsx, ProfileView.jsx). Tauranga nodes added: wh-001 (19 Cornwall St, Gate Pa — real first waterhouse), wh-002 (Greerton), wh-003 (Mt Maunganui, offline). Auckland pilot nodes renumbered wh-005 through wh-008 (content unchanged). Scholars, sponsors, channels, messages, tasks all updated to match.
- **M-LiveData + M-Deploy tasks added to TASK_QUEUE.md** — Notes on wh-001 live data integration (ID translation wh_001↔wh-001, frontend data layer decision, field mapping) and future node deployment wizard (server provisioning script, SD card checklist, in-app admin flow, NODE_DEPLOYMENT.md).

**Key decisions recorded:**
- Frontend node IDs use hyphens (`wh-001`). Firmware/MQTT use underscores (`wh_001`). Bridge layer must translate.
- wh-001 mock data will be replaced with live sensor data when node deploys to Gate Pa. Other nodes stay mock until their hardware exists.
- Deployment wizard is a future milestone (M-Deploy) — not blocking current work.

**Known gaps to fix before production:**
- UFW inactive on Hetzner — all ports exposed.
- Mosquitto 1883 with allow_anonymous — needs removal or confirmed internal-only.
- wh_001 upload token not yet written to SD card.
- M-Upload.4 QR code label not yet generated.

---

### Session 2026-05-16 (continued) — Server security hardening ✓

- **UFW enabled on Hetzner** — default deny incoming, allow 22/80/443/8883. Ports 1883/3000/8181 blocked externally. No service disruption.
- **Mosquitto 1883 listener removed** — config rewritten to 8883-only TLS. Anonymous plaintext access eliminated. Node (wh_001) and relay reconnected on 8883 immediately.
- **Password file secured** — `chown 1883:1883` + `chmod 0600` on host path `/home/kenny/projects/mqtt/config/passwords`. Fixed via host bind-mount (container was crash-looping so couldn't exec in).
- **OTA confirmed unaffected** — trigger on 8883, firmware download on 443, both open.
- **Infrastructure registry updated** — UFW rules, mosquitto config notes, password file path and permissions all documented in `~/.twwp/INFRASTRUCTURE.md`.
- **TASK_QUEUE.md updated** — Server Security Hardening section marked done.

### Session 2026-05-22 — 3rd Flow Sensor + Tank Monitoring + Google Sheets Bridge

**Scope:** Added Ch3 (DWS-MH-02, GPIO7) for RO-to-tank fill line, software tank monitoring with volume integration, Google Sheets bridge for calibration session logging, and extended HA dashboard.

**System topology:** Ch1=output(tap), Ch2=input(total RO), Ch3=RO-to-tank fill. Derived: grey_waste=Ch2−Ch3, ro_efficiency=Ch3/Ch2×100%.

**Changes:**
- Firmware: Ch3 fully wired in sensor_flow.cpp (ISR, statics, K-table, cal state machine, all getters/setters)
- New driver: `src/tank_monitor.cpp/.h` — Ch3-in minus Ch1-out integration; self-corrects on full; NVS persist
- `src/main.cpp` — ch3+tank heartbeat, HA discovery, derived sensors, DATA_LOG_HEADER extended
- `src/display_oled.cpp` — migrated from simulation to tankMonitor; Ch3 used for real fill rate
- `src/net_mqtt.cpp` — setBufferSize 8192 (was 4096, failed with ~5.8KB 3-ch heartbeat)
- `docs/LOVELACE_DASHBOARD.yaml` — Tank section (Overview), Ch3+RO Efficiency (Flow Data), Ch3 cal wizard + Tank cal + K-table card (Calibration); 139 entity refs, 0 broken
- Excel workbook — DWS-MH-02 added to SensorConfig, CalibrationLog + TankCalibration sheets added
- Hetzner — `cal-sheet-bridge` Docker service deployed; awaits `SHEETS_WEBHOOK_URL` in .env

**Firmware verified:** `0.1.1+20260521-1217`, all ch3/tank/derived fields confirmed in MQTT heartbeat.

**Key decisions:**
- GPIO7 repurposed from future pressure sensor (pressure TBD on free pin later)
- Tank full = Ch3 flow <0.05 L/min for 30s AND level ≥ 90% capacity → snap level to capacity
- MQTT buffer 8192 bytes sufficient; heartbeat payload ~5.8KB

### Session 2026-05-22 (continued) — Sensor wiring reorder + session WQ snapshot + dashboard update

**Scope:** Ch1/Ch2/Ch3 sensor model reassignment (DWS-MH-02 now Ch1/GPIO4, USN-HS06PE=Ch2, USN-HS06PS=Ch3), session WQ snapshot added to each tap session record, HA dashboard Sessions tab extended with WQ columns, Excel workbook cleaned up (Google Sheets references removed, DWS-MH-02 SensorConfig row completed).

**Changes:**
- `include/config.h` — sensor model defaults updated: Ch1=DWS-MH-02 (K=780), Ch2=USN-HS06PE (K=5500), Ch3=USN-HS06PS (K=20700)
- `src/session_flow.cpp` — `SessionRecord` extended with `wqPh`, `wqOrpMv`, `wqEcUsCm`, `wqTdsPpm`, `tdsPrePpm`, `tdsPostPpm`, `userId`; `SESSION_LOG_HEADER` updated; `finaliseSession()` snapshots Remin zone YiErYi + TDS meter readings at session end; `publishRecentSessions()` buffer bumped 2048→4096; `pushRecentSession()` extended; SD load/save updated; MQTT payload includes all WQ fields (null-skipped if sensor offline)
- `docs/LOVELACE_DASHBOARD.yaml` — Sessions flex-table extended with Pre TDS, Post TDS, Remin TDS, pH, ORP, User columns; font-size reduced 0.85→0.80em; pushed to HA via WebSocket API
- `reference/miscellaneous/flow_sensor_calibration_v106.xlsx` — DWS-MH-02 SensorConfig row completed (unmerged, filled); CalibrationLog "Google Sheets bridge" note removed; formula-derived K-table reference rows added (rows 5–9)
- Firmware `0.1.1+20260521-1412` deployed via OTA — confirmed running with new sensor model assignments

**Key decisions:**
- Google Sheets / cal-sheet-bridge / Apps Script all abandoned per user direction. Excel is the sole calibration data store.
- WQ snapshot uses `NAN`/`-32768` sentinels for unavailable sensors — omitted from JSON, blank in CSV, shown as `—` in dashboard
- `userId` = 0 (anonymous) hardcoded for now; TapLock/app integration populates this in future
- Sensor wiring is now: Ch1=DWS-MH-02 user tap (GPIO4), Ch2=USN-HS06PE RO output (GPIO5), Ch3=USN-HS06PS RO input/grey water (GPIO7)

**Known gaps:**
- node.json on SD card still has old sensor model assignments — update via MQTT `set_sensor_model_1/2/3` commands or rewrite SD file
- WQ session snapshot fields will be `—` in dashboard until YiErYi RS485 sensor is physically connected and responding (currently `wq_remin_fail_count` > 0)
- DWS-MH-02 K-table in firmware uses formula-derived defaults — update via cal wizard using field K values now in Excel
- No Low-band data for DWS-MH-02 yet (<0.4 LPM — important for slow fill-line accuracy)

### Session 2026-05-26 — DWS-MH-02 field calibration data recovery + Excel entry

**Scope:** Recovered 3 DWS-MH-02 calibration runs from 2026-05-18 via InfluxDB and entered into Excel workbook. Also documented InfluxDB query path for future use.

**Data recovered from InfluxDB (db=twwp_ha):**
- Run 1 (07:01–07:04, 206s, ~1.46 LPM, High band): 4697 pulses → K_suggested = **939.4**, accepted 939
- Run 2 (07:41–07:47, 330s, ~0.91 LPM, Medium band): 4372 pulses → K_suggested = **874.4**, accepted 874
- Run 3 (07:49–07:53, 244s, ~1.23 LPM, High band): 4581 pulses → K_suggested = **916.2**, accepted 916 (currently active)
- All 3 runs: ref_vol = 5.0 L (confirmed exact from pulses ÷ suggested_K = 5.000)
- Prior K before run 1 was 500 (firmware default before any cal)

**Key finding:** Field K values (874–939) are 12–22% higher than formula-derived K-table (780–860 from F=15Q−2). Field data must be used for the K-table, not the formula.

**Excel changes (`reference/miscellaneous/flow_sensor_calibration_v106.xlsx`):**
- **5L Baseline rows 18–20**: MH02 Medium/High/"Very High" filled with field run K values, duration, ref vol
- **CalibrationLog rows 11–13**: 3 field runs entered (old K, new K, pulses, flow rate, notes)

**InfluxDB query pathway (for future use):**
```bash
TOKEN="apiv3_MW6BVnPLQ7_9_rBHxf1ZnPm-_5coXT4cuc75iZ-4ftOp_Mjlk6vjMJP7e2__DvQRhgTZ3K0YCV0uklGfoD-mtA"
# Run from local machine via Hetzner SSH + Grafana container (twwp-monitoring network):
ssh -i ~/.ssh/hetzner_ed25519 kenny@91.98.133.15 \
  "docker exec twwp-grafana curl -s 'http://influxdb:8181/api/v3/query_sql' \
   -H 'Authorization: Bearer $TOKEN' \
   -H 'Content-Type: application/json' \
   -d '{\"db\":\"twwp_ha\",\"q\":\"SELECT ...\",\"format\":\"jsonl\"}'"
```
- Database: `twwp_ha` (HA recorder data). Token in `/home/kenny/projects/twwp-monitoring/.env`.
- Relevant tables: `pulses/L` (applied_k, suggested_k), `pulses` (cal_pulses entity), `sensor.output_flow_output_cal_state`

## In progress
Uncommitted deployment-readiness stabilization pass. Firmware changes are in:
- `include/config.h`
- `src/main.cpp`
- `src/net_ota.{h,cpp}`
- `src/tank_monitor.{h,cpp}`
- `src/display_oled.cpp`

Docs are being synchronized in the same pass and must land together before any commit.

## Next step

1. **Finish doc sync** — update `TASK_QUEUE.md`, `USER_OPERATIONS.md`, `MQTT_TOPIC_MAP.md`, `COMPONENTS.md`, `PIN_ALLOCATION.md`, and any remaining dashboard labels so they match the implemented canonical map and the new `status` / `status_diag` / `status_cfg` contract.
2. **Live-verify the channel map** — upload to the node, confirm serial output + MQTT + HA entity names, and verify tank level rises on Ch2 flow and falls on Ch1 flow.
3. **Live-verify OTA safety** — run one good MQTT-driven OTA, one bad-MD5 OTA, and confirm `ota_validation_pending`, `ota_rolled_back`, and retained `twwp/<id>/ota_state` behavior.
4. **Measure heartbeat reduction** — capture real payload sizes / cadence before and after the status split and record the result here.
5. **Apply field K-table values** — update the DWS-MH-02 (`Ch1`) K-table from recovered field data and complete the low-band run when hardware is available.

## Tool last used
claude-code

## Updated
2026-06-01
