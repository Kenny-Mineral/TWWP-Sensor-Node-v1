# Flow Sensor Firmware Improvements — Implementation Plan

**Date:** 2026-04-30
**Source:** Research in `/home/kenny/Documents/Waveshare build TWWP/reference/miscellaneous/`
**References:** `flow_pulse_firmware_improvements.md`, `flow_sensor_calibration_v106.xlsx`

---

## Current State Summary

The firmware already has the two most important foundations correct:
- **ISR-driven pulse counting** with `IRAM_ATTR` and `volatile` counters
- **Atomic snapshot + clear** with `noInterrupts()`/`interrupts()` pair

What is missing are the improvements identified during calibration testing that address sensor non-linearity, signal noise, measurement accuracy, and HA configurability.

---

## Calibration Data (from flow_sensor_calibration_v106.xlsx)

| Sensor | Channel | Nominal K | K at Low Flow | K at Med Flow | K at High Flow | Variation |
|--------|---------|-----------|---------------|---------------|----------------|-----------|
| USN-HS06PE (Output) | 1 (GPIO4) | ~5,500 | ~4,972 | ~5,468 | ~5,476 | **~10%** |
| USN-HS06PS (Input) | 2 (GPIO5) | ~20,700 | ~21,120 | ~20,818 | ~21,105 | ~2% |

**Critical finding:** The PE (output) sensor shows ~10% K-factor variation across its flow range. A single fixed K causes systematic measurement error. The PS (input) sensor is relatively linear (~2% variation).

**Current firmware defaults are wrong:** `kFactor1 = kFactor2 = 38.0f` — these are placeholder values from an older sensor datasheet. The calibrated defaults should be ~5,500 for channel 1 and ~20,700 for channel 2.

---

## Implementation Sequence

### Phase 1: Foundation (files: `sensor_flow.h`, `sensor_flow.cpp`)
_Safest changes — pure additions, no existing logic altered_

#### 1.1 ISR Debounce Guard
- Add `DEBOUNCE_US 1000` (1 ms minimum between valid pulses)
- Modify ISRs to track `last_pulse_time_us` per channel and reject pulses arriving too fast
- Rationale: Long sensor cables can pick up noise; this prevents phantom pulse counting
- **Files:** [`src/sensor_flow.cpp`](src/sensor_flow.cpp:48)

#### 1.2 Low-Flow Cutoff
- Add `MIN_PULSES_PER_INTERVAL` threshold (2 for PS channel, 1 for PE channel)
- If fewer pulses arrive in the 1-second window, treat as zero flow
- Rationale: Prevents spurious volume accumulation from thermal expansion or residual pressure jitter
- **Files:** [`src/sensor_flow.cpp`](src/sensor_flow.cpp:167) — inside `sensorFlow_loop()`

#### 1.3 Pulse Total Accumulation (uint64_t)
- Add `static uint64_t totalPulses1, totalPulses2` as the authoritative volume source
- Accumulate raw pulses into these totals (never reset)
- Persist to NVS every 10 s (alongside current float totals)
- Persist to SD every 60 s
- Rationale: Volume = total_pulses / K. Raw pulses have zero rounding error; float volume accumulates rounding noise over time.
- **Files:** [`src/sensor_flow.cpp`](src/sensor_flow.cpp:11)

---

### Phase 2: Multi-Point K-Factor (files: `sensor_flow.h`, `sensor_flow.cpp`, `include/config.h`)
_Medium risk — adds new logic alongside existing_

#### 2.1 K-Table Data Structure
```cpp
struct FlowKPoint {
    float flowLpm;        // flow rate at this calibration point (L/min)
    float kPulsesPerL;   // K factor at this flow rate
};
// Up to 5 points per channel, stored in node.json
```

- Replace single `kFactor1`/`kFactor2` with K-table arrays
- Maintain backward compatibility: if `node.json` has `k_factor_1` but no `k_table_1`, load single K as a 1-point table
- New `node.json` schema:
```json
{
  "flow": {
    "k_factor_1": 5500,
    "k_factor_2": 20700,
    "k_table_1": [
      {"flow_lpm": 0.42, "k": 4972},
      {"flow_lpm": 0.99, "k": 5468},
      {"flow_lpm": 1.42, "k": 5476}
    ],
    "k_table_2": [
      {"flow_lpm": 0.42, "k": 21120},
      {"flow_lpm": 0.99, "k": 20818},
      {"flow_lpm": 1.38, "k": 21104}
    ]
  }
}
```

#### 2.2 Linear Interpolation Function
```cpp
float interpolateK(float flowLpm, const FlowKPoint* table, int len);
```
- Clamp below first point → return first K
- Clamp above last point → return last K  
- Linear interpolate between bracketing points

#### 2.3 Moving-Average Flow Rate
- Ring buffer of last 5 one-second flow readings per channel
- Compute smoothed flow rate before K-table lookup
- Prevents K-value oscillation from turbulent flow jitter
- Configurable window size via `#define FLOW_AVG_WINDOW 5`

#### 2.4 Volume-First Calculation
- Volume is now: `totalPulses / interpolatedK(smoothedFlowRate)`
- This replaces the current: `Σ(litres_per_interval)` where each interval uses a potentially different K
- Period subtotals (today/week/month/year) continue to accumulate litres from the 1-second windows, but now each window's litres use the interpolated K

**Files affected:** [`src/sensor_flow.h`](src/sensor_flow.h), [`src/sensor_flow.cpp`](src/sensor_flow.cpp), [`include/config.h`](include/config.h)

---

### Phase 3: HA Integration (files: `src/main.cpp`, `docs/MQTT_TOPIC_MAP.md`)
_Medium risk — extends MQTT payloads and HA discovery_

#### 3.1 Raw Pulse Data in Status Payload
Add to [`publishM0Status()`](src/main.cpp:37):
```json
{
  "pulses_raw_1": 123456789,
  "pulses_raw_2": 987654321,
  "k_applied_1": 5476.0,
  "k_applied_2": 20818.0,
  "flow_avg_window_1": 0.85,
  "flow_avg_window_2": 0.62
}
```
Rationale: Raw pulse totals enable post-hoc recalibration without losing historical data.

#### 3.2 HA Discovery for Raw Pulse Entities
- `sensor.twwp_<id>_pulses_raw_1` — diagnostic, total_increasing, unit: pulses
- `sensor.twwp_<id>_pulses_raw_2` — diagnostic, total_increasing, unit: pulses
- `sensor.twwp_<id>_k_applied_1` — diagnostic, measurement, unit: pulses/L
- `sensor.twwp_<id>_k_applied_2` — diagnostic, measurement, unit: pulses/L

#### 3.3 HA Discovery for K-Table Configuration
Per-channel number entities for up to 5 flow points:
- `number.twwp_<id>_k_table_1_flow_1` through `_flow_5` (L/min, 0–10, step 0.01)
- `number.twwp_<id>_k_table_1_k_1` through `_k_5` (pulses/L, 1–99999, step 1)
- Same for channel 2

Alternatively (simpler): text entity that accepts JSON array:
- `text.twwp_<id>_k_table_1` — accepts `[{"f":0.42,"k":4972},{"f":0.99,"k":5468}]`
- This is simpler to implement and less cluttered in HA

#### 3.4 Command Handler Extensions
New keys in [`handleCmd()`](src/main.cpp:800):
| Key | Type | Effect |
|-----|------|--------|
| `set_k_table_1` | JSON string | Replace channel 1 K-table, save to node.json |
| `set_k_table_2` | JSON string | Replace channel 2 K-table, save to node.json |
| `set_debounce_us_1` | int (100–10000) | Set debounce for channel 1 |
| `set_debounce_us_2` | int (100–10000) | Set debounce for channel 2 |
| `set_flow_avg_window` | int (1–20) | Set moving average window size |

#### 3.5 Existing K-Factor Number Entity Behaviour
- When user changes `k_factor_1` via HA number entity → firmware updates single-point K table (1-point table with that K value) AND sets that as the nominal K
- Backward compatible: existing `set_k_factor_1` command still works

**Files affected:** [`src/main.cpp`](src/main.cpp), [`docs/MQTT_TOPIC_MAP.md`](docs/MQTT_TOPIC_MAP.md)

---

### Phase 4: Documentation (files: `docs/*.md`)
_Low risk — documentation only_

#### 4.1 Update `docs/MQTT_TOPIC_MAP.md`
- Add raw pulse topic mappings
- Add K-table command keys
- Add debounce/window config keys

#### 4.2 Update `docs/COMPONENTS.md`
- Correct K-value defaults in sensor library table (PE: 5,500, PS: 20,700)
- Add calibration data summary
- Update `node.json` schema example

#### 4.3 Update `docs/USER_OPERATIONS.md`
- Add serial commands for K-table management (if serial commands are added)
- Add calibration procedure reference
- Add serial commands: `set_k_table_1 <json>`, `get_k_table_1`

#### 4.4 Update `docs/FIRMWARE_ARCHITECTURE.md`
- Update SensorData model to include raw pulse fields
- Update data persistence layers for raw pulses
- Update `node.json` schema
- Update data log CSV header

#### 4.5 Update `docs/TASK_QUEUE.md`
- Mark this improvement task in the M1 section

**Files affected:** `docs/MQTT_TOPIC_MAP.md`, `docs/COMPONENTS.md`, `docs/USER_OPERATIONS.md`, `docs/FIRMWARE_ARCHITECTURE.md`, `docs/TASK_QUEUE.md`

---

## Files Changed Summary

| File | Phase | Change Type |
|------|-------|-------------|
| [`src/sensor_flow.h`](src/sensor_flow.h) | 1, 2 | Add K-table struct, new function declarations, raw pulse getters |
| [`src/sensor_flow.cpp`](src/sensor_flow.cpp) | 1, 2, 3 | ISR debounce, low-flow cutoff, uint64_t totals, K-table, moving avg, interpolation |
| [`src/session_flow.cpp`](src/session_flow.cpp) | 2 | Use smoothed flow rate for session detection (optional — current rate works) |
| [`include/config.h`](include/config.h) | 2 | Add K-table defaults, debounce/window defines |
| [`src/main.cpp`](src/main.cpp) | 3 | Status payload expansion, HA discovery, command handler extensions |
| [`docs/MQTT_TOPIC_MAP.md`](docs/MQTT_TOPIC_MAP.md) | 4 | New topics and command keys |
| [`docs/COMPONENTS.md`](docs/COMPONENTS.md) | 4 | Corrected K defaults, calibration data |
| [`docs/USER_OPERATIONS.md`](docs/USER_OPERATIONS.md) | 4 | New serial commands, calibration procedure |
| [`docs/FIRMWARE_ARCHITECTURE.md`](docs/FIRMWARE_ARCHITECTURE.md) | 4 | Updated schemas, data model, persistence |
| [`docs/TASK_QUEUE.md`](docs/TASK_QUEUE.md) | 4 | Mark M1 improvements |

---

## Review Checklist

### Code Reviewer checks:
1. [ ] ISR debounce: verify `micros()` in ISR is safe on ESP32-S3, verify `volatile` on `last_pulse_time` variables
2. [ ] K-table interpolation: verify edge cases (empty table, single point, flow outside range)
3. [ ] Moving average: verify ring buffer initialization (all zeros on first boot)
4. [ ] uint64_t pulse totals: verify NVS read/write for 64-bit values (Preferences supports `putUInt64`/`getUInt64`)
5. [ ] Backward compatibility: verify old `node.json` without `k_table_*` still works
6. [ ] No delay() anywhere, no blocking >10s
7. [ ] All pin references use `include/pins.h` defines
8. [ ] No `setInsecure()` — TLS only

### IoT Engineer checks:
1. [ ] Calibration data matches Excel — PE K varies 4,972–5,608, PS K varies 20,818–21,120
2. [ ] HA entities appear correctly after firmware flash
3. [ ] K-table updates from HA propagate to node.json and take effect
4. [ ] Session tracking still works with new flow rate calculations
5. [ ] SD log CSV header updated with new columns
6. [ ] NVS migration path: old `flow` namespace `t1`/`t2` keys still read correctly
7. [ ] Memory budget: K-table arrays, ring buffers, uint64_t totals fit in RAM
