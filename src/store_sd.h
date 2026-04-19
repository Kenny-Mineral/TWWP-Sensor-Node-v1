#pragma once
#include <Arduino.h>

bool storeSd_begin();
void storeSd_loop();
bool storeSd_logEvent(const char* msg);           // Append to daily CSV
bool storeSd_bufferMessage(const char* topic, const char* payload); // Ring-buffer for offline MQTT
bool storeSd_drainBuffer(uint8_t maxMessages = 10); // Drain buffered msgs to MQTT
