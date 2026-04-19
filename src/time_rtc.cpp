#include "time_rtc.h"

bool timeRtc_begin() {
    return true;
}

void timeRtc_loop() {
}

String timeRtc_getISOTimestamp() {
    return "1970-01-01T00:00:00Z";
}

String timeRtc_getDateString() {
    return "1970-01-01";
}

uint32_t timeRtc_getUnixTime() {
    return 0;
}

bool timeRtc_isSynced() {
    return false;
}
