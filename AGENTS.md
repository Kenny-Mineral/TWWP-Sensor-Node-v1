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
