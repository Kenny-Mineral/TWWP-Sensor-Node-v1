#pragma once
#include <Arduino.h>

bool timeRtc_begin();
void timeRtc_loop();
String timeRtc_getISOTimestamp();  // "YYYY-MM-DDTHH:MM:SSZ"
String timeRtc_getDateString();    // "YYYY-MM-DD" (for log filenames)
uint32_t timeRtc_getUnixTime();
bool timeRtc_isSynced();
