#pragma once
#include <Arduino.h>

bool sensorTemp_begin();
void sensorTemp_loop();
float sensorTemp_getCelsius(uint8_t index = 0); // DS18B20 by index
uint8_t sensorTemp_getCount();
