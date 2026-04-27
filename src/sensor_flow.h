#pragma once
#include <Arduino.h>

bool  sensorFlow_begin();
void  sensorFlow_loop();
float sensorFlow_getRateLpm(uint8_t ch);   // L/min, ch = 1 or 2
float sensorFlow_getTotalL(uint8_t ch);    // lifetime total, L (persisted)
float sensorFlow_getTodayL(uint8_t ch);    // L since midnight
float sensorFlow_getWeekL(uint8_t ch);     // L since Monday midnight
float sensorFlow_getMonthL(uint8_t ch);    // L since 1st of month
float sensorFlow_getYearL(uint8_t ch);     // L since 1 Jan
float sensorFlow_getKFactor(uint8_t ch);   // active K value (pulses/L)
bool  sensorFlow_setKFactor(uint8_t ch, float k); // update K in RAM + save to node.json
void  sensorFlow_resetToday(uint8_t ch);          // ch=1, ch=2, or ch=0 for both
void  sensorFlow_resetTotals(uint8_t ch);         // ch=1, ch=2, or ch=0 for both — clears NVS too
