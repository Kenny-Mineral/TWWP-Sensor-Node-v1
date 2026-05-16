# The Wholey Water Project — Initiative Context

This document gives developers and AI agents working on the firmware node the broader context of what TWWP is, who the users are, and how the firmware fits into the platform.

---

## What is TWWP

The Wholey Water Project (TWWP) is a community-owned water access platform. It deploys street-level Reverse Osmosis + UV filtered water stations — called **Waterhouses** or **Taps** — block by block in neighbourhoods. The goal is affordable, high-quality drinking water accessible to anyone in the community, operated sustainably through a membership model.

Each waterhouse is a physical kiosk or tap point with an integrated water treatment system (pre-filter, RO membrane, UV sterilisation, remineralisation) and an IoT monitoring node.

---

## Who are the users

**Members** — people who pay a subscription (via Stripe) to access TWWP water. They use the Tap-Map App (`app.thewholeywaterproject.com`) to locate taps, monitor their usage, and manage their account. Members are the primary audience for features like the "Sync offline data" button.

**Anonymous passers-by** — anyone who walks past a tap. They may not be members but can still interact with the node (e.g., scan a QR code to help upload buffered data). The upload portal is designed to convert these interactions into TWWP membership leads.

**Administrators / operators** — manage the fleet of nodes, review water quality data, and maintain the hardware. They access data via Grafana dashboards and Home Assistant.

---

## The Tap-Map App

- URL: `https://app.thewholeywaterproject.com`
- Marketing site: `https://www.thewholeywaterproject.com`
- Stack: Ruby on Rails + DaisyUI (Tailwind component library), hosted on Fly.io
- PWA-capable (mobile-web-app-capable manifest)
- Payments: Stripe
- Maps: Mapkick
- The app is under active redesign ("Tap-Map App v2") — a significant UI/UX upgrade is in progress in a separate workspace

The app is currently a web app (not on any app stores). It has real users and members.

---

## What the firmware node does

Each waterhouse has one sensor node (Waveshare ESP32-S3-RS485-CAN board) that:

- Monitors **water quality** — pH, ORP, EC, TDS, temperature across three zones (pre-RO, post-RO, remineralised) via YiErYi RS485 sensors and dual EC/TDS meters
- Monitors **flow rate and totals** — two Hall effect flow sensors per channel, with session tracking
- Monitors **supply voltage** — 12V battery via ADS1115 ADC
- Detects **leaks** — binary sensor on leak probe
- Gates the **water valve** — relay-driven actuator (currently flow-triggered for testing; future: app/QR/auth trigger)
- Publishes all data to MQTT over TLS, with SD card buffering when offline
- Serves an upload portal over WiFi AP when offline or manually triggered

---

## How the node connects to the platform

```
Sensor Node (ESP32-S3)
    │
    │  MQTT/TLS (port 8883)
    ▼
Mosquitto broker (Hetzner VPS — twwp-iot.duckdns.org)
    │
    ├──▶ Home Assistant (Tailscale: 100.67.244.37:8123)
    │         │
    │         └──▶ InfluxDB 3 Core (port 8181)
    │                   │
    │                   └──▶ Grafana dashboards (port 3000, Tailscale only)
    │
    └──▶ (future) Tap-Map App — node status, buffer count, last-seen
```

Node heartbeat publishes every 10 seconds to `twwp/<node_id>/status` with ~70+ fields covering all sensor readings, flow data, water quality, system health, and AP state.

MQTT topic map: `docs/MQTT_TOPIC_MAP.md`
SD card layout: `docs/FIRMWARE_ARCHITECTURE.md`

---

## Why offline buffering matters

Many waterhouse tap locations may have intermittent or no WiFi coverage — community taps are often in outdoor or semi-public spaces where home WiFi doesn't reach reliably. When the node loses WiFi, it buffers up to 500 MQTT messages to the SD card (`/buf/`).

Normally the buffer drains automatically when WiFi reconnects. But if the node is offline for an extended period (days), someone needs to manually relay that buffered data to the server.

The **Mobile Offline Buffer Upload** feature (M-Upload milestone) solves this: the node broadcasts a WiFi AP that any phone can join, download the buffered data, and upload it to the server via cellular. This makes every person who walks past a waterhouse a potential data relay — no app required.

---

## Key project paths

| Resource | Location |
|---|---|
| Firmware project | `/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/` |
| Server monitoring stack | `/home/kenny/projects/twwp-monitoring/` (Hetzner VPS) |
| Tap-Map App | `/home/kenny/Desktop/Original tap map layout fetch /` (v2 rebuild in progress) |
| Infrastructure registry | `~/.twwp/INFRASTRUCTURE.md` (never in git) |
| Session state | `docs/SESSION.md` |
| Task queue | `docs/TASK_QUEUE.md` |
