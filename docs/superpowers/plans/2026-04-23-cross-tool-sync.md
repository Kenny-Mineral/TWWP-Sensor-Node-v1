# Cross-Tool Sync + Infrastructure Registry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire up SESSION.md-based session handoff, infrastructure registry, TWWP-specific Claude slash commands, a rewritten Roo mode, and rewritten Roo skill files so that Claude Code, Roo, and Codex all share context and enforce the same firmware rules.

**Architecture:** A single `docs/SESSION.md` file in the firmware project acts as the shared handoff state between all three tools. A `~/.twwp/INFRASTRUCTURE.md` file outside git holds all infrastructure credentials as a lookup reference. Claude Code gets three slash commands; Roo gets a fully rewritten TWWP-specific mode and skill files; Codex gets an `AGENTS.md`.

**Tech Stack:** Markdown files, Claude Code slash commands (`.claude/commands/`), Roo mode + skill directories (`.roo/`), git for commit verification.

---

## File Map

| Action | File |
|---|---|
| Create | `TWWP Sensor Node v1/docs/SESSION.md` |
| Create | `~/.twwp/INFRASTRUCTURE.md` |
| Create | `Waveshare build TWWP/CLAUDE.md` |
| Create | `Waveshare build TWWP/.claude/commands/twwp-start.md` |
| Create | `Waveshare build TWWP/.claude/commands/twwp-handoff.md` |
| Create | `Waveshare build TWWP/.claude/commands/twwp-firmware.md` |
| Rewrite | `Waveshare build TWWP/.roo/modes/iot-engineer/iot-engineer.mode.md` |
| Rewrite | `Waveshare build TWWP/.roo/skills/iot-engineer/SKILL.md` |
| Rewrite | `Waveshare build TWWP/.roo/skills/iot-engineer/firmware-development.md` |
| Rewrite | `Waveshare build TWWP/.roo/skills/iot-engineer/hardware-integration.md` |
| Rewrite | `Waveshare build TWWP/.roo/skills/iot-engineer/sensor-actuator-management.md` |
| Rewrite | `Waveshare build TWWP/.roo/skills/iot-engineer/network-communication.md` |
| Rewrite | `Waveshare build TWWP/.roo/skills/iot-engineer/security-compliance.md` |
| Rewrite | `Waveshare build TWWP/.roo/skills/iot-engineer/data-pipeline.md` |
| Rewrite | `Waveshare build TWWP/.roo/skills/iot-engineer/power-management.md` |
| Create | `TWWP Sensor Node v1/AGENTS.md` |
| Rename + trim | `TWWP Sensor Node v1/docs/HANDOFF_TO_ROO.md` → `docs/TASK_QUEUE.md` |
| Append | `TWWP Sensor Node v1/docs/FIRMWARE_ARCHITECTURE.md` (ESPHome notes) |

Paths prefixed with `Waveshare build TWWP` = `/home/kenny/Documents/Waveshare build TWWP/`
Paths prefixed with `TWWP Sensor Node v1` = `/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/`

---

## Task 1: Create SESSION.md

**Files:**
- Create: `/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/docs/SESSION.md`

- [ ] **Step 1: Write the file**

```markdown
# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done
M0 complete — leak detection, MQTT/TLS, WiFiManager, SD buffering, RTC, watchdog all working.

## In progress
None — ready to start M0.3 polish tasks.

## Next step
Implement buffer overflow cap in store_sd.cpp: if s_seq - oldestSeq > SD_MAX_BUFFER_LINES, delete oldest file before writing and append warning to /log/crashes.txt.

## Tool last used
claude-code

## Updated
2026-04-23 00:00
```

- [ ] **Step 2: Verify the file exists and looks correct**

Run:
```bash
cat "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/docs/SESSION.md"
```
Expected: file prints with all five sections visible.

- [ ] **Step 3: Commit**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add docs/SESSION.md
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "chore: add SESSION.md for cross-tool session handoff"
```

---

## Task 2: Create infrastructure registry

**Files:**
- Create: `/home/kenny/.twwp/INFRASTRUCTURE.md`

- [ ] **Step 1: Create the ~/.twwp directory and write the file**

```bash
mkdir -p /home/kenny/.twwp
```

Then write `/home/kenny/.twwp/INFRASTRUCTURE.md`:

```markdown
# TWWP Infrastructure Registry
_Never share. Never commit. Location: ~/.twwp/INFRASTRUCTURE.md_
_Append entries here whenever a tool asks "save this to the registry?"_

---

## Hosting — Hetzner
- Account email: kennymtbeach@gmail.com
- Console URL: [PLACEHOLDER — hetzner console URL]
- Server plan: [PLACEHOLDER]
- Hetzner IP: 91.98.133.15
- SSH user: root
- SSH key: ~/.ssh/hetzner_ed25519
- SSH key passphrase: [PLACEHOLDER or "none"]

## DNS — DuckDNS
- Account: [PLACEHOLDER — duckdns login]
- Domain: twwp-iot.duckdns.org
- Token: [PLACEHOLDER]
- Auto-renew: [PLACEHOLDER — cron or script location]

## DNS — Cloudflare
- Account email: [PLACEHOLDER]
- Domain(s) managed: [PLACEHOLDER]
- API token: [PLACEHOLDER]
- Zone ID: [PLACEHOLDER]

## Certificates — Let's Encrypt
- Domain: twwp-iot.duckdns.org
- Path on server: /etc/letsencrypt/live/twwp-iot.duckdns.org/
- Renewal method: [PLACEHOLDER — certbot/acme.sh/other]
- Expiry check: [PLACEHOLDER]

## MQTT Broker — Mosquitto
- Host: twwp-iot.duckdns.org
- Port: 8883 (TLS only — never 1883)
- Config path on server: /mosquitto/config/mosquitto.conf
- Password file: [PLACEHOLDER — path]
- allow_anonymous: false

## Home Assistant
- Tailscale URL: [PLACEHOLDER]
- Local URL: [PLACEHOLDER]
- Admin user: [PLACEHOLDER]
- Docker volume path: /home/kenny/projects/homeassistant/

## VPN — Tailscale
- Account: [PLACEHOLDER]
- Server Tailscale IP: 100.67.244.37
- SSH via Tailscale: kenny@100.67.244.37

## Local Network
- Primary WiFi SSID: [PLACEHOLDER]
- WiFi password: [PLACEHOLDER]
- Router admin URL: [PLACEHOLDER]
- Router admin credentials: [PLACEHOLDER]
- Local subnet: [PLACEHOLDER e.g. 192.168.1.0/24]

## WiFi Provisioning
- Portal SSID: TWWP-Setup-XXXX (auto-generated suffix)
- Portal password: wateriswet

## OTA — Firmware Binary Hosting
- Hosting method: [PLACEHOLDER — e.g. Hetzner nginx, GitHub Releases, S3]
- Base URL: [PLACEHOLDER — URL the ESP32 fetches .bin from]
- Auth required: [PLACEHOLDER]
- Deploy script: [PLACEHOLDER — path or command]

## Notifications
- Service: [PLACEHOLDER — Telegram / Pushover / email]
- Telegram bot token: [PLACEHOLDER]
- Telegram chat ID: [PLACEHOLDER]
- Pushover app key: [PLACEHOLDER]
- Email relay: [PLACEHOLDER]

## Git Remote — Firmware
- Provider: [PLACEHOLDER — GitHub / GitLab / Gitea]
- Repo URL: [PLACEHOLDER]
- Auth: [PLACEHOLDER — SSH key or token]
- Last pushed: [PLACEHOLDER]

## Docker — Server Containers
- Mosquitto image: [PLACEHOLDER — e.g. eclipse-mosquitto:2.0.18]
- Home Assistant image: [PLACEHOLDER]
- Docker Hub account: [PLACEHOLDER]
- Compose file location on server: [PLACEHOLDER]

## Node Credentials
### wh_001
- MQTT user: twwp_wh_001
- MQTT pass: [PLACEHOLDER — in secrets.h only]
- NODE_ID: wh_001
- secrets.h: include/secrets.h (gitignored)

### wh_002 [FUTURE]
- MQTT user: twwp_wh_002
- MQTT pass: [PLACEHOLDER]
- NODE_ID: wh_002

## Pipeline / Future Services
- [PLACEHOLDER — e.g. Grafana, InfluxDB, Node-RED, backup provider]
```

- [ ] **Step 2: Verify**

```bash
cat /home/kenny/.twwp/INFRASTRUCTURE.md | head -20
```
Expected: header and Hetzner section visible.

---

## Task 3: Create CLAUDE.md and slash commands

**Files:**
- Create: `/home/kenny/Documents/Waveshare build TWWP/CLAUDE.md`
- Create: `/home/kenny/Documents/Waveshare build TWWP/.claude/commands/twwp-start.md`
- Create: `/home/kenny/Documents/Waveshare build TWWP/.claude/commands/twwp-handoff.md`
- Create: `/home/kenny/Documents/Waveshare build TWWP/.claude/commands/twwp-firmware.md`

- [ ] **Step 1: Create commands directory**

```bash
mkdir -p "/home/kenny/Documents/Waveshare build TWWP/.claude/commands"
```

- [ ] **Step 2: Write CLAUDE.md**

Write `/home/kenny/Documents/Waveshare build TWWP/CLAUDE.md`:

```markdown
# TWWP Project — Claude Code Instructions

## Infrastructure registry detection

Whenever any of the following appear in conversation, stop and ask the user:
"Should I save this to the infrastructure registry at ~/.twwp/INFRASTRUCTURE.md?"

Detect and prompt for:
- IP addresses (any x.x.x.x pattern)
- URLs (http://, https://, mqtt://, mqtts://)
- Passwords or credentials mentioned in conversation
- Certificate content (-----BEGIN CERTIFICATE-----)
- File paths that look like server config (/etc/, /mosquitto/, /home/kenny/projects/)
- API tokens, keys, or secrets
- SSH key paths or fingerprints
- Domain names used as service endpoints (e.g. twwp-iot.duckdns.org)

If the user says yes, append the value to the correct section of ~/.twwp/INFRASTRUCTURE.md.

## Slash commands

- `/twwp-start` — run at the start of every session. Reads SESSION.md and gives a briefing.
- `/twwp-handoff` — run before switching to Roo or Codex. Writes SESSION.md and commits.
- `/twwp-firmware` — load before any firmware coding. Enforces all design rules.

## Project paths

- Firmware: /home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/
- Session state: docs/SESSION.md (in firmware project)
- Task queue: docs/TASK_QUEUE.md (in firmware project)
- Infrastructure registry: ~/.twwp/INFRASTRUCTURE.md (never in git)
```

- [ ] **Step 3: Write twwp-start.md**

Write `/home/kenny/Documents/Waveshare build TWWP/.claude/commands/twwp-start.md`:

```markdown
Read these files:
1. /home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/docs/SESSION.md
2. /home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/docs/FIRMWARE_ARCHITECTURE.md

Run: git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" log --oneline -5

Then output exactly this — nothing more:

**Where we are:** [current milestone, last completed thing, what is in progress — 2 sentences max]

**Next action:** [the exact next step from SESSION.md "Next step" field — 1 sentence, specific enough to act on immediately]

Do not ask questions. Do not summarise the architecture. Briefing only, then stop.
```

- [ ] **Step 4: Write twwp-handoff.md**

Write `/home/kenny/Documents/Waveshare build TWWP/.claude/commands/twwp-handoff.md`:

```markdown
Ask the user one question: "What did you finish this session? (one sentence)"

Wait for their answer. Then do all of the following:

1. Read /home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/docs/SESSION.md
2. Run: git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" status --short
3. Read /home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/docs/TASK_QUEUE.md — find the first unchecked [ ] task after the last [x] task.

Rewrite SESSION.md with these values:
- Last done: [the user's answer]
- In progress: [list of uncommitted files from git status, or "none"]
- Next step: [the first unchecked task found in TASK_QUEUE.md]
- Tool last used: claude-code
- Updated: [current date and time as YYYY-MM-DD HH:MM]

Then run:
```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add docs/SESSION.md
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "chore: session handoff [claude-code]"
```

Tell the user: "Handoff committed. SESSION.md is up to date — you can now open Roo or Codex."
```

- [ ] **Step 5: Write twwp-firmware.md**

Write `/home/kenny/Documents/Waveshare build TWWP/.claude/commands/twwp-firmware.md`:

```markdown
Load these as active constraints for this session. Apply silently to all code. Flag any violation immediately before writing code.

## Locked decisions — never change without explicit user instruction

- Stack: PlatformIO + Arduino framework
- MQTT: TLS only, port 8883, WiFiClientSecure + CA cert. Never plain 1883.
- MQTT broker: twwp-iot.duckdns.org — DNS name only, never a hardcoded IP.
- Per-device credentials: unique client_id, username, password per node. Stored in include/secrets.h.
- WiFi provisioning: WiFiManager captive portal only.
- Time: DS3231 on GPIO9(SDA)/GPIO3(SCL). NTP syncs RTC when online. RTC holds time offline.
- Storage: microSD on SPI (GPIO11/12/13/14). Daily CSV rotation. FIFO ring-buffer for offline MQTT.
- RS485: UART_MODE_RS485_HALF_DUPLEX on UART1 GPIO17(TX)/GPIO18(RX). DE/RE auto via GPIO21.
- FreeRTOS: all tasks pinned to core 0 with xTaskCreatePinnedToCore(..., 0).
- PSRAM: heap_caps_malloc(..., MALLOC_CAP_SPIRAM) for all large buffers.
- ArduinoJson: v7 API only. Use JsonDocument, not DynamicJsonDocument.

## Design rules — flag before writing any code

- NEVER use delay() anywhere (main loop or drivers).
- NEVER block > 10s without calling watchdog_feed().
- NEVER call client.setInsecure() — always use client.setCACert(MQTT_CA_CERT).
- NEVER fall back to plain MQTT on TLS failure. Retry with backoff only.
- Pin numbers ONLY in include/pins.h. Never hardcode GPIO numbers elsewhere.
- NEVER suggest git add include/secrets.h — it is gitignored.
- New sensors follow sensor_leak pattern: _begin(), _loop(), _read*() interface. All MQTT via netMqtt_publishSub(). All events via storeSd_logEvent().
- Each new MQTT topic → add a row to docs/MQTT_TOPIC_MAP.md in the same commit.
- Each new GPIO → add a row to docs/PIN_ALLOCATION.md AND update include/pins.h in the same commit.
- ArduinoJson: only use v7 API. Never use StaticJsonDocument or DynamicJsonDocument.

## SensorData struct — only add fields, never remove or rename

```cpp
struct SensorData {
    uint32_t  timestamp;
    float     flow1;
    float     flow_total;
    float     pressure;
    float     temperature;
    float     supply_voltage;
    bool      leak;
    bool      flow_ok;
    bool      pressure_ok;
    bool      power_ok;
    float     ph;
    float     orp;
    float     ec;
    float     tds;
    float     water_temp;
};
```

## Current driver inventory (src/)

net_wifi, net_mqtt, time_rtc, store_sd, watchdog, status_led,
sensor_leak, sensor_flow_stub, sensor_pressure_stub, sensor_temp_stub, actuator_solenoid_stub
```

- [ ] **Step 6: Verify all four files exist**

```bash
ls "/home/kenny/Documents/Waveshare build TWWP/.claude/commands/"
ls "/home/kenny/Documents/Waveshare build TWWP/CLAUDE.md"
```
Expected: twwp-start.md, twwp-handoff.md, twwp-firmware.md listed. CLAUDE.md exists.

---

## Task 4: Rewrite the Roo iot-engineer mode

**Files:**
- Rewrite: `/home/kenny/Documents/Waveshare build TWWP/.roo/modes/iot-engineer/iot-engineer.mode.md`

- [ ] **Step 1: Overwrite the mode file with TWWP-specific content**

Write `/home/kenny/Documents/Waveshare build TWWP/.roo/modes/iot-engineer/iot-engineer.mode.md`:

```markdown
---
name: iot-engineer
description: "Use for all TWWP water monitoring work — ESP32-S3 firmware, PlatformIO, MQTT/TLS, Home Assistant, Hetzner server."
emoji: "📡"
when_to_use: "When working on TWWP firmware (PlatformIO/C++), server infrastructure (Mosquitto, HA, Hetzner), or any cross-tool task."
model: sonnet
---

# TWWP IoT Engineer

You are a patient firmware assistant working on the TWWP home water monitoring project. The user is an amateur developer. Explain clearly, avoid jargon, flag problems before writing code.

## Session start — do this before anything else

Read:
1. `/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/docs/SESSION.md`
2. `/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/docs/FIRMWARE_ARCHITECTURE.md`

Run: `git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" log --oneline -5`

Output exactly:

**Where we are:** [current milestone, last done, in progress — 2 sentences]
**Next action:** [exact next step from SESSION.md — 1 sentence]

Do not ask questions until after the briefing.

## Before ending any session

Ask: "What did you finish this session?"

Update `/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/docs/SESSION.md`:
- Last done: [their answer]
- In progress: [uncommitted changes from git status, or "none"]
- Next step: [next unchecked [ ] task in TASK_QUEUE.md]
- Tool last used: roo
- Updated: [YYYY-MM-DD HH:MM]

Commit:
```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add docs/SESSION.md
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "chore: session handoff [roo]"
```

## Infrastructure detection

When any of these appear in conversation, ask: "Should I save this to ~/.twwp/INFRASTRUCTURE.md?"
- IP addresses (x.x.x.x)
- URLs (http://, https://, mqtt://, mqtts://)
- Passwords or credentials
- Certificate content (-----BEGIN CERTIFICATE-----)
- Server paths (/etc/, /mosquitto/, /home/kenny/projects/)
- API tokens, SSH key details, domain names used as endpoints

If yes, append to the correct section of `~/.twwp/INFRASTRUCTURE.md`.

## Locked decisions — never change without explicit user instruction

- Stack: PlatformIO + Arduino framework
- MQTT: TLS only, port 8883, WiFiClientSecure + CA cert. Never plain 1883.
- MQTT broker: twwp-iot.duckdns.org — DNS name only, never hardcoded IP.
- Per-device credentials: unique client_id, username, password per node in include/secrets.h.
- WiFi: WiFiManager captive portal.
- Time: DS3231 on GPIO9(SDA)/GPIO3(SCL). NTP syncs RTC when online.
- Storage: microSD SPI (GPIO11/12/13/14). Daily CSV + FIFO offline MQTT queue.
- RS485: UART_MODE_RS485_HALF_DUPLEX, UART1 GPIO17/18, GPIO21 auto DE/RE.
- FreeRTOS: all tasks pinned to core 0.
- PSRAM: heap_caps_malloc(..., MALLOC_CAP_SPIRAM) for large buffers.
- ArduinoJson: v7 API (JsonDocument, not DynamicJsonDocument).

## Design rules — flag before writing any code

- No delay() anywhere. No blocking > 10s without watchdog_feed().
- Never setInsecure(). Always setCACert(MQTT_CA_CERT).
- Never fall back to plain MQTT. Retry with backoff only.
- Pin numbers only in include/pins.h. Never hardcode GPIO numbers.
- Never commit include/secrets.h.
- New sensors follow sensor_leak pattern: _begin(), _loop(), _read*().
- New MQTT topic → docs/MQTT_TOPIC_MAP.md in same commit.
- New GPIO → docs/PIN_ALLOCATION.md + include/pins.h in same commit.

## Key paths

- Firmware project: `/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/`
- Session state: `docs/SESSION.md`
- Task queue: `docs/TASK_QUEUE.md`
- Architecture reference: `docs/FIRMWARE_ARCHITECTURE.md`
- Infrastructure registry: `~/.twwp/INFRASTRUCTURE.md`
```

- [ ] **Step 2: Verify**

```bash
head -20 "/home/kenny/Documents/Waveshare build TWWP/.roo/modes/iot-engineer/iot-engineer.mode.md"
```
Expected: TWWP-specific description in frontmatter, not the old generic enterprise text.

---

## Task 5: Rewrite SKILL.md index and firmware-development.md

**Files:**
- Rewrite: `/home/kenny/Documents/Waveshare build TWWP/.roo/skills/iot-engineer/SKILL.md`
- Rewrite: `/home/kenny/Documents/Waveshare build TWWP/.roo/skills/iot-engineer/firmware-development.md`

- [ ] **Step 1: Rewrite SKILL.md**

Write `/home/kenny/Documents/Waveshare build TWWP/.roo/skills/iot-engineer/SKILL.md`:

```markdown
---
name: iot-engineer-skills
description: TWWP-specific reference skills for firmware, hardware, networking, security, data, and reliability.
---

# TWWP IoT Engineer Skills

Reference files for the iot-engineer mode. Use mid-session when you need specific details.

## Skills

- [firmware-development.md](firmware-development.md) — PlatformIO build, flash workflow, driver pattern
- [hardware-integration.md](hardware-integration.md) — GPIO pin map, board facts, RS485, I2C, SPI
- [sensor-actuator-management.md](sensor-actuator-management.md) — SensorData struct, sensor_leak interface, new sensor checklist
- [network-communication.md](network-communication.md) — WiFiManager, MQTT/TLS, topic map
- [security-compliance.md](security-compliance.md) — TLS rules, secrets.h, never setInsecure()
- [data-pipeline.md](data-pipeline.md) — SD card layout, offline buffer, node.json
- [power-management.md](power-management.md) — Watchdog, FreeRTOS tasks, timing constants
```

- [ ] **Step 2: Rewrite firmware-development.md**

Write `/home/kenny/Documents/Waveshare build TWWP/.roo/skills/iot-engineer/firmware-development.md`:

```markdown
---
name: firmware-development
description: PlatformIO build, flash workflow, and driver patterns for TWWP ESP32-S3 firmware.
---

# Firmware Development — TWWP

## Build command

```bash
cd "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1"
~/.platformio/penv/bin/pio run
```
Expected output ends with: `[SUCCESS]`

## Flash + monitor workflow (CRITICAL — easy to get wrong)

```bash
# Step 1: flash
~/.platformio/penv/bin/pio run -t upload

# Step 2: wait for this line in output:
# "Hard resetting via RTS pin" — do NOT open monitor yet

# Step 3: open monitor
~/.platformio/penv/bin/pio device monitor

# Step 4: press the physical RESET button on the board once
# The app boots and serial output appears
```

To enter download mode manually: hold BOOT → tap RESET → release BOOT → upload immediately.

**Why this is needed:** The ESP32-S3 USB-JTAG maps host DTR → GPIO0 (boot-mode pin). Opening any serial port asserts DTR → chip resets into download mode. `monitor_dtr=0` in platformio.ini fixes this, but you still need the manual RESET after flashing.

## Main loop structure (target M7+)

```cpp
void loop() {
    watchdog_feed();                         // always first

    SensorData data = readAllSensors();

    healthService.evaluate(data);
    alertService.evaluate(data);
    ruleEngine.evaluate(data);

    if (alertService.hasNewAlert())
        mqtt.publishAlert(alertService.getState());

    telemetryService.sendStatus(data, alertService.getState());

    storeSd_logEvent(data);
    mqtt.drainBuffer();
}
```

## Driver interface pattern (follow sensor_leak exactly)

Every new sensor driver must follow this interface:

```cpp
// header: sensor_<name>.h
void sensor<Name>_begin();          // call once in setup()
void sensor<Name>_loop();           // call every loop() iteration — non-blocking
float sensor<Name>_read();          // returns current value
```

All MQTT publishing via `netMqtt_publishSub()`.
All event logging via `storeSd_logEvent()`.

## Key config files

| File | Purpose |
|---|---|
| `include/secrets.h` | NODE_ID, MQTT_HOST, MQTT_PORT, MQTT_USER, MQTT_PASS, CA cert — gitignored |
| `include/config.h` | MQTT topics, timing constants, SD paths |
| `include/pins.h` | GPIO numbers only — single source of truth |
| `platformio.ini` | Build flags, library versions, board config |
```

- [ ] **Step 3: Verify both files**

```bash
head -5 "/home/kenny/Documents/Waveshare build TWWP/.roo/skills/iot-engineer/SKILL.md"
head -5 "/home/kenny/Documents/Waveshare build TWWP/.roo/skills/iot-engineer/firmware-development.md"
```
Expected: TWWP-specific descriptions in both frontmatters.

---

## Task 6: Rewrite hardware-integration.md and sensor-actuator-management.md

**Files:**
- Rewrite: `/home/kenny/Documents/Waveshare build TWWP/.roo/skills/iot-engineer/hardware-integration.md`
- Rewrite: `/home/kenny/Documents/Waveshare build TWWP/.roo/skills/iot-engineer/sensor-actuator-management.md`

- [ ] **Step 1: Rewrite hardware-integration.md**

Write `/home/kenny/Documents/Waveshare build TWWP/.roo/skills/iot-engineer/hardware-integration.md`:

```markdown
---
name: hardware-integration
description: Waveshare ESP32-S3-RS485-CAN board GPIO map, peripheral facts, and wiring patterns for TWWP.
---

# Hardware Integration — TWWP

## Board: Waveshare ESP32-S3-RS485-CAN

- Module: ESP32-S3-WROOM-1, 16MB flash, 8MB OPI PSRAM
- USB serial: USB CDC — requires `-DARDUINO_USB_CDC_ON_BOOT=1` or Serial is silent
- GPIO47: reserved onboard — do not use

## GPIO assignments (from include/pins.h)

| GPIO | Function | Notes |
|---|---|---|
| 3 | I2C SCL (external) | DS3231 RTC |
| 4 | Flow sensor 1 | M1 — interrupt input |
| 5 | Flow sensor 2 | M1 — future |
| 6 | Leak sensor digital | MH-RD |
| 7 | Pressure ADC | M2 — analog input |
| 8 | Solenoid gate | M3 — MOSFET gate |
| 9 | I2C SDA (external) | DS3231 RTC |
| 10 | DS18B20 1-Wire | M2 — temperature |
| 11 | SPI MOSI | microSD |
| 12 | SPI MISO | microSD |
| 13 | SPI CLK | microSD |
| 14 | SPI CS | microSD |
| 15 | CAN TX | TWAI peripheral |
| 16 | CAN RX | TWAI peripheral |
| 17 | RS485 TX | UART1 |
| 18 | RS485 RX | UART1 |
| 21 | RS485 DE/RE | Auto-toggled by hardware |
| 38 | I2C SDA (internal) | PCF85063 onboard RTC — NOT used by TWWP |
| 39 | I2C SCL (internal) | PCF85063 onboard RTC — NOT used by TWWP |
| 48 | WS2812 LED | status_led.cpp via FastLED |

**Important:** TWWP uses the DS3231 external RTC on GPIO9/3, NOT the onboard PCF85063.

## RS485 init pattern (from Waveshare demo)

```cpp
HardwareSerial rs485Serial(1);

void rs485_init(unsigned long baud) {
    rs485Serial.begin(baud, SERIAL_8N1, 18, 17);  // RX, TX
    rs485Serial.setPins(-1, -1, -1, 21);            // GPIO21 auto DE/RE
    rs485Serial.setMode(UART_MODE_RS485_HALF_DUPLEX);
}
```

## PSRAM allocation (for any buffer > ~4KB)

```cpp
uint8_t* buf = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
if (!buf) { /* handle failure */ }
```

## I2C (DS3231 external RTC)

- SDA: GPIO9
- SCL: GPIO3
- Address: 0x68
- Library: RTClib (Adafruit)
```

- [ ] **Step 2: Rewrite sensor-actuator-management.md**

Write `/home/kenny/Documents/Waveshare build TWWP/.roo/skills/iot-engineer/sensor-actuator-management.md`:

```markdown
---
name: sensor-actuator-management
description: SensorData struct, sensor driver interface, and new sensor checklist for TWWP.
---

# Sensor & Actuator Management — TWWP

## SensorData struct — the core data contract

Only add fields. Never remove or rename existing fields.

```cpp
struct SensorData {
    uint32_t  timestamp;       // Unix epoch from DS3231
    float     flow1;           // L/min, calibrated
    float     flow_total;      // L, daily total (resets at RTC midnight)
    float     pressure;        // kPa, calibrated
    float     temperature;     // °C (DS18B20)
    float     supply_voltage;  // V
    bool      leak;            // MH-RD state
    bool      flow_ok;         // set by HealthService (M6)
    bool      pressure_ok;
    bool      power_ok;
    float     ph;              // YiErYi 3788 (M5)
    float     orp;
    float     ec;
    float     tds;
    float     water_temp;
};
```

## Sensor driver interface (follow sensor_leak exactly)

```cpp
// sensor_<name>.h
void sensor<Name>_begin();    // init — call once in setup()
void sensor<Name>_loop();     // non-blocking poll — call every loop()
float sensor<Name>_read();    // return current calibrated value
```

All MQTT publishing via `netMqtt_publishSub()`.
All event logging via `storeSd_logEvent()`.

## Current sensor/actuator inventory

| File | Sensor/Actuator | Status |
|---|---|---|
| sensor_leak.cpp | MH-RD leak probe (GPIO6) | M0 — active |
| sensor_flow_stub.cpp | Hall flow sensor (GPIO4) | M1 — stub |
| sensor_pressure_stub.cpp | Pressure transducer (GPIO7) | M2 — stub |
| sensor_temp_stub.cpp | DS18B20 1-Wire (GPIO10) | M2 — stub |
| actuator_solenoid_stub.cpp | Solenoid MOSFET (GPIO8) | M3 — stub |

## New sensor checklist

When replacing a stub with a real driver:
- [ ] Copy sensor_leak.{h,cpp} as template
- [ ] Implement _begin(), _loop(), _read*() interface
- [ ] Assign GPIO in include/pins.h — no hardcoded numbers
- [ ] Add GPIO row to docs/PIN_ALLOCATION.md
- [ ] Add HA discovery payload in net_mqtt.cpp
- [ ] Add MQTT topic row to docs/MQTT_TOPIC_MAP.md
- [ ] Add HA entity row to docs/HA_DISCOVERY.md
- [ ] Test with sensor_leak pattern in main loop
```

---

## Task 7: Rewrite network-communication.md and security-compliance.md

**Files:**
- Rewrite: `/home/kenny/Documents/Waveshare build TWWP/.roo/skills/iot-engineer/network-communication.md`
- Rewrite: `/home/kenny/Documents/Waveshare build TWWP/.roo/skills/iot-engineer/security-compliance.md`

- [ ] **Step 1: Rewrite network-communication.md**

Write `/home/kenny/Documents/Waveshare build TWWP/.roo/skills/iot-engineer/network-communication.md`:

```markdown
---
name: network-communication
description: WiFiManager, MQTT/TLS connection, and topic map for TWWP nodes.
---

# Network Communication — TWWP

## WiFi — WiFiManager captive portal

- First boot: opens `TWWP-Setup-XXXX` AP. User connects and enters WiFi credentials via browser.
- Timeout: 180s — if not configured, device continues offline.
- Credential reset: hold GPIO0 LOW for > 5s in loop() → calls netWifi_resetCredentials().
- Reconnect: checked every 5s. Exponential backoff on failure.

## MQTT — connection pattern

```cpp
// Always TLS. Never plain. Never setInsecure().
WiFiClientSecure wifiClient;
wifiClient.setCACert(MQTT_CA_CERT);   // from secrets.h
PubSubClient mqtt(wifiClient);
mqtt.setServer(MQTT_HOST, 8883);
```

Reconnect backoff: 1s → 2s → 4s → ... → 60s cap. Never infinite retry without backoff.

## MQTT topic map

| Topic | Direction | Payload |
|---|---|---|
| `twwp/<id>/status` | node → broker | JSON telemetry snapshot (every 10s) |
| `twwp/<id>/alert` | node → broker | JSON alert state changes |
| `twwp/<id>/log` | node → broker | SD errors, warnings (rate-limited 1/min) |
| `twwp/<id>/lwt` | node → broker | Last will: `"offline"` |
| `twwp/<id>/cmd` | broker → node | JSON: solenoid, calibration, decommission, OTA |
| `twwp/register` | node → broker | JSON: first-connect registration |
| `homeassistant/...` | node → broker | MQTT discovery configs |

All topics: port 8883 TLS only.

## Telemetry payload example (twwp/<id>/status)

```json
{
  "ts": 1714000000,
  "leak": false,
  "flow_lpm": 12.4,
  "flow_total_l": 340.1,
  "pressure_kpa": 280.5,
  "temp_c": 18.2,
  "voltage": 4.98,
  "wifi_rssi": -62,
  "uptime_s": 86400,
  "sd_free_kb": 14200,
  "mqtt_buffer": 0
}
```

## Offline buffering

When MQTT is unreachable, messages are written to `/buf/<seq>.json` on SD.
On reconnect, `mqtt.drainBuffer()` sends them FIFO order.
Buffer cap: 500 files. Oldest deleted when cap reached.
```

- [ ] **Step 2: Rewrite security-compliance.md**

Write `/home/kenny/Documents/Waveshare build TWWP/.roo/skills/iot-engineer/security-compliance.md`:

```markdown
---
name: security-compliance
description: TLS rules, secrets management, and security requirements for TWWP nodes.
---

# Security & Compliance — TWWP

## Non-negotiable rules

These are locked. Do not suggest alternatives.

| Rule | What to do | What never to do |
|---|---|---|
| TLS transport | `client.setCACert(MQTT_CA_CERT)` | `client.setInsecure()` |
| MQTT port | 8883 only | 1883 (plain, unencrypted) |
| TLS failure | retry with backoff, log error | fall back to plain MQTT |
| Broker address | DNS name from secrets.h | hardcoded IP |
| credentials | unique per device in secrets.h | shared credentials across nodes |
| secrets.h | gitignored, fill manually | commit to git, ever |

## secrets.h structure

```cpp
// include/secrets.h — gitignored, fill in manually for each node
#define NODE_ID       "wh_001"
#define MQTT_HOST     "twwp-iot.duckdns.org"
#define MQTT_PORT     8883
#define MQTT_USER     "twwp_wh_001"
#define MQTT_PASS     "..."
#define MQTT_CA_CERT  \
"-----BEGIN CERTIFICATE-----\n" \
"...\n" \
"-----END CERTIFICATE-----\n"
```

## TLS failure handling

```cpp
if (!mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
    int sslErr = wifiClient.lastError(nullptr, 0);
    Serial.printf("TLS connect failed, err=%d\n", sslErr);
    storeSd_logCrash("TLS connect failed");
    // exponential backoff — never fall through to plain MQTT
}
```

## Server-side checklist (for reference)

- Mosquitto: `allow_anonymous false`, `listener 8883`, cert/key/cafile configured
- Firewall: `ufw deny 1883/tcp`, `ufw allow 8883/tcp`
- Per-device password: `mosquitto_passwd -b /path/passwordfile twwp_wh_001 <pass>`
```

---

## Task 8: Rewrite data-pipeline.md and power-management.md

**Files:**
- Rewrite: `/home/kenny/Documents/Waveshare build TWWP/.roo/skills/iot-engineer/data-pipeline.md`
- Rewrite: `/home/kenny/Documents/Waveshare build TWWP/.roo/skills/iot-engineer/power-management.md`

- [ ] **Step 1: Rewrite data-pipeline.md**

Write `/home/kenny/Documents/Waveshare build TWWP/.roo/skills/iot-engineer/data-pipeline.md`:

```markdown
---
name: data-pipeline
description: SD card layout, offline MQTT buffer, daily CSV log, and node.json config for TWWP.
---

# Data Pipeline — TWWP

## SD card layout

```
/
├── log/
│   ├── YYYY-MM-DD.csv     ← daily sensor log, rotated at RTC midnight
│   └── crashes.txt        ← watchdog resets + exception log
├── buf/
│   └── <seq>.json         ← unsent MQTT messages, drained FIFO on reconnect
└── config/
    └── node.json          ← calibration, thresholds, sensor intervals
```

SD card: FAT32, 8–32 GB SDHC, Class 10+. Insert before power-up.
Library: SdFat 2.x (greiman/SdFat).

## Daily CSV format (log/YYYY-MM-DD.csv)

```
timestamp,leak,flow_lpm,flow_total_l,pressure_kpa,temp_c,voltage
1714000000,0,12.4,340.1,280.5,18.2,4.98
```

Rotated at RTC midnight. Never deleted by firmware — manual cleanup only.

## Offline buffer (buf/<seq>.json)

Each file is one unsent MQTT publish:
```json
{ "topic": "twwp/wh_001/status", "payload": "{...}", "ts": 1714000000 }
```

Files named by monotonic sequence number. Drained FIFO on MQTT reconnect.
Cap: 500 files. When cap reached, oldest deleted + warning written to crashes.txt.

## node.json (config/node.json)

```json
{
  "node_id": "wh_001",
  "sensor_interval_ms": 5000,
  "flow": { "k_factor": 450 },
  "pressure": { "offset": 0.0, "scale": 1.0 },
  "calibration": {
    "flow1": { "pulses_per_liter": 450 },
    "pressure": { "offset": 0.2, "scale": 1.1 }
  },
  "health_thresholds": {
    "min_voltage": 4.5,
    "max_flow": 50.0,
    "min_pressure": 0.0,
    "max_pressure": 700.0
  }
}
```

Loaded at boot. Updated remotely via `twwp/<id>/cmd` MQTT command.
```

- [ ] **Step 2: Rewrite power-management.md (repurposed as reliability & timing)**

Write `/home/kenny/Documents/Waveshare build TWWP/.roo/skills/iot-engineer/power-management.md`:

```markdown
---
name: power-management
description: Watchdog, FreeRTOS task patterns, and timing constants for TWWP reliability.
---

# Reliability & Timing — TWWP

Note: TWWP nodes are mains-powered. This file covers reliability patterns, not battery optimisation.

## Watchdog (watchdog.cpp)

Hardware WDT timeout: 30s. Must call watchdog_feed() at least every 10s.

```cpp
// Every loop() iteration — always the first line
void loop() {
    watchdog_feed();
    // ... rest of loop
}

// Also call inside any blocking operation > 10s
void longOperation() {
    for (int i = 0; i < 1000; i++) {
        watchdog_feed();
        // ... work
    }
}
```

On WDT reset: crash reason written to `/log/crashes.txt` before reboot.

## FreeRTOS task pattern

All tasks pinned to core 0.

```cpp
xTaskCreatePinnedToCore(
    myTaskFunction,   // function
    "taskName",       // name for debugging
    4096,             // stack size in bytes
    nullptr,          // parameter
    1,                // priority
    &taskHandle,      // handle
    0                 // core — always 0
);

void myTaskFunction(void* param) {
    while (1) {
        // do work
        watchdog_feed();
        vTaskDelay(pdMS_TO_TICKS(50));   // yield — never busy-wait
    }
}
```

## Timing constants (from config.h)

| Constant | Value | Purpose |
|---|---|---|
| Heartbeat interval | 10s | MQTT telemetry publish |
| MQTT backoff start | 1s | First retry delay |
| MQTT backoff max | 60s | Max retry delay |
| WiFi reconnect check | 5s | Poll interval |
| WiFi portal timeout | 180s | Captive portal auto-close |
| Watchdog timeout | 30s | Hardware WDT reset |

## No-blocking rules

- No `delay()` anywhere — use `vTaskDelay()` in tasks, or `millis()` timers in loop().
- No `while(condition)` without a `vTaskDelay()` inside.
- No `Serial.readString()` with default timeout — set timeout explicitly.
```

---

## Task 9: Write AGENTS.md

**Files:**
- Create: `/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/AGENTS.md`

- [ ] **Step 1: Write the file**

Write `/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/AGENTS.md`:

```markdown
# TWWP Sensor Node — Codex Agent Instructions

## Read first — always, before touching any file

1. `docs/SESSION.md` — where the project is right now
2. `docs/FIRMWARE_ARCHITECTURE.md` — architecture reference, locked decisions, design rules
3. `docs/TASK_QUEUE.md` — ordered task queue, pick up at the first unchecked [ ] task

## Before stopping any session

Update `docs/SESSION.md`:
- Last done: what you completed
- In progress: uncommitted changes, or "none"
- Next step: next unchecked task in TASK_QUEUE.md
- Tool last used: codex
- Updated: YYYY-MM-DD HH:MM

Commit: `git add docs/SESSION.md && git commit -m "chore: session handoff [codex]"`

## Locked decisions — never change

- Stack: PlatformIO + Arduino framework
- MQTT: TLS only, port 8883, WiFiClientSecure. Never plain 1883.
- MQTT broker: DNS name only (twwp-iot.duckdns.org). Never hardcoded IP.
- Credentials: unique per device. Stored in include/secrets.h (gitignored). Never commit it.
- FreeRTOS: all tasks pinned to core 0.
- PSRAM: heap_caps_malloc(..., MALLOC_CAP_SPIRAM) for large buffers.
- ArduinoJson: v7 API (JsonDocument not DynamicJsonDocument).

## Design rules — flag violations before writing code

- No delay() anywhere. No blocking > 10s without watchdog_feed().
- Never client.setInsecure(). Always client.setCACert(MQTT_CA_CERT).
- Pin numbers only in include/pins.h. Never hardcoded.
- New sensors follow sensor_leak pattern: _begin(), _loop(), _read*().
- New MQTT topic → docs/MQTT_TOPIC_MAP.md in same commit.
- New GPIO → docs/PIN_ALLOCATION.md + include/pins.h in same commit.
- SensorData struct: only add fields, never remove or rename.

## Infrastructure detection

When any IP address, URL, password, certificate, or service credential appears in conversation:
Ask: "Should I save this to ~/.twwp/INFRASTRUCTURE.md?"
If yes, append it to the correct section of that file.

## Never

- Never edit include/secrets.h directly. Ask the user to fill it in manually.
- Never suggest `git add include/secrets.h`.
- Never use delay() or blocking calls > 10s.
- Never hardcode GPIO numbers outside include/pins.h.
```

- [ ] **Step 2: Verify**

```bash
head -10 "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/AGENTS.md"
```
Expected: "Read first — always" heading visible.

- [ ] **Step 3: Commit**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add AGENTS.md
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "chore: add AGENTS.md for Codex cross-tool sync"
```

---

## Task 10: Rename and trim HANDOFF_TO_ROO.md → TASK_QUEUE.md

**Files:**
- Rename: `docs/HANDOFF_TO_ROO.md` → `docs/TASK_QUEUE.md`
- Append: `docs/FIRMWARE_ARCHITECTURE.md` (ESPHome notes)

- [ ] **Step 1: Move ESPHome notes to FIRMWARE_ARCHITECTURE.md**

Append to the end of `/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/docs/FIRMWARE_ARCHITECTURE.md`:

```markdown
---

## ESPHome (prototyping only — not production)

ESPHome is useful for:
- Quickly validating new sensor wiring before writing C++ drivers
- Confirming Modbus register addresses on the YiErYi 3788 (M5) before implementing in PlatformIO
- Testing DS18B20 1-Wire addresses at boot

ESPHome is not used for production firmware — it lacks offline buffering, custom MQTT retry
logic, and the FreeRTOS task structure this project requires.
```

- [ ] **Step 2: Rename the file using git mv**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" mv docs/HANDOFF_TO_ROO.md docs/TASK_QUEUE.md
```

- [ ] **Step 3: Trim TASK_QUEUE.md — remove redundant sections**

Rewrite `/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/docs/TASK_QUEUE.md` keeping only the milestone task checkboxes. Replace the entire file content with:

```markdown
# TWWP Task Queue

Ordered milestone task list. Pick up at the first unchecked `[ ]` item.
Design rules, locked decisions, and standing rules are in `docs/FIRMWARE_ARCHITECTURE.md`.

---

## M0 — bring-up

### M0.1 — User-side prep
- [ ] Copy `include/secrets.h.sample` → `include/secrets.h`. Fill in MQTT_HOST, MQTT_PORT (8883), MQTT_USER, MQTT_PASS, MQTT_CA_CERT, NODE_ID.
- [ ] Format microSD card FAT32. Insert with CR2032 in DS3231.
- [ ] Wire per `docs/WIRING_M0.md`.
- [ ] `pio run` — compiles clean.
- [ ] `pio run -t upload` + `pio device monitor`.
- [ ] Verify serial output matches `WIRING_M0.md` "what to look for" section.
- [ ] Drip water on leak probe → HA `binary_sensor` flips + entry in `/log/YYYY-MM-DD.csv`.

### M0.2 — Offline buffering
- [ ] Block MQTT (kill broker or pull WiFi).
- [ ] Trigger 5–10 leak transitions.
- [ ] Confirm `/buf/` accumulates files.
- [ ] Restore broker → files drain in order, HA shows historical transitions.

### M0.3 — Polish
- [ ] **Buffer overflow cap.** `storeSd_bufferMessage()` has no cap. If `s_seq - oldestSeq > SD_MAX_BUFFER_LINES`, delete oldest before writing, append warning to `/log/crashes.txt`.
- [ ] **SD-failure surfacing.** On write failure in `store_sd.cpp`, publish `"sd write failed"` to `twwp/<id>/log` via MQTT (rate-limited 1/min).
- [ ] **Reset-creds gesture.** `digitalRead(0) == LOW` held > 5s in `loop()` → `netWifi_resetCredentials()`.
- [ ] **Heartbeat enrichment.** Add `wifi_ssid`, `ip`, `mqtt_buffer_count` to heartbeat JSON.
- [ ] **HA device availability.** Single `device_availability` block on all discovery payloads — DRY.

---

## M0.5 — TLS + security hardening

> Do this before M1. Public MQTT without TLS is unacceptable.

### Server-side (user does manually)
- [ ] Let's Encrypt cert exists for `mqtt.twwp.nz`. If not: `certbot certonly --standalone -d mqtt.twwp.nz`.
- [ ] `mosquitto.conf`: `listener 8883`, cert/key/cafile paths, `allow_anonymous false`, `password_file`.
- [ ] `ufw deny 1883/tcp`. `ufw allow 8883/tcp`.
- [ ] Add per-device passwords: `mosquitto_passwd -b /mosquitto/config/passwordfile twwp_wh_001 <pass>`.
- [ ] Verify: `mosquitto_pub -h mqtt.twwp.nz -p 8883 --cafile ca.crt -u twwp_wh_001 -P <pass> -t test -m hello`.

### Firmware-side (agent does)
- [ ] `secrets.h.sample`: add `MQTT_PORT 8883` and `MQTT_CA_CERT` (raw string PEM literal).
- [ ] `net_mqtt.cpp`: replace `WiFiClient` with `WiFiClientSecure`. Call `client.setCACert(MQTT_CA_CERT)` before connect.
- [ ] `config.h`: `#define MQTT_CLIENT_ID "twwp_" NODE_ID`.
- [ ] On TLS handshake failure: log SSL error code to serial + SD crashes log. Retry with backoff.
- [ ] Update `docs/MQTT_TOPIC_MAP.md` — note port 8883 only.

---

## M1 — Hall flow sensor

- [ ] Confirm part number with user (YF-S201? K-factor depends on part).
- [ ] Update `docs/PIN_ALLOCATION.md` — commit GPIO4 for flow #1.
- [ ] Replace `sensor_flow_stub.{h,cpp}` with `sensor_flow.{h,cpp}`.
- [ ] HA discovery: `flow_rate` (`measurement`, `L/min`) and `flow_total` (`total_increasing`, `L`).
- [ ] Update `docs/MQTT_TOPIC_MAP.md` and `docs/HA_DISCOVERY.md`.

---

## M2 — Pressure + DS18B20

- [ ] Confirm pressure transducer model with user (need PSI range and output voltage).
- [ ] Pressure: averaged ADC read on GPIO7. Voltage divider 2:1 (0–5V → 0–2.5V).
- [ ] DS18B20: `OneWire` + `DallasTemperature`. Auto-discover ROMs at boot, expose by index.
- [ ] HA discovery: `pressure` and `temperature` per probe.

---

## M3 — Solenoid command channel

- [ ] Confirm device with user — solenoid or flow switch.
- [ ] If solenoid: N-MOSFET driver (IRLZ44N + 1N4007 flyback). Document in `docs/SOLENOID_DRIVER.md`.
- [ ] Replace `actuator_solenoid_stub.{h,cpp}`.
- [ ] `net_mqtt.cpp::onMessage`: parse `{"solenoid":"open"|"close"}`.
- [ ] Safety: auto-close after N minutes if no confirmation, configurable in `node.json`.

---

## M4 — OTA over MQTT

- [ ] Decide: `ArduinoOTA` (LAN) vs MQTT-driven OTA (internet).
- [ ] If MQTT-driven: subscribe `twwp/<id>/ota` for URL, fetch with `HTTPClient`, write with `Update.h`.
- [ ] Rollback: if boot crashes within 60s, `esp_ota_set_boot_partition` to known-good partition.

---

## M5 — YiErYi 3788 RS485

Blocked on hardware debug. When unblocked:

- [ ] Use ESPHome to confirm Modbus register addresses and baud rate.
- [ ] Read `references/Modbus Communication Data Format-V1.01.xlsx` to confirm slave address, baud, register map.
- [ ] Add `sensor_yieryi.{h,cpp}` using UART1 (`UART_MODE_RS485_HALF_DUPLEX`, GPIO17/18, GPIO21 auto DE/RE).
- [ ] HA discovery: pH, ORP, EC, TDS, CF, water temp, RH.
- [ ] Staleness watchdog: no successful read in 60s → mark unavailable.

---

## M6 — HealthService + CalibrationService

- [ ] `src/services/HealthService.{h,cpp}`: validates `SensorData`, sets `flow_ok`, `pressure_ok`, `power_ok`.
- [ ] `src/services/CalibrationService.{h,cpp}`: loads from `node.json` calibration block.
- [ ] Extend `SensorData` struct — apply calibration in drivers, never raw values in MQTT payload.
- [ ] Document field calibration in `docs/CALIBRATION.md`.

---

## M7 — AlertService + TelemetryService

- [ ] `src/services/AlertService.{h,cpp}`: fires on state change only. Types: LEAK_DETECTED, FLOW_ANOMALY, PRESSURE_OUT_OF_RANGE, LOW_VOLTAGE, SENSOR_FAILURE, DEVICE_REBOOT.
- [ ] `src/services/TelemetryService.{h,cpp}`: sends snapshot to `twwp/<id>/status` every 10s.
- [ ] Main loop order: `healthService → alertService → ruleEngine → telemetryService`.

---

## M8 — Device lifecycle

- [ ] First-connect registration: publish `{device_id, firmware_version, mac, ip}` to `twwp/register`.
- [ ] Decommission command: `{"action":"decommission"}` → wipe NVS, reboot to captive portal.
- [ ] MQTT rate-limit guard: > 60 publishes/min → back off + warn.
- [ ] Document credential rotation in `docs/DEVICE_LIFECYCLE.md`.
```

- [ ] **Step 4: Verify the rename and content**

```bash
ls "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/docs/" | grep -E "TASK|HANDOFF"
head -5 "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/docs/TASK_QUEUE.md"
```
Expected: `TASK_QUEUE.md` present, `HANDOFF_TO_ROO.md` absent. Header shows "TWWP Task Queue".

- [ ] **Step 5: Commit**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" add docs/FIRMWARE_ARCHITECTURE.md docs/TASK_QUEUE.md
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" commit -m "refactor: rename HANDOFF_TO_ROO to TASK_QUEUE, move ESPHome notes to architecture doc"
```

---

## Task 11: Verify everything end-to-end

- [ ] **Step 1: Confirm all files exist**

```bash
echo "=== Claude commands ===" && ls "/home/kenny/Documents/Waveshare build TWWP/.claude/commands/"
echo "=== CLAUDE.md ===" && ls "/home/kenny/Documents/Waveshare build TWWP/CLAUDE.md"
echo "=== Roo mode ===" && ls "/home/kenny/Documents/Waveshare build TWWP/.roo/modes/iot-engineer/"
echo "=== Roo skills ===" && ls "/home/kenny/Documents/Waveshare build TWWP/.roo/skills/iot-engineer/"
echo "=== Firmware project ===" && ls "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/AGENTS.md" && ls "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1/docs/SESSION.md"
echo "=== Registry ===" && ls /home/kenny/.twwp/INFRASTRUCTURE.md
```

Expected: all files listed, no "No such file" errors.

- [ ] **Step 2: Test twwp-start (Claude)**

In a Claude Code session, run: `/twwp-start`

Expected output format:
```
**Where we are:** M0 complete — [2 sentences about current state]
**Next action:** [specific next task from SESSION.md]
```

- [ ] **Step 3: Verify Roo mode loads correctly**

Open Roo and select the `iot-engineer` mode. Confirm:
- The briefing shows TWWP-specific milestone info (not "50,000 devices" generic text)
- SESSION.md content is referenced in the briefing

- [ ] **Step 4: Test infrastructure detection**

In any tool (Claude, Roo, or Codex), mention: "The server IP is 91.98.133.15"

Expected: tool asks "Should I save this to ~/.twwp/INFRASTRUCTURE.md?"

- [ ] **Step 5: Final git status check**

```bash
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" log --oneline -5
git -C "/home/kenny/Documents/PlatformIO/Projects/TWWP Sensor Node v1" status
```
Expected: clean working tree, recent commits visible including SESSION.md and AGENTS.md.
