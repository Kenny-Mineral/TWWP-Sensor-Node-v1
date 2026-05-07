# TWWP Sensor Node — Codex Agent Instructions

## Read first — always, before touching any file

1. `docs/SESSION.md` — where the project is right now
2. `docs/FIRMWARE_ARCHITECTURE.md` — architecture reference, locked decisions, design rules
3. `docs/TASK_QUEUE.md` — ordered task queue, pick up at the first unchecked [ ] task

## Before stopping any session

Gather context first — do not ask open questions yet:
1. Read `docs/SESSION.md`
2. Run: `git log --oneline -5`
3. Run: `git status --short`
4. Read `docs/TASK_QUEUE.md`

Then present this to the user and wait for confirmation:

---
**Handoff — confirm or correct before I commit:**

**Last done:** [derived from session work, git log, SESSION.md — 1–2 sentences]
**Uncommitted files:** [from git status, or "none"]
**Next step:** [first unchecked [ ] task from TASK_QUEUE.md]

**Docs to update — yes/no:**
- USER_OPERATIONS.md — anything a user operating the device needs to know?
- MQTT_TOPIC_MAP.md — any new MQTT topics?
- PIN_ALLOCATION.md — any new GPIO pins?
---

Apply any corrections the user gives. Make any doc updates they confirm. Then update `docs/SESSION.md`:
- Last done: [confirmed summary]
- In progress: [uncommitted files, or "none"]
- Next step: [confirmed next step]
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
