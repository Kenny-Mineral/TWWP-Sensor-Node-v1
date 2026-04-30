# M4 — OTA Firmware Update Design

> **Status:** Plan — awaiting implementation
> **Decisions:** Hetzner hosting, MQTT-driven + ArduinoOTA, HTTPS + MD5, serial `ota` command

---

## 1. Overview

ESP32-S3 with 16MB flash, [`default_16MB.csv`](file:///home/kenny/.platformio/packages/framework-arduinoespressif32/tools/partitions/default_16MB.csv:1) partition scheme provides two 6.25MB app slots (`app0`/ota_0 and `app1`/ota_1). The ESP32 can write new firmware to the inactive slot while running, then swap on reboot.

Two OTA paths:
- **ArduinoOTA** — LAN-only, for development convenience (wireless PlatformIO uploads)
- **MQTT-driven OTA** — Internet-capable, triggered from Home Assistant or any MQTT client

---

## 2. Files Changed / Created

| File | Action | Purpose |
|------|--------|---------|
| [`src/net_ota.h`](src/net_ota.h) | **Create** | OTA driver public API |
| [`src/net_ota.cpp`](src/net_ota.cpp) | **Create** | OTA driver implementation |
| [`include/config.h`](include/config.h) | **Modify** | New topic define, timing constants |
| [`src/main.cpp`](src/main.cpp) | **Modify** | Command handler, serial console, setup/loop integration |
| [`platformio.ini`](platformio.ini) | **Modify** | No changes needed — ArduinoOTA is built into ESP32 Arduino framework |
| [`docs/MQTT_TOPIC_MAP.md`](docs/MQTT_TOPIC_MAP.md) | **Modify** | Document `ota_state`/`ota_progress_pct` in status topic |
| [`docs/USER_OPERATIONS.md`](docs/USER_OPERATIONS.md) | **Modify** | Document OTA procedures |
| [`docs/TASK_QUEUE.md`](docs/TASK_QUEUE.md) | **Modify** | Mark M4 items as they complete |

---

## 3. Component Design

### 3.1 `src/net_ota.h` — Public API

```cpp
#pragma once
#include <Arduino.h>

// OTA state enum — readable by main.cpp for status publishing
enum class OtaState : uint8_t {
    IDLE,
    DOWNLOADING,
    VERIFYING,
    APPLYING,
    SUCCESS,   // transient — reboot imminent
    FAILED
};

// Initialize OTA subsystem
// - Sets up ArduinoOTA (optional, enabled via NODE_ENABLE_ARDUINO_OTA)
// - Registers rollback check on boot
bool netOta_begin();

// Call each loop iteration — drives ArduinoOTA handling
void netOta_loop();

// Begin MQTT-driven OTA from URL
// url: HTTPS URL to firmware binary
// md5_expected: optional hex string (32 chars) for verification; nullptr to skip
// Returns true if download started successfully
bool netOta_beginUpdate(const char* url, const char* md5_expected = nullptr);

// Query current state and progress
OtaState netOta_getState();
uint8_t  netOta_getProgressPct();   // 0-100
const char* netOta_getError();      // human-readable error, or nullptr
const char* netOta_getUrl();        // currently downloading URL, or nullptr

// Force rollback to previous partition (called on boot if crash detected)
void netOta_rollback();
bool netOta_isRollbackPending();    // true if we just rolled back
```

### 3.2 `src/net_ota.cpp` — Implementation

**State machine:**

```
IDLE ──beginUpdate()──▶ DOWNLOADING ──complete──▶ VERIFYING ──md5 ok──▶ APPLYING ──reboot──▶ (boot)
  ▲                        │                       │                    │
  │                        │ HTTP fail             │ md5 mismatch       │
  │                        ▼                       ▼                    │
  └──────────────────── FAILED ◀───────────────────┘                    │
                                                                        │
  (boot) ──check rollback flag──▶ normal boot ──mark_app_valid────────▶ IDLE
                     │
                     └── crash detected ──rollback──▶ IDLE (with rollback flag)
```

**Key implementation details:**

- **Download**: Use `WiFiClientSecure` (already configured with CA cert in [`net_mqtt.cpp`](src/net_mqtt.cpp:62)). Stream directly into `Update` object — no full-firmware buffer needed. PSRAM could hold it but streaming is simpler.
- **`Update.h` API**: `Update.begin(size)`, `Update.write(buf, len)`, `Update.end()`, `Update.isFinished()`, `Update.getError()`. The library automatically targets the inactive OTA partition.
- **MD5 verification**: ESP32 has hardware-accelerated MD5 via `mbedtls/md5.h`. Run on the streamed data (can compute incrementally as chunks arrive, or verify after write completes by reading back the partition). Simpler: compute during download and compare at end.
- **Progress reporting**: Track bytes written vs content-length header. Publish to status topic every 10% increment.
- **Rollback flag**: Store `ota_boot_pending=1` in NVS ("ota" namespace) before reboot. In `netOta_begin()`, check if flag is set — if so, start a 60s timer. If `netOta_loop()` is called after timer expires without `mark_app_valid`, trigger rollback.
- **`esp_ota_mark_app_valid_cancel_rollback()`**: Called after 60s of stable operation — commits the new firmware.
- **`esp_ota_set_boot_partition()`**: Called on rollback — switches back to the previous partition, then reboot.

**ArduinoOTA integration:**
- Compile-time guard: `#ifdef NODE_ENABLE_ARDUINO_OTA` or always-on (simpler)
- `ArduinoOTA.setHostname("twwp-" NODE_ID)`
- `ArduinoOTA.setPasswordHash(...)` — optional, could load from secrets
- `ArduinoOTA.begin()` in `netOta_begin()`
- `ArduinoOTA.handle()` in `netOta_loop()` — non-blocking, just services pending OTA packets
- Only active when `OtaState::IDLE` — if an MQTT OTA is in progress, ArduinoOTA is ignored

### 3.3 `include/config.h` — Additions

```cpp
// OTA (M4)
#define TOPIC_OTA_STATE     "twwp/" NODE_ID "/ota_state"    // retained — OTA status
#define OTA_ROLLBACK_TIMEOUT_MS   60000UL   // 60s stable before committing new firmware
#define OTA_HTTP_TIMEOUT_MS       300000UL  // 5 min download timeout
#define OTA_PROGRESS_INTERVAL_MS  2000UL    // publish progress every 2s during download
```

### 3.4 `src/main.cpp` — Changes

**New include:**
```cpp
#include "net_ota.h"
```

**Command handler addition** (in [`handleCmd()`](src/main.cpp:951)):
```cpp
if (!doc["ota_url"].isNull()) {
    const char* url = doc["ota_url"].as<const char*>();
    const char* md5 = doc["ota_md5"].isNull() ? nullptr : doc["ota_md5"].as<const char*>();
    if (!netOta_beginUpdate(url, md5)) {
        Serial.println("[OTA] failed to start update");
    }
}
```

**Serial console addition** (in [`serviceSerialConsole()`](src/main.cpp:125)):
```cpp
} else if (strncmp(cmd, "ota ", 4) == 0) {
    // Parse: ota <url> [md5]
    char* rest = cmd + 4;
    while (*rest == ' ') ++rest;
    char* urlPart = rest;
    char* space = strchr(urlPart, ' ');
    char* md5Part = nullptr;
    if (space) { *space = '\0'; md5Part = space + 1; while (*md5Part == ' ') ++md5Part; }
    if (!netOta_beginUpdate(urlPart, md5Part)) {
        Serial.println("[OTA] failed to start update from serial command");
    }
} else if (strcmp(cmd, "ota_state") == 0) {
    Serial.printf("[OTA] state=%d progress=%d%% error=%s url=%s\n",
        (int)netOta_getState(), netOta_getProgressPct(),
        netOta_getError() ? netOta_getError() : "none",
        netOta_getUrl() ? netOta_getUrl() : "none");
}
```

**Setup addition** (in [`setup()`](src/main.cpp:1051)):
```cpp
netOta_begin();   // after netMqtt_begin(), before loop enters
```

**Loop addition** (in [`loop()`](src/main.cpp:1083)):
```cpp
netOta_loop();    // after netMqtt_loop()
```

**Heartbeat enrichment** (in [`publishM0Status()`](src/main.cpp:37)):
```cpp
doc["ota_state"]       = (uint8_t)netOta_getState();
doc["ota_progress_pct"] = netOta_getProgressPct();
if (netOta_getError()) {
    doc["ota_error"] = netOta_getError();
}
```

**New helper — OTA progress publisher** (in loop or heartbeat handler):
When `netOta_getState() == OtaState::DOWNLOADING`, publish progress every [`OTA_PROGRESS_INTERVAL_MS`](include/config.h) to `TOPIC_OTA_STATE` (retained) so HA can show live progress.

---

## 4. Rollback Mechanism — Detailed Flow

```
BOOT SEQUENCE (in netOta_begin):
──────────────────────────────────────────────────────────
1. Check NVS key "ota_boot_pending" in "ota" namespace
2. If NOT set:
   → Normal boot. Start 60s timer.
   → After 60s, call esp_ota_mark_app_valid_cancel_rollback()
   → Clear any stale rollback flag
3. If SET:
   → Previous OTA boot was attempted
   → Check NVS key "ota_boot_ts" — when was the OTA applied?
   → If (now - ota_boot_ts) < OTA_ROLLBACK_TIMEOUT_MS:
       → CRASH DETECTED — firmware didn't survive 60s
       → Log to SD: "[OTA] rollback — new firmware crashed within 60s"
       → Call esp_ota_set_boot_partition(previous_partition)
       → Clear NVS flags
       → ESP.restart()
   → If (now - ota_boot_ts) >= OTA_ROLLBACK_TIMEOUT_MS:
       → Firmware survived, but flag wasn't cleared (edge case)
       → Mark as valid, clear flags

PRE-REBOOT (in netOta_beginUpdate, after Update.end() success):
──────────────────────────────────────────────────────────
1. Store current timestamp to NVS "ota_boot_ts"
2. Set NVS "ota_boot_pending" = 1
3. Publish "ota_state: applying" to MQTT
4. delay(500) to let MQTT message send
5. ESP.restart()
```

---

## 5. MQTT Integration

### 5.1 Command Topic

Topic: [`twwp/<id>/cmd`](include/config.h:9) (existing)

New recognized keys in [`handleCmd()`](src/main.cpp:951):

| Key | Type | Effect |
|-----|------|--------|
| `ota_url` | string | HTTPS URL to firmware .bin |
| `ota_md5` | string (optional) | Expected MD5 hex digest (32 chars) |

Example payload (published by HA automation or manual MQTT tool):
```json
{
  "ota_url": "https://twwp-iot.duckdns.org/firmware/twwp-v0.2.0.bin",
  "ota_md5": "d41d8cd98f00b204e9800998ecf8427e"
}
```

### 5.2 Status Reporting

Fields added to [`TOPIC_STATUS`](include/config.h:5) heartbeat JSON:

| Key | Type | Description |
|-----|------|-------------|
| `ota_state` | int | 0=IDLE, 1=DOWNLOADING, 2=VERIFYING, 3=APPLYING, 4=SUCCESS, 5=FAILED |
| `ota_progress_pct` | int | 0-100 during DOWNLOADING |
| `ota_error` | string (optional) | Human-readable error when FAILED |

### 5.3 Dedicated OTA State Topic

Topic: `twwp/<id>/ota_state` (retained)

Published during OTA with higher frequency (every 2s during download) so HA dashboards can show live progress without waiting for the 10s heartbeat.

Payload:
```json
{
  "state": "downloading",
  "progress_pct": 45,
  "url": "https://twwp-iot.duckdns.org/firmware/twwp-v0.2.0.bin",
  "error": null
}
```

---

## 6. Home Assistant Integration

### 6.1 Sensor Entities (diagnostic, read-only)

| Entity ID | Name | Maps to |
|-----------|------|---------|
| `sensor.twwp_<id>_ota_state` | OTA State | `ota_state` from status (0-5) |
| `sensor.twwp_<id>_ota_progress` | OTA Progress | `ota_progress_pct` from status (%) |

Both use `value_template` to extract from [`TOPIC_STATUS`](include/config.h:5).

### 6.2 HA Automation Example

```yaml
# Manually trigger OTA from HA — publish to MQTT
action: mqtt.publish
data:
  topic: twwp/wh_001/cmd
  payload: '{"ota_url": "https://twwp-iot.duckdns.org/firmware/twwp-v0.2.0.bin", "ota_md5": "abc123..."}'
```

### 6.3 Discovery Payloads

Published in a new `publishHaDiscoveryOta()` function, called from [`publishOnlineState()`](src/main.cpp:889):

```cpp
// OTA state sensor
publishHaDiagSensor("twwp_" NODE_ID "_ota_state", "OTA State",
    "ota_state", "", "", "measurement");
// OTA progress sensor
publishHaDiagSensor("twwp_" NODE_ID "_ota_progress", "OTA Progress",
    "ota_progress_pct", "%", "", "measurement");
```

---

## 7. Serial Console

**New commands:**

| Command | Description |
|---------|-------------|
| `ota <url> [md5]` | Trigger OTA update from serial |
| `ota_state` | Show current OTA state, progress, error, URL |
| `help` | Updated to list `ota <url> [md5]` and `ota_state` |

---

## 8. Server-Side — Hetzner nginx

The firmware `.bin` file is produced by `pio run` and found at:
```
.pio/build/waveshare-esp32-s3-rs485-can/firmware.bin
```

On the Hetzner server, add to nginx config:

```nginx
# Serve firmware binaries for TWWP OTA
location /firmware/ {
    alias /var/www/twwp/firmware/;
    autoindex on;           # optional — list available files
    add_header Cache-Control "no-cache";
    
    # Only allow GET
    limit_except GET {
        deny all;
    }
}
```

Upload flow:
1. Build locally: `pio run`
2. Rename: `cp .pio/build/.../firmware.bin twwp-v0.2.0.bin`
3. Compute MD5: `md5sum twwp-v0.2.0.bin`
4. SCP to Hetzner: `scp twwp-v0.2.0.bin hetzner:/var/www/twwp/firmware/`
5. Trigger OTA via HA or serial with the URL and MD5

---

## 9. Implementation Sequence

Tasks ordered for incremental testing:

### M4.1 — Core OTA driver
- [ ] Create [`src/net_ota.h`](src/net_ota.h) and [`src/net_ota.cpp`](src/net_ota.cpp)
- [ ] Implement state machine (IDLE, DOWNLOADING, VERIFYING, APPLYING, FAILED)
- [ ] HTTPS download via `WiFiClientSecure` + `Update.h`
- [ ] MD5 verification (streaming computation)
- [ ] Progress tracking and error handling
- [ ] NVS rollback flag management

### M4.2 — Rollback mechanism
- [ ] `netOta_begin()` boot-time rollback check
- [ ] `esp_ota_mark_app_valid_cancel_rollback()` after 60s stable
- [ ] `esp_ota_set_boot_partition()` on crash detection
- [ ] SD crash log entry on rollback

### M4.3 — MQTT command integration
- [ ] Add `ota_url` and `ota_md5` to [`handleCmd()`](src/main.cpp:951)
- [ ] Add `ota_state`, `ota_progress_pct`, `ota_error` to heartbeat JSON
- [ ] Publish `TOPIC_OTA_STATE` during download at 2s intervals
- [ ] Update [`docs/MQTT_TOPIC_MAP.md`](docs/MQTT_TOPIC_MAP.md)

### M4.4 — Serial console
- [ ] Add `ota <url> [md5]` command
- [ ] Add `ota_state` diagnostic command
- [ ] Update `help` output
- [ ] Update [`docs/USER_OPERATIONS.md`](docs/USER_OPERATIONS.md)

### M4.5 — ArduinoOTA
- [ ] Enable ArduinoOTA in `netOta_begin()` (guarded by OTA state — only when IDLE)
- [ ] Call `ArduinoOTA.handle()` in `netOta_loop()`
- [ ] Set hostname to `twwp-<NODE_ID>`
- [ ] Test wireless upload from PlatformIO

### M4.6 — HA discovery
- [ ] Add `publishHaDiscoveryOta()` with OTA state and progress sensors
- [ ] Call from [`publishOnlineState()`](src/main.cpp:889)
- [ ] Test entities appear in HA

### M4.7 — Server-side
- [ ] Configure nginx on Hetzner to serve `/firmware/` directory
- [ ] Upload test firmware binary
- [ ] End-to-end test: build → upload to Hetzner → trigger OTA from HA → verify new version boots

### M4.8 — Documentation
- [ ] Update [`docs/USER_OPERATIONS.md`](docs/USER_OPERATIONS.md) — OTA section
- [ ] Update [`docs/TASK_QUEUE.md`](docs/TASK_QUEUE.md) — check off completed items
- [ ] Update [`docs/FIRMWARE_ARCHITECTURE.md`](docs/FIRMWARE_ARCHITECTURE.md) — add net_ota to driver inventory

---

## 10. Design Rules Compliance

| Rule | Compliance |
|------|-----------|
| No `delay()` | ✅ `netOta_loop()` is non-blocking; download streams without blocking >10s; `watchdog_feed()` called in main loop |
| No blocking >10s without `watchdog_feed()` | ✅ HTTP download loops call `watchdog_feed()` periodically |
| Never `client.setInsecure()` | ✅ Uses existing `WiFiClientSecure` with CA cert; OTA URL must be HTTPS |
| Pin numbers only in `pins.h` | ✅ No new pins needed for OTA |
| New MQTT topic → update `MQTT_TOPIC_MAP.md` | ✅ `TOPIC_OTA_STATE` documented |
| New file follows driver pattern | ✅ `net_ota` follows `_begin()` / `_loop()` pattern |
| ArduinoJson v7 | ✅ All new JSON uses `JsonDocument` |
| PSRAM for large buffers | ✅ Stream to `Update` — no large buffer needed |
| FreeRTOS core 0 | ✅ No new tasks created |
| Offline-first | ✅ OTA only runs when WiFi is connected; doesn't block sensor operations |

---

## 11. Failure Modes

| Failure | Behaviour |
|---------|-----------|
| WiFi drops during download | `netOta_getState()` → FAILED, `netOta_getError()` → "WiFi disconnected". Publish to status. |
| HTTP 404 / server error | FAILED with HTTP status code in error. |
| TLS handshake failure | FAILED with TLS error. Never falls back to plain HTTP. |
| MD5 mismatch | FAILED with "MD5 mismatch: expected=... got=...". Firmware NOT applied. |
| `Update.end()` fails | FAILED with `Update.getError()` message. |
| New firmware crashes within 60s | Rollback to previous partition. Log to SD. Publish alert on next boot. |
| Power loss during download | On reboot, no `ota_boot_pending` flag → normal boot. OTA must be retried. |
| Power loss during `Update.end()` | OTA partition incomplete. Bootloader skips it. Boots from current partition. |
| NVS full / corrupt | FAILED with NVS error. Rollback safety net: if NVS is broken, firmware boots normally. |
