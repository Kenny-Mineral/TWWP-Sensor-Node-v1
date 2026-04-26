# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done
M0 complete plus RTC/SD logger bring-up. PlatformIO firmware flashed and confirmed working. WiFi connects, MQTT TLS connects to twwp-iot.duckdns.org:8883, HA discovery publishes, leak sensor fires WET/DRY in HA under TWWP-WH-001 device, telemetry publishes every 10s with non-zero RTC timestamps.

DS3231 on GPIO9/GPIO3 is detected and NTP drift sync works. SD logging is verified through the serial console: `sdls /log` lists `/log/2026-04-26.csv`, and `sdcat /log/2026-04-26.csv` dumps leak events over USB. Existing `2000-00-00.csv` is a harmless pre-fix artifact; invalid RTC reads now fall back to `/log/unsynced.csv`.

## In progress
Nothing — RTC/SD logger bring-up verified and ready for next module.

## Next step
Continue data logger polish or move to the next hardware module. Useful logger polish items: delete old malformed SD files over serial, add `sdrm <path>`, and expose SD status/buffer count in heartbeat JSON.

## Tool last used
codex

## Updated
2026-04-27 17:12
