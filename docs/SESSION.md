# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done

### Session 2026-05-13 — MQTT OTA confirmed end-to-end

**Scope:** No firmware changes. Confirmed MQTT-driven OTA working in practice — firmware built, SCP'd to Hetzner server, triggered via `mosquitto_pub`, device downloaded and rebooted successfully. Updated USER_OPERATIONS.md Step 3 to reflect the correct SCP workflow (direct to `/var/www/twwp/firmware/`, no Docker workaround needed — directory is now owned by kenny). ArduinoOTA LAN investigated — blocked by AP client isolation, not a firmware issue.

**Key findings:**
- SSH to server: always `ssh kenny@100.67.244.37` (Tailscale) — not root@91.98.133.15
- kenny has full sudo on server (password in `~/.twwp/INFRASTRUCTURE.md`)
- `/var/www/twwp/firmware/` is now owned by kenny — direct SCP works

---

## In progress
none

## Next step — M3 bench test (blocked on hardware)

Waiting for valve and replacement LED. When hardware arrives, flash and run the 9 verification checks:
1. `set_trigger_source: manual` → flow no longer opens valve
2. `set_trigger_source: flow` → flow opens valve again
3. `set_valve_idle_timeout: 10` → safety close 10s after flow stops
4. `set_valve_max_open: 10` → safety close 10s after open even with flow
5. `set_valve_timeout_disable_auto: true` → `valve_auto` → false after safety close
6. `set_valve_timeout_alert: true` → `VALVE_SAFETY_CLOSE` alert on `twwp/wh_001/alert`
7. `set_valve_type: ball_valve` → warning logged, solenoid fallback behaviour
8. All 6 new fields present in heartbeat JSON
9. All 6 new HA entities on device card (2 select + 2 number + 2 switch)

Next firmware task (no hardware needed): OLED screen + button — brainstorm notes to be added next session.

## Tool last used
claude-code

## Updated
2026-05-13 20:15
