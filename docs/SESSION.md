# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done
Session tracking overhaul complete: configurable idle timeout (5–100 s, HA number entity, NVS-persisted), configurable flow threshold (0.01–0.5 L/min, HA number entity, NVS-persisted), 10-session ring buffer with SD persistence and retained MQTT publish to `twwp/<id>/sessions_recent`, leak-suspect binary sensors per channel, HA discovery for all new entities, status heartbeat published immediately on MQTT reconnect (fixes "HA gets weird after power loss"). HACS installed on Hetzner HA server (fixed wrong docker volume path via `docker cp`). Firmware build clean (RAM 17.2%, Flash 21.2%). Firmware NOT yet flashed — board ready.

## In progress
- M docs/MQTT_TOPIC_MAP.md
- M include/config.h
- M src/main.cpp
- M src/sensor_flow.cpp
- M src/sensor_flow.h
- M src/session_flow.cpp
- M src/session_flow.h
- ?? docs/LOVELACE_SESSIONS_CARD.yaml

## Next step
Flash firmware (`pio run --target upload`), then complete HACS UI setup in HA (Settings → Devices & Services → Add Integration → HACS → GitHub login), install flex-table-card via HACS Frontend, add Lovelace card from `docs/LOVELACE_SESSIONS_CARD.yaml`, verify session entities appear on main device card (not Diagnostics).

## Tool last used
claude-code

## Updated
2026-04-29 00:00
