#pragma once
#include <Arduino.h>

bool netMqtt_begin();
void netMqtt_loop();
bool netMqtt_isConnected();
bool netMqtt_publish(const char* topic, const char* payload, bool retain = false);
void netMqtt_publishSub(const char* topic, const char* payload); // SD-buffered publish
