# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done
Phase 4 documentation updates for flow sensor improvements completed. All five documentation files updated to reflect ISR debounce, low-flow cutoff, multi-point K-table with linear interpolation, moving-average smoothing, uint64_t raw pulse totals, runtime-configurable debounce/window, HA discovery for new diagnostic and config entities, and corrected K-value defaults (PE: 5500, PS: 20700).

Files changed:
- [`docs/MQTT_TOPIC_MAP.md`](docs/MQTT_TOPIC_MAP.md) — removed stale K-factor sensor entries, added 6 new diagnostic entities (pulses_raw, k_applied, flow_avg_window), verified existing command keys and writable entity sections are correct
- [`docs/COMPONENTS.md`](docs/COMPONENTS.md) — corrected K-value defaults (38→5500, 200→20700), updated node.json schema with K-table/debounce/window fields, added calibration data summary
- [`docs/USER_OPERATIONS.md`](docs/USER_OPERATIONS.md) — added new heartbeat fields (k_applied, pulses_raw, flow_avg_window, k_table, debounce, session config, leak_suspect, waste_ratio), expanded HA diagnostic entities section
- [`docs/FIRMWARE_ARCHITECTURE.md`](docs/FIRMWARE_ARCHITECTURE.md) — updated SensorData model with raw pulse fields, persistence layers for uint64_t pulse totals, node.json schema, boot restore order
- [`docs/TASK_QUEUE.md`](docs/TASK_QUEUE.md) — added checked task in M1 for flow sensor improvements

All changes verified consistent with implemented code in [`src/sensor_flow.cpp`](src/sensor_flow.cpp), [`src/sensor_flow.h`](src/sensor_flow.h), [`include/config.h`](include/config.h), and [`src/main.cpp`](src/main.cpp).

## In progress
none

## Next step
Pick up the first unchecked task in [`docs/TASK_QUEUE.md`](docs/TASK_QUEUE.md:82): confirm the pressure transducer model and PSI range with the user before ordering.

## Tool last used
codex

## Updated
2026-04-30 13:20
