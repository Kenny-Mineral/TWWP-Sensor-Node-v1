# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done

### Session 2026-05-15 — OLED display driver implemented and confirmed working

**Scope:** Implemented `src/display_oled.cpp/h` — 7-frame sliding carousel (Pre-RO, Post-RO, Remin, Flow & Waste, Storage Tank, Filter Health stub, Branding), persistent header overlay (today's volume, leak alert, WiFi/MQTT status), tactile button on GPIO10. Pre-RO/Post-RO frames use TDS meter data; Remin uses Yieryi. Added ThingPulse OLED library to `platformio.ini`. Wired and confirmed working on hardware.

**Key findings:**
- SH1.0 sockets carry no power rail — OLED VCC/GND must come from internal 2×12 header
- SH1.0 GPIO1/GPIO2 had no pull-ups adequate for I²C — OLED moved to main Wire bus (GPIO9/GPIO3, shared with DS3231/ADS1115, no address conflict)
- OLED I²C address confirmed 0x3C
- Screen orientation: `s_disp.flipScreenVertically()` required for this mounting

---

## In progress
none

## Next step

M2 bench test — or continue with M3 valve hardware bench test (9 verification checks in previous SESSION.md) when valve hardware arrives.

First unchecked task: M2 — Confirm pressure transducer model + PSI range with user before ordering.

## Tool last used
claude-code

## Updated
2026-05-15 18:00
