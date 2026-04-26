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
static bool mqtt_justConnected = false;

static bool mqttConnect(const String& clientId) {
    watchdog_feed();

    if (!secureClient.connected()) {
        secureClient.stop();
        if (!secureClient.connect(MQTT_HOST, MQTT_PORT, 5000)) {
            return false;
        }
    }

    watchdog_feed();
    bool ok = mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS, TOPIC_LWT, 1, true, "offline");
    watchdog_feed();
    return ok;
}

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
    secureClient.setHandshakeTimeout(5);

    // Configure PubSubClient server
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setKeepAlive(15);
    mqttClient.setSocketTimeout(3);
    mqttClient.setBufferSize(1024);

    mqtt_lastAttempt = 0;
    mqtt_backoffMs = 1000;
    return true;
}

void netMqtt_loop() {
    if (mqttClient.connected()) {
        if (!mqttClient.loop()) {
            Serial.println("[MQTT] connection lost during loop, reconnecting");
            mqttClient.disconnect();
        }
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

    bool ok = mqttConnect(clientId);
    if (ok) {
        Serial.println("[MQTT] connected");
        mqtt_justConnected = true;
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

bool netMqtt_takeJustConnected() {
    bool justConnected = mqtt_justConnected;
    mqtt_justConnected = false;
    return justConnected;
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
