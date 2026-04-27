#pragma once
#include <Arduino.h>

typedef void (*MqttCmdCallback)(const char* payload);

bool netMqtt_begin();
void netMqtt_loop();
bool netMqtt_isConnected();
bool netMqtt_takeJustConnected();
bool netMqtt_publish(const char* topic, const char* payload, bool retain = false);
void netMqtt_publishSub(const char* topic, const char* payload); // SD-buffered publish
void netMqtt_setCmdCallback(MqttCmdCallback cb);                 // register cmd topic handler
