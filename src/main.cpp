#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Wire.h>
#include <cstring>

#include "config.h"
#include "pins.h"
#include "net_wifi.h"
#include "net_mqtt.h"
#include "time_rtc.h"
#include "store_sd.h"
#include "watchdog.h"
#include "status_led.h"
#include "sensor_leak.h"
#include "sensor_flow_stub.h"
#include "sensor_pressure_stub.h"
#include "sensor_temp_stub.h"
#include "actuator_solenoid_stub.h"

#ifndef NODE_FIRMWARE_VERSION
#define NODE_FIRMWARE_VERSION "0.0.0"
#endif

static unsigned long lastHeartbeatMs = 0;

static const char* leakStateText() {
    return sensorLeak_isWet() ? "WET" : "DRY";
}

static bool serializeDoc(JsonDocument& doc, char* out, size_t outLen) {
    size_t written = serializeJson(doc, out, outLen);
    return written > 0 && written < outLen;
}

static bool publishM0Status(bool retain, bool bufferIfOffline) {
    JsonDocument doc;
    doc["node_id"] = NODE_ID;
    doc["firmware"] = NODE_FIRMWARE_VERSION;
    doc["leak"] = sensorLeak_isWet();
    doc["leak_state"] = leakStateText();
    doc["uptime_ms"] = millis();
    doc["wifi_rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
    doc["ts"] = timeRtc_getUnixTime();

    char payload[320];
    if (!serializeDoc(doc, payload, sizeof(payload))) {
        Serial.println("[M0] status JSON too large");
        return false;
    }

    if (netMqtt_isConnected()) {
        bool ok = netMqtt_publish(TOPIC_STATUS, payload, retain);
        if (ok) {
            Serial.print("[M0] status ");
            Serial.println(payload);
        }
        return ok;
    }

    if (bufferIfOffline) {
        netMqtt_publishSub(TOPIC_STATUS, payload);
    }
    return false;
}

static void serviceSerialConsole() {
    static bool wasConnected = false;
    static char line[128];
    static size_t lineLen = 0;
    static bool lastWasLineEnd = false;
    bool connected = Serial;

    if (connected && !wasConnected) {
        Serial.println();
        Serial.println("[SERIAL] console connected");
        Serial.print("[SERIAL] leak state: ");
        Serial.println(leakStateText());
        Serial.println("[SERIAL] commands: sdls [path], sdcat <path>, sdrm <path>, sdinfo, sdprune");
    }

    wasConnected = connected;

    while (Serial.available() > 0) {
        char c = (char)Serial.read();

        if (c == '\r' || c == '\n') {
            if (lastWasLineEnd && lineLen == 0) {
                continue;
            }
            lastWasLineEnd = true;
            line[lineLen] = '\0';

            if (lineLen > 0) {
                char* cmd = line;
                while (*cmd == ' ' || *cmd == '\t') {
                    ++cmd;
                }
                if (strncmp(cmd, "!:", 2) == 0) {
                    cmd += 2;
                }

                if (strcmp(cmd, "sdls") == 0) {
                    storeSd_printDirectory("/", Serial);
                } else if (strncmp(cmd, "sdls ", 5) == 0) {
                    storeSd_printDirectory(cmd + 5, Serial);
                } else if (strncmp(cmd, "sdcat ", 6) == 0) {
                    storeSd_printFile(cmd + 6, Serial);
                } else if (strncmp(cmd, "sdrm ", 5) == 0) {
                    storeSd_removePath(cmd + 5, Serial);
                } else if (strcmp(cmd, "sdinfo") == 0) {
                    storeSd_printInfo(Serial);
                } else if (strcmp(cmd, "sdprune") == 0) {
                    storeSd_pruneLogs(Serial);
                } else if (strcmp(cmd, "help") == 0) {
                    Serial.println("[SERIAL] commands: sdls [path], sdcat <path>, sdrm <path>, sdinfo, sdprune");
                } else {
                    Serial.print("[SERIAL] unknown command: ");
                    Serial.println(cmd);
                }
            }

            lineLen = 0;
            continue;
        }

        if (lineLen + 1 < sizeof(line)) {
            line[lineLen++] = c;
            lastWasLineEnd = false;
        }
    }
}

static bool publishLeakAlert() {
    JsonDocument doc;
    doc["type"] = "LEAK_STATE";
    doc["node_id"] = NODE_ID;
    doc["leak"] = sensorLeak_isWet();
    doc["leak_state"] = leakStateText();
    doc["uptime_ms"] = millis();
    doc["ts"] = timeRtc_getUnixTime();

    char payload[256];
    if (!serializeDoc(doc, payload, sizeof(payload))) {
        Serial.println("[M0] alert JSON too large");
        return false;
    }

    netMqtt_publishSub(TOPIC_ALERT, payload);
    return true;
}

static bool publishHaDiscovery() {
    JsonDocument doc;
    doc["name"] = "TWWP " NODE_ID " Leak";
    doc["unique_id"] = "twwp_" NODE_ID "_leak";
    doc["object_id"] = "twwp_" NODE_ID "_leak";
    doc["device_class"] = "moisture";
    doc["state_topic"] = TOPIC_STATUS;
    doc["value_template"] = "{{ 'ON' if value_json.leak else 'OFF' }}";
    doc["availability_topic"] = TOPIC_LWT;
    doc["payload_available"] = "online";
    doc["payload_not_available"] = "offline";

    JsonObject device = doc["device"].to<JsonObject>();
    JsonArray identifiers = device["identifiers"].to<JsonArray>();
    identifiers.add("twwp_" NODE_ID);
    device["name"] = "TWWP " NODE_ID;
    device["manufacturer"] = "TWWP";
    device["model"] = "Waveshare ESP32-S3 RS485 CAN";
    device["sw_version"] = NODE_FIRMWARE_VERSION;

    char payload[768];
    if (!serializeDoc(doc, payload, sizeof(payload))) {
        Serial.println("[MQTT] HA discovery JSON too large");
        return false;
    }

    const char* topic = "homeassistant/binary_sensor/twwp_" NODE_ID "_leak/config";
    bool ok = netMqtt_publish(topic, payload, true);
    Serial.print("[MQTT] HA discovery ");
    Serial.println(ok ? "published" : "failed");
    return ok;
}

static void publishOnlineState() {
    if (!netMqtt_isConnected()) {
        return;
    }

    netMqtt_publish(TOPIC_LWT, "online", true);
    publishHaDiscovery();
    publishM0Status(true, false);
    lastHeartbeatMs = millis();
}

static void handleLeakTransition() {
    if (!sensorLeak_hasChanged()) {
        return;
    }

    char msg[48];
    snprintf(msg, sizeof(msg), "[LEAK] state change -> %s", leakStateText());
    Serial.println(msg);
    storeSd_logEvent(msg);

    publishLeakAlert();
    publishM0Status(true, true);
}

static void handleHeartbeat() {
    unsigned long now = millis();
    if (now - lastHeartbeatMs < HEARTBEAT_INTERVAL_MS) {
        return;
    }

    lastHeartbeatMs = now;
    publishM0Status(true, false);
}

static void updateM0Led() {
    if (sensorLeak_isWet()) {
        statusLed_setState(LedState::LEAK_DETECTED);
    } else if (netMqtt_isConnected()) {
        statusLed_setState(LedState::ONLINE);
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("[BOOT] TWWP Sensor Node starting");
    Serial.print("[BOOT] firmware ");
    Serial.println(NODE_FIRMWARE_VERSION);

    statusLed_begin();
    statusLed_setState(LedState::BOOTING);

    Wire.setPins(PIN_I2C_SDA, PIN_I2C_SCL);

    storeSd_begin();
    timeRtc_begin();

    sensorLeak_begin();
    sensorFlow_begin();
    sensorPressure_begin();
    sensorTemp_begin();
    actuatorSolenoid_begin();

    netWifi_begin();
    watchdog_begin();
    netMqtt_begin();

    Serial.print("[LEAK] initial state: ");
    Serial.println(leakStateText());
}

void loop() {
    watchdog_feed();
    serviceSerialConsole();

    storeSd_loop();
    timeRtc_loop();
    statusLed_loop();

    sensorLeak_loop();
    sensorFlow_loop();
    sensorPressure_loop();
    sensorTemp_loop();
    actuatorSolenoid_loop();

    netWifi_loop();
    netMqtt_loop();

    if (netMqtt_takeJustConnected()) {
        publishOnlineState();
    }

    handleLeakTransition();
    handleHeartbeat();
    updateM0Led();
}
