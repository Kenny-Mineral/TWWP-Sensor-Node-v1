#include "store_sd.h"

bool storeSd_begin() {
    return true;
}

void storeSd_loop() {
}

bool storeSd_logEvent(const char* msg) {
    return false;
}

bool storeSd_bufferMessage(const char* topic, const char* payload) {
    return false;
}

bool storeSd_drainBuffer(uint8_t maxMessages) {
    return false;
}
