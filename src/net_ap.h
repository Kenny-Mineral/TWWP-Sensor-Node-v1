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
const char* netAp_getUploadToken();
uint32_t netAp_getAutoTriggerLossMs();
int netAp_getWeakRssiThreshold();
uint32_t netAp_getAutoDurationS();
void netAp_printDebug(Print& out, bool revealToken = false);
