# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done
Session tracking + HA sub-device cards + flow reset (plan: 2026-04-27-session-tracking-ha-cards-reset). New session_flow module with 90s idle timeout state machine, MQTT session-end publish to twwp/<id>/session, SD session log to /log/sessions.csv. HA entities split into 4 device cards (main node, RO Output, RO Input, Leak Sensor) via via_device links. Flow reset commands via MQTT, HA buttons, and serial console for per-channel and both-channel today/totals resets.

## In progress
none

## Next step
M2 — Confirm pressure transducer model + PSI range with user before ordering.

## Tool last used
codex

## Updated
2026-04-27 19:21
