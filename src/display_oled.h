#pragma once
#include <Arduino.h>

bool  displayOled_begin();
void  displayOled_loop();

// Override the calculated tank level — call from a dedicated level sensor when one is wired
void  displayOled_setTankLiters(float l);
float displayOled_getTankLiters();
