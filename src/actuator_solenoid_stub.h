#pragma once
#include <Arduino.h>

bool actuatorSolenoid_begin();
void actuatorSolenoid_loop();
void actuatorSolenoid_open();
void actuatorSolenoid_close();
bool actuatorSolenoid_isOpen();
