# Design Spec — Cross-Tool Sync + Infrastructure Registry
_Date: 2026-04-23_

## Problem

The TWWP project uses Claude Code, Roo, and Codex interchangeably within the same work session. No tool has context about what another just did, causing repeated re-explanation and risk of contradicting previous work. Infrastructure credentials, IPs, certs, and service details are scattered across chat history with no single lookup point for provisioning new ESP32 nodes.

The user is an amateur developer. Guardrails preventing common firmware mistakes (blocking calls, insecure TLS, hardcoded pins) are as important as the sync tooling.

---

## Design

### Two shared files — the foundation of everything

#### `docs/SESSION.md` (firmware project, committed to git)

Updated before every tool switch. Three fields only.

```markdown
# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done
One sentence — what was fully completed.

## In progress
What is currently being worked on. List any uncommitted file changes.

## Next step
The single next action — specific enough to act on immediately.

## Tool last used
claude-code | roo | codex

## Updated
YYYY-MM-DD HH:MM
```

**Location:** `docs/SESSION.md` in the firmware project root.
**Who reads it:** All three tools at session start.
**Who writes it:** All three tools before handing off.

---

#### `~/.twwp/INFRASTRUCTURE.md` (home directory, never committed)

Single lookup for all infrastructure details when provisioning new nodes or debugging. Append-only — tools add new entries when detected in conversation.

Sections:
- Hosting — Hetzner (account, IP, SSH key)
- DNS — DuckDNS (domain, token, auto-renew)
- DNS — Cloudflare (account, API token, zone ID)
- Certificates — Let's Encrypt (domain, path, renewal)
- MQTT Broker — Mosquitto (host, port, config path)
- Home Assistant (Tailscale URL, local URL, Docker path)
- VPN — Tailscale (account, server IP)
- Local Network (WiFi SSID, password, router, subnet)
- WiFi Provisioning (portal SSID, password)
- OTA — Firmware Binary Hosting (hosting method, base URL)
- Notifications (service, tokens)
- Git Remote — Firmware (provider, repo URL)
- Docker — Server Containers (images, compose path)
- Node Credentials (per node: MQTT user/pass, NODE_ID, secrets.h path)
- Pipeline / Future Services (placeholders)

All unknown values use `[PLACEHOLDER]`.

---

### Three Claude Code skills

**`twwp-start`**
Reads `docs/SESSION.md`, `docs/FIRMWARE_ARCHITECTURE.md`, and `git log --oneline -5`.
Outputs a two-paragraph briefing: where the project is right now, and the single next action.
Run at the top of every Claude session.

**`twwp-handoff`**
Asks: "What did you just finish?"
Writes answer into `SESSION.md`, fills progress/next from git status and `HANDOFF_TO_ROO.md`.
Sets tool=claude-code, timestamp. Commits with `chore: session handoff [claude-code]`.
Run before switching to Roo or Codex.

**`twwp-firmware`**
Loads all locked decisions and design rules as active constraints for the session.
Enforces silently. Flags violations immediately.
Covers: no `delay()`, `watchdog_feed()` required, no `setInsecure()`, pin numbers only in `pins.h`, never commit `secrets.h`, `SensorData` struct field rules, driver interface pattern, doc-update requirements.

**Standing instruction in `CLAUDE.md`:**
Whenever an IP, URL, password, certificate content, file path (infrastructure), or service credential appears in conversation — ask: "Should I save this to the infrastructure registry?" If yes, append to `~/.twwp/INFRASTRUCTURE.md`.

---

### Roo `iot-engineer` mode and skill

**Mode** (`.roo/modes/iot-engineer`):
- Session start: read `SESSION.md` + `FIRMWARE_ARCHITECTURE.md`, output two-paragraph briefing.
- Session end: update `SESSION.md`, commit with `chore: session handoff [roo]`.
- Locked decisions enforced: same as `twwp-firmware`.
- Infrastructure detection: same standing instruction as CLAUDE.md.

**Skill** (`.roo/skills/iot-engineer`):
Compact mid-session reference containing:
- Flash workflow (DTR/GPIO0 issue, RESET button step)
- Driver inventory
- `SensorData` struct (full, with note: only add fields)
- SD card layout
- MQTT topic map
- New sensor driver checklist

---

### `AGENTS.md` — Codex (firmware project root)

Read-first instructions: `docs/SESSION.md` → `docs/FIRMWARE_ARCHITECTURE.md` → `docs/HANDOFF_TO_ROO.md`.
Before stopping: update `SESSION.md`, commit.
Same locked decisions, design rules, and infrastructure detection instruction as the other tools.
Additional note: never edit `include/secrets.h` directly — prompt the user.

---

## Data flow

```
User switches tool
    ↓
Outgoing tool writes SESSION.md → git commit
    ↓
Incoming tool reads SESSION.md at session start
    ↓
Two-paragraph briefing output → work resumes
```

```
Infrastructure data appears in conversation
    ↓
Tool detects (IP / URL / password / cert / path)
    ↓
"Save to registry?" prompt
    ↓
User says yes → tool appends to ~/.twwp/INFRASTRUCTURE.md
```

---

## Files created / modified

| File | Action |
|---|---|
| `docs/SESSION.md` | Create (firmware project) |
| `~/.twwp/INFRASTRUCTURE.md` | Create (home dir, outside git) |
| Claude skill: `twwp-start` | Create |
| Claude skill: `twwp-handoff` | Create |
| Claude skill: `twwp-firmware` | Create |
| `CLAUDE.md` | Add infrastructure detection instruction |
| `.roo/modes/iot-engineer` | Populate (currently empty) |
| `.roo/skills/iot-engineer` | Populate (currently empty) |
| `AGENTS.md` | Create (firmware project root) |

---

## Success criteria

- Opening any tool mid-project produces a briefing from `SESSION.md` within 10 seconds, without the user explaining anything.
- Any IP/URL/credential mentioned in conversation triggers a save prompt.
- `~/.twwp/INFRASTRUCTURE.md` is the single place to look when provisioning a new node.
- Common firmware violations (blocking calls, insecure TLS, hardcoded pins) are flagged before code is committed.
