# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done

### Session 2026-05-16 — Relay deployment + Tap-Map v2 sync button

**Scope:**
- **M-Upload.2 deployed live** — upload-relay FastAPI service built and running on Hetzner VPS as `twwp-upload-relay` Docker container (on `web` network alongside nginx proxy manager). Fixed MQTT connection bug (proper `on_connect`/`on_disconnect` callbacks, `connect_async`). Fixed hairpin NAT issue via `extra_hosts: twwp-iot.duckdns.org → 172.18.0.5` (mqtt container IP). Added nginx location block for `/api/v1/node-upload` and `/api/v1/health` in `/home/kenny/projects/proxy/data/nginx/custom/http.conf`.
- **End-to-end verified** — `curl` smoke test returned `{"published":1,"failed":0}`. Health endpoint confirms `mqtt_connected: true`.
- **Credentials provisioned** — `twwp_relay` MQTT user added to mosquitto password file; `wh_001` upload token written to `/home/kenny/projects/twwp-monitoring/upload-relay/data/upload_tokens.json` on VPS. Token also needs to be written to SD card `/config/upload_token.json` on the physical node.
- **M-Upload.5 — Tap-Map v2 sync button** — added `SYNC_OFFLINE_DATA` permission to `roles.js` (member+). Added "Offline Data Sync" card to `TapDetailView.jsx` in `twwp-v2-frontend`. Two-state flow: idle (shows buffer count + amber "Sync offline data →" button) → triggered (shows WiFi SSID + "Open upload portal →" deeplink to `192.168.4.1/?member=1`). Verified in browser — both states render correctly.
- **USER_OPERATIONS.md updated** — relay credentials, how-to, curl test commands, token management, CRM leads log path all documented.
- **TASK_QUEUE.md updated** — added M-PowerLoss audit task, Server Security Hardening section (UFW inactive, 1883 exposed, password file permissions).
- **Infrastructure registry updated** — relay endpoint, credentials, tokens, Docker networking note, known security gaps.

**Known gaps to fix before production:**
- UFW is inactive on Hetzner — all ports exposed. Need to enable UFW and block everything except 22, 80, 443, 8883.
- Mosquitto 1883 listener has `allow_anonymous true` — needs to be removed or confirmed internal-only.
- wh_001 upload token not yet written to SD card (node was not powered on this session).
- M-Upload.5 MQTT publish (the backend action that actually triggers the node's AP) is not wired — the UI shows instructions, but no MQTT command is sent from the v2 app yet (needs backend when Rails/API is built).
- M-Upload.4 QR code label not yet generated.

---

## In progress
Nothing. Clean state.

## Next step

1. **Security pass** — enable UFW, block 1883 externally, fix mosquitto password file permissions. See "Server Security Hardening" section in TASK_QUEUE.md.
2. **SD card provisioning** — power on node, write wh_001 upload token to `/config/upload_token.json` on SD card: `{"token":"054634ef20ff45bad13b331904114cffb660e8d5a7525c692dc811261760d455"}`.
3. **End-to-end bench test** — trigger AP via MQTT, join phone to node WiFi, run upload through browser, confirm relay receives and publishes.

## Tool last used
claude-code

## Updated
2026-05-16
