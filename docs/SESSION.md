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
