# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done
ESPHome setup only — created minimal ESPHome leak-sensor config in ~/esphome-leak-test/, compiled and flashed to board, serial log connection not yet verified.

## In progress
docs/SESSION.md, docs/WIRING_M0.md, include/pins.h, include/secrets.h.sample, platformio.ini, src/main.cpp, src/net_mqtt.cpp, src/net_mqtt.h, src/net_wifi.cpp, src/sensor_leak.cpp, src/status_led.cpp, src/store_sd.cpp, src/time_rtc.cpp, src/watchdog.cpp (uncommitted modifications)

## Next step
Buffer overflow cap: in storeSd_bufferMessage(), if s_seq - oldestSeq > SD_MAX_BUFFER_LINES, delete oldest file before writing and append warning to /log/crashes.txt.

## Tool last used
claude-code

## Updated
2026-04-26 09:00
