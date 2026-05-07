#pragma once
#include <Arduino.h>

bool actuatorValve_begin();
void actuatorValve_loop();
void actuatorValve_open();
void actuatorValve_close();
bool actuatorValve_isOpen();
void actuatorValve_setAuto(bool enable);
bool actuatorValve_isAuto();
