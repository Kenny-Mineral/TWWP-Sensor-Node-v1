# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done

### Session 2026-05-15 — Sensor model registry, calibration system, Grafana dashboards

**Scope:**
- **Flow sensor model registry** — compile-time registry (USN-HS06PE, USN-HS06PS, DWS-MH-02). DWS-MH-02 has 5-point K-table from F=15Q−2 formula. Model selectable per channel via HA `select` entity or `set_sensor_model_1/2` MQTT cmd. Persisted to node.json; explicit k_factor/k_table still override model defaults.
- **Flow K-factor calibration wizard** — per-channel begin/commit/accept/abort state machine. Set reference volume → Start → fill container → Commit → see suggested K → Accept/Abort. MQTT status: `cal_state_1/2`, `cal_suggested_k_1/2`, `cal_pulses_since_start_1/2`, `cal_ref_vol_1/2`. HA buttons + number entity per channel.
- **TDS/EC calibration** — per-zone software correction factor (NVS persisted, `tds_cal` namespace). Wizard: set ref EC → begin → commit (snapshots current raw EC) → suggested factor → accept/abort. Direct `set_tds_pre_ro/post_ro_ec_cal_factor` also available. Raw EC exposed alongside calibrated values.
- **USER_OPERATIONS.md** — full calibration playbook section added covering flow, Pre/Post-RO EC, Yieryi, and voltage.
- **Grafana dashboards** — all 4 rebuilt with correct InfluxDB entity IDs. Previous dashboards had wrong entity IDs (used non-existent Yieryi pre/post-RO entities for TDS; non-existent `%` measurement for battery/WiFi). Water Quality now uses `pre_ro_tds_meter_*` / `post_ro_tds_meter_*` for Pre/Post-RO, `remineralised_water_quality_*` for Remin. EC panels omitted (empty in InfluxDB). Dashboard JSON provisioned on server at `/home/kenny/projects/twwp-monitoring/grafana/provisioning/dashboards/`.

**Key clarification:** Pre-RO and Post-RO water quality comes from WROOM-32 EC/TDS meter via RS485 (`tds_pre_ro_*` / `tds_post_ro_*` fields). Yieryi pre/post-RO meters are not physically connected — only Remin Yieryi is live.

---

## In progress
none

## Next step

M5 — Confirm Modbus addresses on Post-RO and Remin Yieryi meters so those zones can be enabled in node.json.

## Tool last used
claude-code

## Updated
2026-05-15 23:45
