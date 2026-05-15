#pragma once
#include <Arduino.h>

bool        wqConfig_begin();
void        wqConfig_publishState();
bool        wqConfig_handleCmd(const char* payload);

const char* wqConfig_getPreRoName();
const char* wqConfig_getPostRoName();
const char* wqConfig_getReminName();

float       wqConfig_getPreRoMax();
float       wqConfig_getPostRoGoodMax();
float       wqConfig_getPostRoCheckMax();
float       wqConfig_getReminMin();
float       wqConfig_getReminMax();

const char* wqConfig_evalPreRo(float tds_ppm);
const char* wqConfig_evalPostRo(float tds_ppm);
const char* wqConfig_evalRemin(float tds_ppm);
