#pragma once
#include <Arduino.h>

bool netWifi_begin();
void netWifi_loop();
bool netWifi_isConnected();
void netWifi_resetCredentials();
void netWifi_reconnect();        // disconnect and reconnect without clearing credentials
