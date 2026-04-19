#include "net_mqtt.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#include "config.h"
#include "store_sd.h"
#include "status_led.h"
#include "watchdog.h"

static WiFiClientSecure secureClient;
static PubSubClient mqttClient(secureClient);

static unsigned long mqtt_lastAttempt = 0;
static unsigned long mqtt_backoffMs = 1000; // start 1s
static const unsigned long mqtt_backoffMaxMs = 60000; // max 60s

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
    // Log received command to Serial and SD
    char msg[256];
    size_t toCopy = (length < sizeof(msg)-1) ? length : sizeof(msg)-1;
    memcpy(msg, payload, toCopy);
    msg[toCopy] = '\0';

    Serial.print("[MQTT] recv ");
    Serial.print(topic);
    Serial.print(" -> ");
    Serial.println(msg);

    char logbuf[300];
    snprintf(logbuf, sizeof(logbuf), "[MQTT] recv %s %s", topic, msg);
    storeSd_logEvent(logbuf);
}

bool netMqtt_begin() {
    Serial.println("[MQTT] init");

    // Configure TLS CA
    secureClient.setCACert(MQTT_CA_CERT);

    // Configure PubSubClient server
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);

    // Try a short connect window (non-blocking >10s). Keep this under watchdog rules.
    statusLed_setState(LedState::MQTT_CONNECTING);
    String clientId = String("twwp_") + String(NODE_ID);

    unsigned long start = millis();
    bool connected = false;
    while (millis() - start < 3000) {
        watchdog_feed();
        if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS, TOPIC_LWT, 1, true, "offline")) {
            connected = true;
            break;
        }
        delay(200);
    }

    if (!connected) {
        Serial.println("[MQTT] initial connect failed");
        char tlsMsg[64] = "";
        secureClient.lastError(tlsMsg, sizeof(tlsMsg));
        Serial.printf("[MQTT] TLS error: %s\n", tlsMsg);
        char buf[96];
        snprintf(buf, sizeof(buf), "[MQTT] TLS error: %s", tlsMsg);
        storeSd_logEvent(buf);
        statusLed_setState(LedState::WIFI_CONNECTING);
        return false;
    }

    // Subscriptions + post-connect tasks
    mqttClient.subscribe(TOPIC_CMD);
    statusLed_setState(LedState::ONLINE);
    // Drain a few buffered messages
    storeSd_drainBuffer(10);
    mqtt_backoffMs = 1000;
    return true;
}

void netMqtt_loop() {
    if (mqttClient.connected()) {
        mqttClient.loop();
        return;
    }

    unsigned long now = millis();
    if (now - mqtt_lastAttempt < mqtt_backoffMs) return;
    mqtt_lastAttempt = now;

    statusLed_setState(LedState::MQTT_CONNECTING);
    String clientId = String("twwp_") + String(NODE_ID);

    Serial.print("[MQTT] attempting connect to ");
    Serial.print(MQTT_HOST);
    Serial.print(":");
    Serial.println(MQTT_PORT);

    bool ok = mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS, TOPIC_LWT, 1, true, "offline");
    if (ok) {
        Serial.println("[MQTT] connected");
        mqttClient.subscribe(TOPIC_CMD);
        statusLed_setState(LedState::ONLINE);
        // Drain buffered messages
        storeSd_drainBuffer(20);
        mqtt_backoffMs = 1000; // reset backoff
    } else {
        int state = mqttClient.state();
        Serial.printf("[MQTT] connect failed, state=%d\n", state);

        char tlsMsg[64] = "";
        secureClient.lastError(tlsMsg, sizeof(tlsMsg));
        Serial.printf("[MQTT] TLS error: %s\n", tlsMsg);
        char buf[128];
        snprintf(buf, sizeof(buf), "[MQTT] connect failed state=%d tls=%s", state, tlsMsg);
        storeSd_logEvent(buf);

        // Exponential backoff
        mqtt_backoffMs = mqtt_backoffMs * 2;
        if (mqtt_backoffMs > mqtt_backoffMaxMs) mqtt_backoffMs = mqtt_backoffMaxMs;
    }
}

bool netMqtt_isConnected() {
    return mqttClient.connected();
}

bool netMqtt_publish(const char* topic, const char* payload, bool retain) {
    if (!mqttClient.connected()) return false;
    bool ok = mqttClient.publish(topic, payload, retain);
    if (!ok) {
        Serial.print("[MQTT] publish failed: ");
        Serial.println(topic);
    }
    return ok;
}

void netMqtt_publishSub(const char* topic, const char* payload) {
    // Buffer to SD if not connected, otherwise publish
    if (mqttClient.connected()) {
        if (!netMqtt_publish(topic, payload, false)) {
            // If publish fails, buffer to SD
            storeSd_bufferMessage(topic, payload);
        }
    } else {
        storeSd_bufferMessage(topic, payload);
    }
}

