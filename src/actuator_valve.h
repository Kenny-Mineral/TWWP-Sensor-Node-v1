#pragma once
#include <Arduino.h>

bool actuatorValve_begin();
void actuatorValve_loop();
void actuatorValve_open();
void actuatorValve_close();
bool actuatorValve_isOpen();
void actuatorValve_setAuto(bool enable);
bool actuatorValve_isAuto();

void        actuatorValve_setValveType(const char* type);
void        actuatorValve_setTriggerSource(const char* src);
void        actuatorValve_setIdleTimeoutS(uint32_t s);
void        actuatorValve_setMaxOpenS(uint32_t s);
void        actuatorValve_setTimeoutDisableAuto(bool v);
void        actuatorValve_setTimeoutAlert(bool v);
const char* actuatorValve_getValveType();
const char* actuatorValve_getTriggerSource();
uint32_t    actuatorValve_getIdleTimeoutS();
uint32_t    actuatorValve_getMaxOpenS();
bool        actuatorValve_getTimeoutDisableAuto();
bool        actuatorValve_getTimeoutAlert();
