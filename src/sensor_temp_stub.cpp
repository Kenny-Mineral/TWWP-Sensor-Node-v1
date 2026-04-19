#include "sensor_temp_stub.h"

bool sensorTemp_begin() { return true; }
void sensorTemp_loop() {}
float sensorTemp_getCelsius(uint8_t index) { return 0.0f; }
uint8_t sensorTemp_getCount() { return 0; }
