#pragma once

#include <Arduino.h>

enum class OtaState : uint8_t {
    IDLE = 0,
    DOWNLOADING,
    VERIFYING,
    APPLYING,
    SUCCESS,
    FAILED
};

bool netOta_begin();
void netOta_loop();
bool netOta_beginUpdate(const char* url, const char* md5Expected = nullptr);

OtaState netOta_getState();
uint8_t netOta_getProgressPct();
const char* netOta_getError();
const char* netOta_getUrl();

void netOta_rollback();
bool netOta_isRollbackPending();
