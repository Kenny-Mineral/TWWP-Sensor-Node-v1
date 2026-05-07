#pragma once
#include <Arduino.h>

enum YieryiZone : uint8_t {
    YIERYI_ZONE_PRE_RO = 0,
    YIERYI_ZONE_POST_RO = 1,
    YIERYI_ZONE_REMIN = 2,
    YIERYI_ZONE_COUNT = 3
};

bool sensorYieryi_begin();
void sensorYieryi_loop();
void sensorYieryi_forcePoll();

bool sensorYieryi_isEnabled(uint8_t zone);
bool sensorYieryi_isOnline(uint8_t zone);
bool sensorYieryi_hasPh(uint8_t zone);
bool sensorYieryi_hasOrp(uint8_t zone);
bool sensorYieryi_hasEc(uint8_t zone);
bool sensorYieryi_hasTemp(uint8_t zone);

float sensorYieryi_getPh(uint8_t zone);
int16_t sensorYieryi_getOrpMv(uint8_t zone);
float sensorYieryi_getEcUsCm(uint8_t zone);
float sensorYieryi_getTempC(uint8_t zone);
uint16_t sensorYieryi_getHumidityPct(uint8_t zone);
float sensorYieryi_getTdsPpm(uint8_t zone);

const char* sensorYieryi_getPhCalDate(uint8_t zone);
const char* sensorYieryi_getOrpCalDate(uint8_t zone);
const char* sensorYieryi_getEcCalDate(uint8_t zone);
bool sensorYieryi_setPhCalDate(uint8_t zone, const char* date);
bool sensorYieryi_setOrpCalDate(uint8_t zone, const char* date);
bool sensorYieryi_setEcCalDate(uint8_t zone, const char* date);
uint32_t sensorYieryi_getLastSuccessAgeMs(uint8_t zone);
uint16_t sensorYieryi_getFailCount(uint8_t zone);
const char* sensorYieryi_getLastError(uint8_t zone);
const char* sensorYieryi_getRawHex(uint8_t zone);

void sensorYieryi_printStatus(Print& out);
