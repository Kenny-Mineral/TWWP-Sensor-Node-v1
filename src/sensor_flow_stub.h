#pragma once
#include <Arduino.h>

bool sensorFlow_begin();
void sensorFlow_loop();
float sensorFlow_getLPM();      // Litres per minute
float sensorFlow_getDailyTotal(); // Litres since midnight
