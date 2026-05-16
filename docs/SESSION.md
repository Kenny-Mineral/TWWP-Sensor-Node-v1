# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done

### Session 2026-05-16 — Mobile upload portal firmware pass

**Scope:**
- **Firmware-side M-Upload implementation** — added `src/net_ap.{h,cpp}` for a concurrent STA+AP upload portal with local HTTP endpoints (`/`, `/api/buffer/stats`, `/api/buffer/fetch`, `/api/buffer/ack`), SD-backed upload token, auto-trigger on WiFi loss / weak RSSI, and MQTT commands `{"start_ap": true, "duration_s": ...}` plus `{"rotate_upload_token": true}`.
- **Status / display integration** — heartbeat now includes `ap_active`, `ap_ssid`, `ap_clients`, `ap_expires_s`, and `wifi_uptime_s`; OLED shows an `UPLOAD MODE` screen while the AP is active.
- **Buffer metadata support** — buffered MQTT files now persist `ts`, and SD helpers can report stats, fetch oldest buffered records, acknowledge uploaded batches, and serve `/config/upload.html` overrides from SD.
- **Docs / handoff** — updated `docs/USER_OPERATIONS.md`, `docs/MQTT_TOPIC_MAP.md`, and `docs/TASK_QUEUE.md` to document the local portal and to separate completed firmware work from the still-pending relay / Rails tasks.
- **Relay scaffold** — added a FastAPI-based `upload-relay` service in `/home/kenny/twwp-monitoring` with token validation, browser CORS for `192.168.4.1`, per-node rate limiting, `upload_leads.csv` capture, docker-compose wiring, and nginx setup notes.
- **Verification** — `pio run` passed on 2026-05-16 after the AP/upload changes.
  - Relay syntax check passed with `python3 -m py_compile /home/kenny/twwp-monitoring/upload-relay/app.py`.

**Known gap:** the node-side portal and relay scaffold are built, but the relay is not live-verified from this workspace because `docker` is not installed here. nginx routing and end-to-end browser upload still need deployment/runtime verification, and the Rails-side Tap-Map integration is untouched.

---

## In progress
Firmware/docs changes are uncommitted in the firmware repo, and relay-service scaffolding is uncommitted in `/home/kenny/twwp-monitoring`. The monitoring repo also has unrelated pre-existing Grafana dashboard edits that should stay out of any relay-only commit.

## Next step

M-Upload.2 deployment pass — bring up `/home/kenny/twwp-monitoring` with Docker on the target host, populate `upload_tokens.json`, wire nginx to `127.0.0.1:8000`, and test phone upload end-to-end before starting M-Upload.5 Rails work.

## Tool last used
codex

## Updated
2026-05-16 15:20
