#pragma once

#include <Arduino.h>

bool netAp_begin();
void netAp_loop();
bool netAp_start(uint32_t durationSeconds);
void netAp_stop();
bool netAp_isActive();
const char* netAp_getSsid();
uint8_t netAp_getClientCount();
uint32_t netAp_getExpiresS();
bool netAp_rotateUploadToken();
