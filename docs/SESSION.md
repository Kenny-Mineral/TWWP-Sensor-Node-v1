# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done
M0 complete — PlatformIO firmware flashed and confirmed working. WiFi connects, MQTT TLS connects to twwp-iot.duckdns.org:8883, HA discovery published, leak sensor fires WET/DRY in HA under TWWP-WH-001 device. Telemetry publishing every 10s.

## In progress
Nothing — M0 committed and clean.

## Next step
Start M1: wire and bring up DS3231 RTC + SD card logger. DS3231 on GPIO9(SDA)/GPIO3(SCL) addr 0x68. Implement time_rtc.cpp (NTP sync + drift correction) and store_sd.cpp (daily CSV log + offline MQTT buffer). Verify `ts` field in telemetry is non-zero and CSV appears on SD card.

## Tool last used
claude-code

## Updated
2026-04-27 14:00
