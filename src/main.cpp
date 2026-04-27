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
#include "sensor_flow.h"
#include "session_flow.h"
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
    bool wifiUp = (WiFi.status() == WL_CONNECTED);
    int rssi = wifiUp ? WiFi.RSSI() : 0;
    doc["wifi_rssi"]       = rssi;
    doc["wifi_signal_pct"] = wifiUp ? max(0, min(100, 2 * (rssi + 100))) : 0;
    doc["wifi_ssid"]       = wifiUp ? WiFi.SSID()            : "";
    doc["wifi_bssid"]      = wifiUp ? WiFi.BSSIDstr()        : "";
    doc["ip"]              = wifiUp ? WiFi.localIP().toString() : "";
    doc["wifi_status"]     = wifiUp ? "Connected" : "Disconnected";
    doc["uptime_s"]        = (uint32_t)(millis() / 1000UL);
    doc["mqtt_buffer_count"] = storeSd_bufferCount();
    doc["ts"] = timeRtc_getUnixTime();

    doc["flow_rate_1"]  = sensorFlow_getRateLpm(1);
    doc["flow_rate_2"]  = sensorFlow_getRateLpm(2);
    doc["flow_total_1"] = sensorFlow_getTotalL(1);
    doc["flow_total_2"] = sensorFlow_getTotalL(2);
    doc["flow_today_1"] = sensorFlow_getTodayL(1);
    doc["flow_today_2"] = sensorFlow_getTodayL(2);
    doc["flow_week_1"]  = sensorFlow_getWeekL(1);
    doc["flow_week_2"]  = sensorFlow_getWeekL(2);
    doc["flow_month_1"] = sensorFlow_getMonthL(1);
    doc["flow_month_2"] = sensorFlow_getMonthL(2);
    doc["flow_year_1"]  = sensorFlow_getYearL(1);
    doc["flow_year_2"]  = sensorFlow_getYearL(2);
    doc["k_factor_1"]   = sensorFlow_getKFactor(1);
    doc["k_factor_2"]   = sensorFlow_getKFactor(2);
    doc["session_last_id"]       = sessionFlow_getLastId();
    doc["session_last_start_ts"] = sessionFlow_getLastStartTs();
    doc["session_last_end_ts"]   = sessionFlow_getLastEndTs();
    doc["session_last_dur_s"]    = sessionFlow_getLastDurationS();
    doc["session_last_vol_out"]  = sessionFlow_getLastVolumeOut();
    doc["session_last_vol_in"]   = sessionFlow_getLastVolumeIn();

    char payload[1024];
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

static void fillHaDevice(JsonDocument& doc) {
    JsonObject device = doc["device"].to<JsonObject>();
    JsonArray identifiers = device["identifiers"].to<JsonArray>();
    identifiers.add("twwp_" NODE_ID);
    device["name"] = "TWWP " NODE_ID;
    device["manufacturer"] = "TWWP";
    device["model"] = "Waveshare ESP32-S3 RS485 CAN";
    device["sw_version"] = NODE_FIRMWARE_VERSION;
}

static void fillHaSubDevice(JsonDocument& doc, const char* subId, const char* subName) {
    JsonObject device = doc["device"].to<JsonObject>();
    JsonArray identifiers = device["identifiers"].to<JsonArray>();
    identifiers.add(subId);
    device["name"]         = subName;
    device["manufacturer"] = "TWWP";
    device["model"]        = "Waveshare ESP32-S3 RS485 CAN";
    device["sw_version"]   = NODE_FIRMWARE_VERSION;
    device["via_device"]   = "twwp_" NODE_ID;
}

static bool publishHaFlowSensor(const char* uid, const char* name, const char* valueKey,
                                 const char* unit, const char* deviceClass,
                                 const char* stateClass,
                                 const char* subId, const char* subName) {
    JsonDocument doc;
    doc["name"]       = name;
    doc["unique_id"]  = uid;
    doc["object_id"]  = uid;
    doc["state_topic"] = TOPIC_STATUS;
    char tmpl[64];
    snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", valueKey);
    doc["value_template"]      = tmpl;
    doc["unit_of_measurement"] = unit;
    if (deviceClass && deviceClass[0]) {
        doc["device_class"] = deviceClass;
    }
    doc["state_class"]          = stateClass;
    doc["availability_topic"]   = TOPIC_LWT;
    doc["payload_available"]    = "online";
    doc["payload_not_available"] = "offline";
    fillHaSubDevice(doc, subId, subName);

    char payload[768];
    if (!serializeDoc(doc, payload, sizeof(payload))) {
        Serial.println("[MQTT] flow HA discovery JSON too large");
        return false;
    }
    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/config", uid);
    return netMqtt_publish(topic, payload, true);
}

static bool publishHaDiscoveryFlow() {
    bool ok = true;

    // Channel 1 — RO Output
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_rate_1",  "Flow Rate",   "flow_rate_1",  "L/min", "volume_flow_rate", "measurement",    "twwp_" NODE_ID "_flow1", "RO Output");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_total_1", "Flow Total",  "flow_total_1", "L",     "water",            "total_increasing","twwp_" NODE_ID "_flow1", "RO Output");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_today_1", "Flow Today",  "flow_today_1", "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow1", "RO Output");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_week_1",  "Flow Week",   "flow_week_1",  "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow1", "RO Output");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_month_1", "Flow Month",  "flow_month_1", "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow1", "RO Output");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_year_1",  "Flow Year",   "flow_year_1",  "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow1", "RO Output");

    // Channel 2 — RO Input
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_rate_2",  "Flow Rate",   "flow_rate_2",  "L/min", "volume_flow_rate", "measurement",    "twwp_" NODE_ID "_flow2", "RO Input");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_total_2", "Flow Total",  "flow_total_2", "L",     "water",            "total_increasing","twwp_" NODE_ID "_flow2", "RO Input");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_today_2", "Flow Today",  "flow_today_2", "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow2", "RO Input");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_week_2",  "Flow Week",   "flow_week_2",  "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow2", "RO Input");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_month_2", "Flow Month",  "flow_month_2", "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow2", "RO Input");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_year_2",  "Flow Year",   "flow_year_2",  "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow2", "RO Input");

    // Clear any old sensor discovery entries for K factors (published before number entities existed)
    netMqtt_publish("homeassistant/sensor/twwp_" NODE_ID "_k_factor_1/config", "", true);
    netMqtt_publish("homeassistant/sensor/twwp_" NODE_ID "_k_factor_2/config", "", true);

    // K factor writable number entities
    auto publishKFactorNumber = [&ok](const char* uid, const char* name,
                                      const char* valueKey, const char* cmdKey,
                                      const char* subId, const char* subName) {
        JsonDocument doc;
        doc["name"]       = name;
        doc["unique_id"]  = uid;
        doc["object_id"]  = uid;
        doc["state_topic"] = TOPIC_STATUS;
        char tmpl[64];
        snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s | int }}", valueKey);
        doc["value_template"]      = tmpl;
        doc["command_topic"]       = TOPIC_CMD;
        char cmdTmpl[64];
        snprintf(cmdTmpl, sizeof(cmdTmpl), "{\"%s\": {{ value | int }}}", cmdKey);
        doc["command_template"]    = cmdTmpl;
        doc["unit_of_measurement"] = "pulses/L";
        doc["min"]                 = 1;
        doc["max"]                 = 9999;
        doc["step"]                = 1;
        doc["mode"]                = "box";
        doc["entity_category"]     = "config";
        doc["availability_topic"]   = TOPIC_LWT;
        doc["payload_available"]    = "online";
        doc["payload_not_available"] = "offline";
        fillHaSubDevice(doc, subId, subName);

        char payload[768];
        if (!serializeDoc(doc, payload, sizeof(payload))) {
            Serial.println("[MQTT] K factor number JSON too large");
            ok = false;
            return;
        }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/number/%s/config", uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    };

    publishKFactorNumber("twwp_" NODE_ID "_k_factor_1", "K Factor",
                         "k_factor_1", "set_k_factor_1",
                         "twwp_" NODE_ID "_flow1", "RO Output");
    publishKFactorNumber("twwp_" NODE_ID "_k_factor_2", "K Factor",
                         "k_factor_2", "set_k_factor_2",
                         "twwp_" NODE_ID "_flow2", "RO Input");

    Serial.print("[MQTT] HA flow discovery ");
    Serial.println(ok ? "published" : "partial");
    return ok;
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

    fillHaSubDevice(doc, "twwp_" NODE_ID "_leak", "Leak Sensor");

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

static bool publishHaDiagSensor(const char* uid, const char* name, const char* valueKey,
                                  const char* unit, const char* deviceClass,
                                  const char* stateClass) {
    JsonDocument doc;
    doc["name"]              = name;
    doc["unique_id"]         = uid;
    doc["object_id"]         = uid;
    doc["entity_category"]   = "diagnostic";
    doc["state_topic"]       = TOPIC_STATUS;
    char tmpl[64];
    snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", valueKey);
    doc["value_template"]      = tmpl;
    if (unit && unit[0])        doc["unit_of_measurement"] = unit;
    if (deviceClass && deviceClass[0]) doc["device_class"] = deviceClass;
    if (stateClass && stateClass[0])   doc["state_class"]  = stateClass;
    doc["availability_topic"]   = TOPIC_LWT;
    doc["payload_available"]    = "online";
    doc["payload_not_available"] = "offline";
    fillHaDevice(doc);

    char payload[768];
    if (!serializeDoc(doc, payload, sizeof(payload))) {
        Serial.println("[MQTT] diag sensor JSON too large");
        return false;
    }
    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/config", uid);
    return netMqtt_publish(topic, payload, true);
}

static bool publishHaDiscoverySession() {
    bool ok = true;

    auto pub = [&ok](const char* uid, const char* name, const char* valueKey,
                     const char* unit, const char* stateClass) {
        JsonDocument doc;
        doc["name"]             = name;
        doc["unique_id"]        = uid;
        doc["object_id"]        = uid;
        doc["entity_category"]  = "diagnostic";
        doc["state_topic"]      = TOPIC_STATUS;
        char tmpl[80];
        snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", valueKey);
        doc["value_template"]      = tmpl;
        if (unit && unit[0])        doc["unit_of_measurement"] = unit;
        if (stateClass && stateClass[0]) doc["state_class"] = stateClass;
        doc["availability_topic"]   = TOPIC_LWT;
        doc["payload_available"]    = "online";
        doc["payload_not_available"] = "offline";
        fillHaDevice(doc);
        char payload[512];
        if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; return; }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/config", uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    };

    pub("twwp_" NODE_ID "_session_last_id",      "Last Session ID",          "session_last_id",       "",  "");
    pub("twwp_" NODE_ID "_session_last_dur_s",   "Last Session Duration",    "session_last_dur_s",    "s", "measurement");
    pub("twwp_" NODE_ID "_session_last_vol_out", "Last Session Volume Out",  "session_last_vol_out",  "L", "measurement");
    pub("twwp_" NODE_ID "_session_last_vol_in",  "Last Session Volume In",   "session_last_vol_in",   "L", "measurement");

    Serial.print("[MQTT] HA session discovery ");
    Serial.println(ok ? "published" : "partial");
    return ok;
}

static bool publishHaDiscoveryDiagnostics() {
    bool ok = true;

    // WiFi connectivity binary sensor
    {
        JsonDocument doc;
        doc["name"]               = "TWWP " NODE_ID " WiFi Status";
        doc["unique_id"]          = "twwp_" NODE_ID "_wifi_status";
        doc["object_id"]          = "twwp_" NODE_ID "_wifi_status";
        doc["entity_category"]    = "diagnostic";
        doc["device_class"]       = "connectivity";
        doc["state_topic"]        = TOPIC_STATUS;
        doc["value_template"]     = "{{ value_json.wifi_status }}";
        doc["payload_on"]         = "Connected";
        doc["payload_off"]        = "Disconnected";
        doc["availability_topic"]  = TOPIC_LWT;
        doc["payload_available"]   = "online";
        doc["payload_not_available"] = "offline";
        fillHaDevice(doc);
        char payload[768];
        if (serializeDoc(doc, payload, sizeof(payload))) {
            ok &= netMqtt_publish("homeassistant/binary_sensor/twwp_" NODE_ID "_wifi_status/config", payload, true);
        }
    }

    // Restart WiFi button
    {
        JsonDocument doc;
        doc["name"]               = "TWWP " NODE_ID " Restart WiFi";
        doc["unique_id"]          = "twwp_" NODE_ID "_restart_wifi";
        doc["object_id"]          = "twwp_" NODE_ID "_restart_wifi";
        doc["entity_category"]    = "config";
        doc["command_topic"]      = TOPIC_CMD;
        doc["payload_press"]      = "{\"restart_wifi\": true}";
        doc["availability_topic"]  = TOPIC_LWT;
        doc["payload_available"]   = "online";
        doc["payload_not_available"] = "offline";
        fillHaDevice(doc);
        char payload[768];
        if (serializeDoc(doc, payload, sizeof(payload))) {
            ok &= netMqtt_publish("homeassistant/button/twwp_" NODE_ID "_restart_wifi/config", payload, true);
        }
    }

    // Sensor entities
    ok &= publishHaDiagSensor("twwp_" NODE_ID "_wifi_ssid",       "TWWP " NODE_ID " WiFi SSID",         "wifi_ssid",       "",     "",               "");
    ok &= publishHaDiagSensor("twwp_" NODE_ID "_wifi_bssid",      "TWWP " NODE_ID " WiFi BSSID",        "wifi_bssid",      "",     "",               "");
    ok &= publishHaDiagSensor("twwp_" NODE_ID "_ip",              "TWWP " NODE_ID " IP Address",        "ip",              "",     "",               "");
    ok &= publishHaDiagSensor("twwp_" NODE_ID "_wifi_rssi",       "TWWP " NODE_ID " WiFi Signal dB",    "wifi_rssi",       "dBm",  "signal_strength", "measurement");
    ok &= publishHaDiagSensor("twwp_" NODE_ID "_wifi_signal_pct", "TWWP " NODE_ID " WiFi Signal",       "wifi_signal_pct", "%",    "",               "measurement");
    ok &= publishHaDiagSensor("twwp_" NODE_ID "_uptime_s",        "TWWP " NODE_ID " Running Time",      "uptime_s",        "s",    "duration",       "measurement");

    Serial.print("[MQTT] HA diagnostics discovery ");
    Serial.println(ok ? "published" : "partial");
    return ok;
}

static void publishOnlineState() {
    if (!netMqtt_isConnected()) {
        return;
    }

    netMqtt_publish(TOPIC_LWT, "online", true);
    publishHaDiscovery();
    publishHaDiscoveryFlow();
    publishHaDiscoveryDiagnostics();
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

static void handleResetCredsButton() {
    static unsigned long pressedAt = 0;
    static bool wasPressed = false;

    bool pressed = (digitalRead(PIN_RESET_CREDS) == LOW);

    if (pressed && !wasPressed) {
        pressedAt = millis();
        wasPressed = true;
    } else if (!pressed) {
        wasPressed = false;
    } else if (pressed && (millis() - pressedAt >= RESET_CREDS_HOLD_MS)) {
        Serial.println("[BOOT] reset-creds gesture — clearing WiFi credentials");
        storeSd_logEvent("[BOOT] reset-creds gesture — clearing WiFi credentials");
        netWifi_resetCredentials();
    }
}

static void handleCmd(const char* payload) {
    JsonDocument doc;
    if (deserializeJson(doc, payload)) {
        Serial.println("[CMD] invalid JSON");
        return;
    }

    if (!doc["set_k_factor_1"].isNull()) {
        sensorFlow_setKFactor(1, doc["set_k_factor_1"].as<float>());
    }
    if (!doc["set_k_factor_2"].isNull()) {
        sensorFlow_setKFactor(2, doc["set_k_factor_2"].as<float>());
    }
    if (doc["restart_wifi"] | false) {
        storeSd_logEvent("[CMD] restart_wifi received");
        netWifi_reconnect();
    }
}

static const char* DATA_LOG_HEADER =
    "ts,"
    "flow_rate_1,flow_total_1,flow_today_1,"
    "flow_rate_2,flow_total_2,flow_today_2,"
    "leak";

static void handleDataLog() {
    static unsigned long lastDataLogMs = 0;
    unsigned long now = millis();
    if (now - lastDataLogMs < DATA_LOG_INTERVAL_MS) {
        return;
    }
    lastDataLogMs = now;

    char row[128];
    snprintf(row, sizeof(row),
        "%lu,"
        "%.3f,%.3f,%.3f,"
        "%.3f,%.3f,%.3f,"
        "%d",
        (unsigned long)timeRtc_getUnixTime(),
        sensorFlow_getRateLpm(1), sensorFlow_getTotalL(1), sensorFlow_getTodayL(1),
        sensorFlow_getRateLpm(2), sensorFlow_getTotalL(2), sensorFlow_getTodayL(2),
        sensorLeak_isWet() ? 1 : 0);

    storeSd_logDataRow(row, DATA_LOG_HEADER);
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

    pinMode(PIN_RESET_CREDS, INPUT_PULLUP);
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
    netMqtt_setCmdCallback(handleCmd);

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
    handleDataLog();
    handleResetCredsButton();
    updateM0Led();
}
