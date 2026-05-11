# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done

### Session 2026-05-12 — Phase 3 MQTT OTA end-to-end test

**Scope:** Full MQTT OTA validation. Both happy-path and failure-path confirmed.

**Results:**
- Good-MD5 test: node downloaded firmware, rebooted, came back online, validated within 60s window → `ota_state: 0` (IDLE) ✓
- Bad-MD5 test: update rejected with `ota_state: 5` (FAILED), error `"MD5 mismatch expected=000...000 got=4001f3c8..."`, no reboot ✓
- LWT sequence confirmed: `online → offline → online` during OTA reboot

**Infrastructure fixed this session:**
- Created `/var/www/twwp/firmware/` on server via Docker (kenny lacks sudo — docker group used instead)
- Opened port 443 in Hetzner cloud firewall (`firewall-1`) — was previously blocked, MQTT 8883 was already open
- Firmware hosted at `https://twwp-iot.duckdns.org/firmware/firmware.bin` confirmed 200 OK

**Files updated this session:**
- `docs/USER_OPERATIONS.md` — fixed MQTT OTA Step 3 (SCP path was wrong; added Docker copy command for `/var/www/twwp/firmware/`; noted port 443 Hetzner firewall requirement)
- `docs/TASK_QUEUE.md` — ticked M5 OTA validation checkbox

---

### Session 2026-05-12 — M6 Phase 2 bench test + docs update

**Scope:** Hardware bench verification of the dual EC/TDS meter integration (M6). All Phase 2 checks passed. USER_OPERATIONS.md and TASK_QUEUE.md updated.

**Bench test results:**
- Boot lines confirmed: `[MUX] RS485 UART1 ready`, `[TDS] EC/TDS meter driver ready`, `[YIERYI] Modbus driver ready`
- `[TDS] P1/P2` frames arriving every ~3s ✓
- HA receiving all 6 TDS entities with real values ✓
- YiErYi water quality entities present in HA — bus coexistence confirmed ✓
- No SD failure alerts published — writes assumed succeeding silently ✓
- Grafana: TDS data not yet visible — dashboard panels not created (separate task)

**Wiring issue resolved:** WROOM-32 RS485 module had DI and RO swapped on first attempt. Correcting DI→TX, RO→RX fixed the no-frames symptom immediately.

**Calibration note:** TDS readings (~17ppm) lower than expected (~52ppm tap water) — default EC×0.5 conversion factor. Probes need calibration with standard solutions for absolute accuracy. Relative P1 vs P2 comparison is accurate.

---

## In progress
none

## Next step — M5 open items + Grafana dashboards

1. Confirm Modbus addresses for post-RO and remin YiErYi meters (use vendor software + USB-RS485 adapter)
2. Enable those zones in `/config/node.json` once addresses confirmed
3. Create Grafana dashboard panels: TDS fields and water quality overview
4. TDS probe calibration with standard solutions (separate from Grafana)

Also open (lower priority):
- M2: confirm pressure transducer model + PSI range before ordering
- Investigate ArduinoOTA LAN failure (router AP isolation suspected)
- Fix NVS `k_factor_2` stored as 2000 — send `{"k_factor_2": 20700}` via MQTT

## Tool last used
claude-code

## Updated
2026-05-12 15:30
