# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done
Phase 2 flow sensor calibration improvements implemented in [`src/sensor_flow.cpp`](src/sensor_flow.cpp:1), [`src/sensor_flow.h`](src/sensor_flow.h:1), and [`include/config.h`](include/config.h:1): multi-point `FlowKPoint` tables, backward-compatible single-K fallback loading from [`/config/node.json`](docs/USER_OPERATIONS.md:309), linear [`interpolateK()`](src/sensor_flow.h:9), 5-sample moving-average flow smoothing, updated default nominal K values (5500 / 20700), and volume-first lifetime/subtotal calculations based on interpolated K. [`pio run`](platformio.ini) builds clean (RAM 17.3%, Flash 21.3%). [`docs/USER_OPERATIONS.md`](docs/USER_OPERATIONS.md:307) updated for the new calibration schema.

## In progress
none

## Next step
Pick up the first unchecked task in [`docs/TASK_QUEUE.md`](docs/TASK_QUEUE.md:81): confirm the pressure transducer model and PSI range with the user before ordering.

## Tool last used
codex

## Updated
2026-04-30 23:55
