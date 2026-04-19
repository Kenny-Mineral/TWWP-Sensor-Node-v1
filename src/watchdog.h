#pragma once
#include <Arduino.h>

bool watchdog_begin();
void watchdog_feed();
void watchdog_logCrash(const char* reason);
