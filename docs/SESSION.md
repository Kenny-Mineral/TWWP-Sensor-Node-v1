# TWWP Session State
_Update before switching tools. Commit immediately after._

## Last done
ESPHome working — created minimal ESPHome leak-sensor config, flashed to board, confirmed leak sensor fires in Home Assistant via MQTT. Original PlatformIO firmware flash is next.

## In progress
docs/WIRING_M0.md, include/pins.h, include/secrets.h.sample, platformio.ini, src/main.cpp, src/net_mqtt.cpp, src/net_mqtt.h, src/net_wifi.cpp, src/sensor_leak.cpp, src/status_led.cpp, src/store_sd.cpp, src/time_rtc.cpp, src/watchdog.cpp (uncommitted modifications)

## Next step
Flash original PlatformIO firmware: hold BOOT, tap RESET, release BOOT, then run `pio run -t upload` from the firmware project dir. Monitor with `pio device monitor` and verify leak sensor fires in HA under WH-001 device.

## Tool last used
claude-code

## Updated
2026-04-27 09:00
