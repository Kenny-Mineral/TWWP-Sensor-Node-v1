#include "net_mqtt.h"

bool netMqtt_begin() {
    return true;
}

void netMqtt_loop() {
}

bool netMqtt_isConnected() {
    return false;
}

bool netMqtt_publish(const char* topic, const char* payload, bool retain) {
    return false;
}

void netMqtt_publishSub(const char* topic, const char* payload) {
}
