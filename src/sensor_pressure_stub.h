#pragma once
#include <Arduino.h>

bool sensorPressure_begin();
void sensorPressure_loop();
float sensorPressure_getKPa();
