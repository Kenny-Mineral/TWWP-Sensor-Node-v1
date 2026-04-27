# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done
Implemented M1 flow sensor driver — interrupt-driven pulse counter on GPIO4/5, K-factor loaded from node.json, two-layer persistence (NVS every 10s + SD every 60s), HA discovery for all flow entities, time-series CSV data log to /data/YYYY-MM-DD.csv, and updated all architecture/operations docs.

## In progress
Uncommitted changes:
- docs/FIRMWARE_ARCHITECTURE.md
- docs/PIN_ALLOCATION.md
- docs/TASK_QUEUE.md
- docs/USER_OPERATIONS.md
- include/config.h
- include/pins.h
- src/main.cpp
- src/store_sd.cpp
- src/store_sd.h
- src/sensor_flow.cpp (new)
- src/sensor_flow.h (new)
- src/sensor_flow_stub.cpp (deleted)
- src/sensor_flow_stub.h (deleted)

## Next step
M2 — Confirm pressure transducer model + PSI range with user before ordering.

## Tool last used
claude-code

## Updated
2026-04-27 19:30
