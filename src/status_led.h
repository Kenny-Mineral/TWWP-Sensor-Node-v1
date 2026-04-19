#pragma once
#include <Arduino.h>

enum class LedState {
    OFF,
    BOOTING,        // slow blue pulse
    WIFI_CONNECTING, // yellow blink
    MQTT_CONNECTING, // orange blink
    ONLINE,         // solid green
    LEAK_DETECTED,  // solid red
    ERROR           // fast red blink
};

bool statusLed_begin();
void statusLed_loop();
void statusLed_setState(LedState state);
