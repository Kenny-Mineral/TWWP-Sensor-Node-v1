#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Wire.h>
#include <cstring>

#include "config.h"
#include "pins.h"
#include "net_wifi.h"
#include "net_ap.h"
#include "net_mqtt.h"
#include "net_ota.h"
#include "time_rtc.h"
#include "store_sd.h"
#include "watchdog.h"
#include "status_led.h"
#include "sensor_leak.h"
#include "sensor_flow.h"
#include "session_flow.h"
#include "sensor_voltage.h"
#include "rs485_mux.h"
#include "sensor_tds_meter.h"
#include "sensor_yieryi.h"
#include "wq_config.h"
#include "sensor_pressure_stub.h"
#include "sensor_temp_stub.h"
#include "actuator_valve.h"
#include "display_oled.h"

#ifndef NODE_FIRMWARE_VERSION
#define NODE_FIRMWARE_VERSION "0.0.0"
#endif

static unsigned long lastHeartbeatMs = 0;
static uint32_t      s_restartMs     = 0;  // non-zero = deferred restart pending

static const char* leakStateText() {
    return sensorLeak_isWet() ? "WET" : "DRY";
}

static bool serializeDoc(JsonDocument& doc, char* out, size_t outLen) {
    size_t written = serializeJson(doc, out, outLen);
    return written > 0 && written < outLen;
}

static void setJsonFloatOrNull(JsonDocument& doc, const char* key, bool valid, float value, uint8_t decimals) {
    if (!valid) {
        doc[key] = nullptr;
        return;
    }
    doc[key] = serialized(String(value, static_cast<unsigned int>(decimals)));
}

static void setJsonIntOrNull(JsonDocument& doc, const char* key, bool valid, int value) {
    if (!valid) {
        doc[key] = nullptr;
        return;
    }
    doc[key] = value;
}

static void addWaterQualityStatus(JsonDocument& doc, uint8_t zone, const char* prefix) {
    char key[40];

    snprintf(key, sizeof(key), "wq_%s_ph", prefix);
    setJsonFloatOrNull(doc, key, sensorYieryi_hasPh(zone), sensorYieryi_getPh(zone), 2);

    snprintf(key, sizeof(key), "wq_%s_orp", prefix);
    setJsonIntOrNull(doc, key, sensorYieryi_hasOrp(zone), sensorYieryi_getOrpMv(zone));

    snprintf(key, sizeof(key), "wq_%s_ec", prefix);
    setJsonFloatOrNull(doc, key, sensorYieryi_hasEc(zone), sensorYieryi_getEcUsCm(zone), 0);

    snprintf(key, sizeof(key), "wq_%s_tds_ppm", prefix);
    setJsonFloatOrNull(doc, key, sensorYieryi_hasEc(zone), sensorYieryi_getTdsPpm(zone), 1);

    snprintf(key, sizeof(key), "wq_%s_temp", prefix);
    setJsonFloatOrNull(doc, key, sensorYieryi_hasTemp(zone), sensorYieryi_getTempC(zone), 1);

    snprintf(key, sizeof(key), "wq_%s_humidity", prefix);
    setJsonIntOrNull(doc, key, sensorYieryi_hasEc(zone), sensorYieryi_getHumidityPct(zone));

    snprintf(key, sizeof(key), "wq_%s_online", prefix);
    doc[key] = sensorYieryi_isOnline(zone);

    snprintf(key, sizeof(key), "wq_%s_fail_count", prefix);
    doc[key] = sensorYieryi_getFailCount(zone);

    snprintf(key, sizeof(key), "wq_%s_last_error", prefix);
    doc[key] = sensorYieryi_getLastError(zone);

    snprintf(key, sizeof(key), "wq_%s_raw_hex", prefix);
    doc[key] = sensorYieryi_getRawHex(zone);

    snprintf(key, sizeof(key), "wq_%s_ph_cal_date", prefix);
    doc[key] = sensorYieryi_getPhCalDate(zone);
    snprintf(key, sizeof(key), "wq_%s_orp_cal_date", prefix);
    doc[key] = sensorYieryi_getOrpCalDate(zone);
    snprintf(key, sizeof(key), "wq_%s_ec_cal_date", prefix);
    doc[key] = sensorYieryi_getEcCalDate(zone);
}

static void addTdsMeterStatus(JsonDocument& doc, uint8_t zone, const char* prefix) {
    char key[48];
    bool online = sensorTdsMeter_isOnline(zone);

    snprintf(key, sizeof(key), "tds_%s_online", prefix);
    doc[key] = online;

    snprintf(key, sizeof(key), "tds_%s_ec", prefix);
    setJsonFloatOrNull(doc, key, online, sensorTdsMeter_getEc(zone), 0);

    snprintf(key, sizeof(key), "tds_%s_temp", prefix);
    setJsonFloatOrNull(doc, key, online, sensorTdsMeter_getTemp(zone), 1);

    snprintf(key, sizeof(key), "tds_%s_ppm", prefix);
    setJsonFloatOrNull(doc, key, online, sensorTdsMeter_getTds(zone), 0);

    snprintf(key, sizeof(key), "tds_%s_fail_count", prefix);
    doc[key] = sensorTdsMeter_getFailCount(zone);

    snprintf(key, sizeof(key), "tds_%s_last_error", prefix);
    doc[key] = sensorTdsMeter_getLastError(zone);

    snprintf(key, sizeof(key), "tds_%s_ec_raw", prefix);
    setJsonFloatOrNull(doc, key, online, sensorTdsMeter_getRawEc(zone), 0);

    snprintf(key, sizeof(key), "tds_%s_ec_cal_factor", prefix);
    doc[key] = sensorTdsMeter_getEcCalFactor(zone);

    snprintf(key, sizeof(key), "tds_%s_cal_state", prefix);
    doc[key] = sensorTdsMeter_getCalState(zone);

    snprintf(key, sizeof(key), "tds_%s_cal_suggested_factor", prefix);
    doc[key] = sensorTdsMeter_getCalSuggestedFactor(zone);

    snprintf(key, sizeof(key), "tds_%s_cal_ref_ec", prefix);
    doc[key] = sensorTdsMeter_getCalRefEc(zone);

    snprintf(key, sizeof(key), "tds_%s_cal_date", prefix);
    doc[key] = sensorTdsMeter_getCalDate(zone);
}

static const char* csvFloatOrBlank(char* out, size_t outLen, bool valid, float value, uint8_t decimals) {
    (void)outLen;
    if (!valid) {
        out[0] = '\0';
        return out;
    }
    dtostrf(value, 0, decimals, out);
    return out;
}

static const char* csvIntOrBlank(char* out, size_t outLen, bool valid, int value) {
    if (!valid) {
        out[0] = '\0';
        return out;
    }
    snprintf(out, outLen, "%d", value);
    return out;
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
    doc["wifi_uptime_s"]   = netWifi_getConnectedUptimeS();
    doc["mqtt_buffer_count"] = storeSd_bufferCount();
    doc["ap_active"]       = netAp_isActive();
    doc["ap_ssid"]         = netAp_getSsid();
    doc["ap_clients"]      = netAp_getClientCount();
    doc["ap_expires_s"]    = netAp_getExpiresS();
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

    // Waste:pure ratio — litres wasted per litre purified (lower = better filter)
    // Formula: (raw_input - pure_output) / pure_output
    auto calcWasteRatio = [](float purified, float raw) -> float {
        return purified > 0.0f ? (raw - purified) / purified : 0.0f;
    };
    doc["waste_ratio_today"]  = serialized(String(calcWasteRatio(sensorFlow_getTodayL(1), sensorFlow_getTodayL(2)), 2));
    doc["waste_ratio_week"]   = serialized(String(calcWasteRatio(sensorFlow_getWeekL(1),  sensorFlow_getWeekL(2)),  2));
    doc["waste_ratio_month"]  = serialized(String(calcWasteRatio(sensorFlow_getMonthL(1), sensorFlow_getMonthL(2)), 2));
    doc["waste_ratio_year"]   = serialized(String(calcWasteRatio(sensorFlow_getYearL(1),  sensorFlow_getYearL(2)),  2));

    doc["sensor_model_1"] = sensorFlow_getModel(1);
    doc["sensor_model_2"] = sensorFlow_getModel(2);
    doc["k_factor_1"]   = sensorFlow_getKFactor(1);
    doc["k_factor_2"]   = sensorFlow_getKFactor(2);
    doc["k_applied_1"]  = sensorFlow_getAppliedKFactor(1);
    doc["k_applied_2"]  = sensorFlow_getAppliedKFactor(2);
    doc["pulses_raw_1"] = (unsigned long long)sensorFlow_getTotalPulses(1);
    doc["pulses_raw_2"] = (unsigned long long)sensorFlow_getTotalPulses(2);
    doc["k_table_1"] = serialized(sensorFlow_getKTableJson(1));
    doc["k_table_2"] = serialized(sensorFlow_getKTableJson(2));
    doc["cal_state_1"]            = sensorFlow_getCalState(1);
    doc["cal_state_2"]            = sensorFlow_getCalState(2);
    doc["cal_suggested_k_1"]      = sensorFlow_getCalSuggestedK(1);
    doc["cal_suggested_k_2"]      = sensorFlow_getCalSuggestedK(2);
    doc["cal_pulses_since_start_1"] = (unsigned long long)sensorFlow_getCalPulsesSinceStart(1);
    doc["cal_pulses_since_start_2"] = (unsigned long long)sensorFlow_getCalPulsesSinceStart(2);
    doc["cal_ref_vol_1"]          = sensorFlow_getCalRefVol(1);
    doc["cal_ref_vol_2"]          = sensorFlow_getCalRefVol(2);
    doc["debounce_us_1"] = sensorFlow_getDebounceUs(1);
    doc["debounce_us_2"] = sensorFlow_getDebounceUs(2);
    doc["flow_avg_window"] = sensorFlow_getFlowAvgWindow();
    doc["session_last_id"]         = sessionFlow_getLastId();
    doc["session_last_start_ts"]   = sessionFlow_getLastStartTs();
    doc["session_last_end_ts"]     = sessionFlow_getLastEndTs();
    doc["session_last_dur_s"]      = sessionFlow_getLastDurationS();
    doc["session_last_flow_dur_s"] = sessionFlow_getLastFlowDurationS();
    doc["session_last_idle_s"]     = sessionFlow_getLastIdleTimeS();
    doc["session_last_vol_out"]    = sessionFlow_getLastVolumeOut();
    doc["session_last_vol_in"]     = sessionFlow_getLastVolumeIn();
    doc["session_enabled"]          = sessionFlow_isEnabled();
    doc["session_idle_timeout_s"]   = sessionFlow_getIdleTimeoutS();
    doc["flow_threshold_lpm"]       = sessionFlow_getFlowThreshold();
    doc["leak_suspect_1"]           = sessionFlow_getLeakSuspect(1);
    doc["leak_suspect_2"]           = sessionFlow_getLeakSuspect(2);
    doc["ota_state"]                = (uint8_t)netOta_getState();
    doc["ota_progress_pct"]         = netOta_getProgressPct();
    if (netOta_getError()) {
        doc["ota_error"] = netOta_getError();
    }

    doc["supply_voltage"]         = sensorVoltage_getVoltageV();
    doc["supply_voltage_divider"] = sensorVoltage_getDividerVoltageV();
    doc["supply_voltage_pct"]     = sensorVoltage_getPercentPct();
    doc["supply_voltage_state"]   = sensorVoltage_getState();
    doc["voltage_v_min"]        = sensorVoltage_getVMin();
    doc["voltage_v_max"]        = sensorVoltage_getVMax();
    doc["voltage_cal_factor"]   = sensorVoltage_getCalFactor();

    doc["valve_open"] = actuatorValve_isOpen();
    doc["valve_auto"] = actuatorValve_isAuto();
    doc["valve_type"]                 = actuatorValve_getValveType();
    doc["trigger_source"]             = actuatorValve_getTriggerSource();
    doc["valve_idle_timeout_s"]       = actuatorValve_getIdleTimeoutS();
    doc["valve_max_open_s"]           = actuatorValve_getMaxOpenS();
    doc["valve_timeout_disable_auto"] = actuatorValve_getTimeoutDisableAuto();
    doc["valve_timeout_alert"]        = actuatorValve_getTimeoutAlert();

    addWaterQualityStatus(doc, YIERYI_ZONE_PRE_RO, "pre_ro");
    addWaterQualityStatus(doc, YIERYI_ZONE_POST_RO, "post_ro");
    addWaterQualityStatus(doc, YIERYI_ZONE_REMIN, "remin");

    addTdsMeterStatus(doc, TDS_ZONE_PRE_RO,  "pre_ro");
    addTdsMeterStatus(doc, TDS_ZONE_POST_RO, "post_ro");

    char payload[4096];
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
        Serial.println("[SERIAL] commands: sdls [path], sdcat <path>, sdrm <path>, sdinfo, sdprune, ota <url> [md5], ota_state, wq_status, wq_poll, reset_flow_today[_1/_2], reset_flow_totals[_1/_2]");
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
                } else if (strncmp(cmd, "ota ", 4) == 0) {
                    char* rest = cmd + 4;
                    while (*rest == ' ') {
                        ++rest;
                    }

                    if (*rest == '\0') {
                        Serial.println("[OTA] usage: ota <url> [md5]");
                    } else {
                        char* urlPart = rest;
                        char* space = strchr(urlPart, ' ');
                        char* md5Part = nullptr;
                        if (space) {
                            *space = '\0';
                            md5Part = space + 1;
                            while (*md5Part == ' ') {
                                ++md5Part;
                            }
                            if (*md5Part == '\0') {
                                md5Part = nullptr;
                            }
                        }

                        if (!netOta_beginUpdate(urlPart, md5Part)) {
                            Serial.print("[OTA] failed to start update from serial command: ");
                            Serial.println(netOta_getError() ? netOta_getError() : "unknown");
                        }
                    }
                } else if (strcmp(cmd, "ota_state") == 0) {
                    Serial.printf("[OTA] state=%u progress=%u%% error=%s url=%s\n",
                                  (unsigned)netOta_getState(),
                                  (unsigned)netOta_getProgressPct(),
                                  netOta_getError() ? netOta_getError() : "none",
                                  netOta_getUrl() ? netOta_getUrl() : "none");
                } else if (strcmp(cmd, "wq_status") == 0) {
                    sensorYieryi_printStatus(Serial);
                } else if (strcmp(cmd, "wq_poll") == 0) {
                    sensorYieryi_forcePoll();
                    Serial.println("[YIERYI] poll requested");
                } else if (strcmp(cmd, "help") == 0) {
                    Serial.println("[SERIAL] SD:      sdls [path], sdcat <path>, sdrm <path>, sdinfo, sdprune");
                    Serial.println("[SERIAL] OTA:     ota <url> [md5], ota_state");
                    Serial.println("[SERIAL] WQ:      wq_status, wq_poll");
                    Serial.println("[SERIAL] today:   reset_flow_today[_1/_2]");
                    Serial.println("[SERIAL] week:    reset_flow_week[_1/_2]");
                    Serial.println("[SERIAL] month:   reset_flow_month[_1/_2]");
                    Serial.println("[SERIAL] year:    reset_flow_year[_1/_2]");
                    Serial.println("[SERIAL] totals:  reset_flow_totals[_1/_2]");
                    Serial.println("[SERIAL] nuclear: factory_reset_flow");
                    Serial.println("[SERIAL] session: session_enable, session_disable");
                } else if (strcmp(cmd, "reset_flow_today") == 0)    { sensorFlow_resetToday(0);
                } else if (strcmp(cmd, "reset_flow_today_1") == 0)  { sensorFlow_resetToday(1);
                } else if (strcmp(cmd, "reset_flow_today_2") == 0)  { sensorFlow_resetToday(2);
                } else if (strcmp(cmd, "reset_flow_week") == 0)     { sensorFlow_resetWeek(0);
                } else if (strcmp(cmd, "reset_flow_week_1") == 0)   { sensorFlow_resetWeek(1);
                } else if (strcmp(cmd, "reset_flow_week_2") == 0)   { sensorFlow_resetWeek(2);
                } else if (strcmp(cmd, "reset_flow_month") == 0)    { sensorFlow_resetMonth(0);
                } else if (strcmp(cmd, "reset_flow_month_1") == 0)  { sensorFlow_resetMonth(1);
                } else if (strcmp(cmd, "reset_flow_month_2") == 0)  { sensorFlow_resetMonth(2);
                } else if (strcmp(cmd, "reset_flow_year") == 0)     { sensorFlow_resetYear(0);
                } else if (strcmp(cmd, "reset_flow_year_1") == 0)   { sensorFlow_resetYear(1);
                } else if (strcmp(cmd, "reset_flow_year_2") == 0)   { sensorFlow_resetYear(2);
                } else if (strcmp(cmd, "reset_flow_totals") == 0)   { sensorFlow_resetTotals(0);
                } else if (strcmp(cmd, "reset_flow_totals_1") == 0) { sensorFlow_resetTotals(1);
                } else if (strcmp(cmd, "reset_flow_totals_2") == 0) { sensorFlow_resetTotals(2);
                } else if (strcmp(cmd, "factory_reset_flow") == 0) {
                    sensorFlow_factoryReset();
                    sessionFlow_factoryReset();
                } else if (strcmp(cmd, "session_enable") == 0)  { sessionFlow_setEnabled(true);
                } else if (strcmp(cmd, "session_disable") == 0) { sessionFlow_setEnabled(false);
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
                                 const char* subId, const char* subName,
                                 const char* sensorModel,
                                 const char* entityCategory = nullptr) {
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
    if (entityCategory && entityCategory[0]) {
        doc["entity_category"] = entityCategory;
    }
    doc["availability_topic"]   = TOPIC_LWT;
    doc["payload_available"]    = "online";
    doc["payload_not_available"] = "offline";
    fillHaSubDevice(doc, subId, subName);
    doc["device"]["model"] = sensorModel;

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

    // Channel 1 — RO Output (USN-HS06PE, GPIO4)
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_rate_1",  "Output Flow Rate",  "flow_rate_1",  "L/min", "volume_flow_rate", "measurement",     "twwp_" NODE_ID "_flow1", "RO Output", "USN-HS06PE");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_total_1", "Output Flow Total", "flow_total_1", "L",     "water",            "total_increasing","twwp_" NODE_ID "_flow1", "RO Output", "USN-HS06PE");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_today_1", "Output Today",      "flow_today_1", "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow1", "RO Output", "USN-HS06PE");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_week_1",  "Output This Week",  "flow_week_1",  "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow1", "RO Output", "USN-HS06PE");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_month_1", "Output This Month", "flow_month_1", "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow1", "RO Output", "USN-HS06PE");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_year_1",  "Output This Year",  "flow_year_1",  "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow1", "RO Output", "USN-HS06PE");

    // Channel 2 — RO Input (USN-HS06PS, GPIO5)
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_rate_2",  "Input Flow Rate",  "flow_rate_2",  "L/min", "volume_flow_rate", "measurement",     "twwp_" NODE_ID "_flow2", "RO Input", "USN-HS06PS");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_total_2", "Input Flow Total", "flow_total_2", "L",     "water",            "total_increasing","twwp_" NODE_ID "_flow2", "RO Input", "USN-HS06PS");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_today_2", "Input Today",      "flow_today_2", "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow2", "RO Input", "USN-HS06PS");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_week_2",  "Input This Week",  "flow_week_2",  "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow2", "RO Input", "USN-HS06PS");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_month_2", "Input This Month", "flow_month_2", "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow2", "RO Input", "USN-HS06PS");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_year_2",  "Input This Year",  "flow_year_2",  "L",     "",                 "measurement",     "twwp_" NODE_ID "_flow2", "RO Input", "USN-HS06PS");

    // Diagnostic raw pulse sensors (total_increasing, never reset)
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_pulses_raw_1",  "Output Raw Pulses",  "pulses_raw_1",  "pulses", "", "total_increasing", "twwp_" NODE_ID "_flow1", "RO Output", "USN-HS06PE", "diagnostic");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_pulses_raw_2",  "Input Raw Pulses",   "pulses_raw_2",  "pulses", "", "total_increasing", "twwp_" NODE_ID "_flow2", "RO Input",  "USN-HS06PS", "diagnostic");

    // Diagnostic applied K-factor sensors
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_k_applied_1",   "Output Applied K",   "k_applied_1",   "pulses/L", "", "measurement",      "twwp_" NODE_ID "_flow1", "RO Output", "USN-HS06PE", "diagnostic");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_k_applied_2",   "Input Applied K",    "k_applied_2",   "pulses/L", "", "measurement",      "twwp_" NODE_ID "_flow2", "RO Input",  "USN-HS06PS", "diagnostic");

    // Diagnostic smoothed/averaged flow rate sensors
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_avg_window_1", "Output Smoothed Flow", "flow_avg_window_1", "L/min", "volume_flow_rate", "measurement", "twwp_" NODE_ID "_flow1", "RO Output", "USN-HS06PE", "diagnostic");
    ok &= publishHaFlowSensor("twwp_" NODE_ID "_flow_avg_window_2", "Input Smoothed Flow",  "flow_avg_window_2", "L/min", "volume_flow_rate", "measurement", "twwp_" NODE_ID "_flow2", "RO Input",  "USN-HS06PS", "diagnostic");

    // Clear any old sensor discovery entries for K factors (published before number entities existed)
    netMqtt_publish("homeassistant/sensor/twwp_" NODE_ID "_k_factor_1/config", "", true);
    netMqtt_publish("homeassistant/sensor/twwp_" NODE_ID "_k_factor_2/config", "", true);

    // K factor writable number entities
    auto publishKFactorNumber = [&ok](const char* uid, const char* name,
                                      const char* valueKey, const char* cmdKey,
                                      const char* subId, const char* subName,
                                      const char* sensorModel) {
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
        doc["max"]                 = 99999;
        doc["step"]                = 1;
        doc["mode"]                = "box";
        doc["entity_category"]     = "config";
        doc["availability_topic"]   = TOPIC_LWT;
        doc["payload_available"]    = "online";
        doc["payload_not_available"] = "offline";
        fillHaSubDevice(doc, subId, subName);
        doc["device"]["model"] = sensorModel;

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

    publishKFactorNumber("twwp_" NODE_ID "_k_factor_1", "Output K Factor",
                         "k_factor_1", "set_k_factor_1",
                         "twwp_" NODE_ID "_flow1", "RO Output", "USN-HS06PE");
    publishKFactorNumber("twwp_" NODE_ID "_k_factor_2", "Input K Factor",
                         "k_factor_2", "set_k_factor_2",
                         "twwp_" NODE_ID "_flow2", "RO Input", "USN-HS06PS");

    // --- K-table text entities (JSON array strings) ---
    auto publishKTableText = [&ok](const char* uid, const char* name,
                                    const char* valueKey, const char* cmdKey,
                                    const char* subId, const char* subName,
                                    const char* sensorModel) {
        JsonDocument doc;
        doc["name"]              = name;
        doc["unique_id"]         = uid;
        doc["object_id"]         = uid;
        doc["entity_category"]   = "config";
        doc["state_topic"]       = TOPIC_STATUS;
        char tmpl[80];
        snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s | tojson }}", valueKey);
        doc["value_template"]    = tmpl;
        doc["command_topic"]     = TOPIC_CMD;
        char cmdTmpl[80];
        snprintf(cmdTmpl, sizeof(cmdTmpl), "{\"%s\": {{ value | tojson }}}", cmdKey);
        doc["command_template"]  = cmdTmpl;
        doc["icon"]              = "mdi:code-json";
        doc["mode"]              = "text";
        doc["availability_topic"]   = TOPIC_LWT;
        doc["payload_available"]    = "online";
        doc["payload_not_available"] = "offline";
        fillHaSubDevice(doc, subId, subName);
        doc["device"]["model"] = sensorModel;

        char payload[768];
        if (!serializeDoc(doc, payload, sizeof(payload))) {
            Serial.println("[MQTT] K-table text JSON too large");
            ok = false;
            return;
        }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/text/%s/config", uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    };

    publishKTableText("twwp_" NODE_ID "_k_table_1", "Output K Table",
                      "k_table_1", "set_k_table_1",
                      "twwp_" NODE_ID "_flow1", "RO Output", "USN-HS06PE");
    publishKTableText("twwp_" NODE_ID "_k_table_2", "Input K Table",
                      "k_table_2", "set_k_table_2",
                      "twwp_" NODE_ID "_flow2", "RO Input", "USN-HS06PS");

    // --- Debounce number entities ---
    auto publishDebounceNumber = [&ok](const char* uid, const char* name,
                                        const char* valueKey, const char* cmdKey,
                                        const char* subId, const char* subName,
                                        const char* sensorModel) {
        JsonDocument doc;
        doc["name"]              = name;
        doc["unique_id"]         = uid;
        doc["object_id"]         = uid;
        doc["entity_category"]   = "config";
        doc["state_topic"]       = TOPIC_STATUS;
        char tmpl[64];
        snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s | int }}", valueKey);
        doc["value_template"]    = tmpl;
        doc["command_topic"]     = TOPIC_CMD;
        char cmdTmpl[64];
        snprintf(cmdTmpl, sizeof(cmdTmpl), "{\"%s\": {{ value | int }}}", cmdKey);
        doc["command_template"]  = cmdTmpl;
        doc["unit_of_measurement"] = "µs";
        doc["min"]               = 100;
        doc["max"]               = 10000;
        doc["step"]              = 100;
        doc["mode"]              = "box";
        doc["availability_topic"]   = TOPIC_LWT;
        doc["payload_available"]    = "online";
        doc["payload_not_available"] = "offline";
        fillHaSubDevice(doc, subId, subName);
        doc["device"]["model"] = sensorModel;

        char payload[896];
        if (!serializeDoc(doc, payload, sizeof(payload))) {
            Serial.println("[MQTT] debounce number JSON too large");
            ok = false;
            return;
        }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/number/%s/config", uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    };

    publishDebounceNumber("twwp_" NODE_ID "_debounce_us_1", "Output Debounce",
                          "debounce_us_1", "set_debounce_us_1",
                          "twwp_" NODE_ID "_flow1", "RO Output", "USN-HS06PE");
    publishDebounceNumber("twwp_" NODE_ID "_debounce_us_2", "Input Debounce",
                          "debounce_us_2", "set_debounce_us_2",
                          "twwp_" NODE_ID "_flow2", "RO Input", "USN-HS06PS");

    // --- Flow average window number entity (shared across both channels) ---
    {
        JsonDocument doc;
        doc["name"]              = "Flow Average Window";
        doc["unique_id"]         = "twwp_" NODE_ID "_flow_avg_window";
        doc["object_id"]         = "twwp_" NODE_ID "_flow_avg_window";
        doc["entity_category"]   = "config";
        doc["state_topic"]       = TOPIC_STATUS;
        doc["value_template"]    = "{{ value_json.flow_avg_window | int }}";
        doc["command_topic"]     = TOPIC_CMD;
        doc["command_template"]  = "{\"set_flow_avg_window\": {{ value | int }}}";
        doc["unit_of_measurement"] = "samples";
        doc["min"]               = 1;
        doc["max"]               = 20;
        doc["step"]              = 1;
        doc["mode"]              = "box";
        doc["icon"]              = "mdi:sine-wave";
        doc["availability_topic"]   = TOPIC_LWT;
        doc["payload_available"]    = "online";
        doc["payload_not_available"] = "offline";
        fillHaDevice(doc);

        char payload[768];
        if (!serializeDoc(doc, payload, sizeof(payload))) {
            Serial.println("[MQTT] flow_avg_window number JSON too large");
            ok = false;
        } else {
            if (!netMqtt_publish(
                    "homeassistant/number/twwp_" NODE_ID "_flow_avg_window/config",
                    payload, true)) ok = false;
        }
    }

    // --- Sensor model select entities ---
    {
        char modelList[128];
        sensorFlow_getModelList(modelList, sizeof(modelList));

        auto publishModelSelect = [&ok, &modelList](const char* uid, const char* name,
                                                     const char* valueKey, const char* cmdKey,
                                                     const char* subId, const char* subName) {
            JsonDocument doc;
            doc["name"]            = name;
            doc["unique_id"]       = uid;
            doc["object_id"]       = uid;
            doc["entity_category"] = "config";
            doc["icon"]            = "mdi:pulse";
            doc["state_topic"]     = TOPIC_STATUS;
            char tmpl[64];
            snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", valueKey);
            doc["value_template"]   = tmpl;
            doc["command_topic"]    = TOPIC_CMD;
            char cmdTmpl[64];
            snprintf(cmdTmpl, sizeof(cmdTmpl), "{\"%s\": \"{{ value }}\"}", cmdKey);
            doc["command_template"] = cmdTmpl;
            doc["availability_topic"]    = TOPIC_LWT;
            doc["payload_available"]     = "online";
            doc["payload_not_available"] = "offline";
            fillHaSubDevice(doc, subId, subName);

            // Parse comma-separated model list into JSON options array
            JsonArray opts = doc["options"].to<JsonArray>();
            char tmp[128];
            strlcpy(tmp, modelList, sizeof(tmp));
            char* tok = strtok(tmp, ",");
            while (tok) { opts.add(tok); tok = strtok(nullptr, ","); }

            char payload[768];
            if (!serializeDoc(doc, payload, sizeof(payload))) {
                Serial.println("[MQTT] model select JSON too large");
                ok = false;
                return;
            }
            char topic[128];
            snprintf(topic, sizeof(topic), "homeassistant/select/%s/config", uid);
            if (!netMqtt_publish(topic, payload, true)) ok = false;
        };

        publishModelSelect("twwp_" NODE_ID "_sensor_model_1", "Output Sensor Model",
                           "sensor_model_1", "set_sensor_model_1",
                           "twwp_" NODE_ID "_flow1", "RO Output");
        publishModelSelect("twwp_" NODE_ID "_sensor_model_2", "Input Sensor Model",
                           "sensor_model_2", "set_sensor_model_2",
                           "twwp_" NODE_ID "_flow2", "RO Input");
    }

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
        char payload[640];
        if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; return; }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/config", uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    };

    pub("twwp_" NODE_ID "_session_last_id",         "Last Session ID",          "session_last_id",           "",  "");
    pub("twwp_" NODE_ID "_session_last_dur_s",      "Last Session Duration",    "session_last_dur_s",        "s", "measurement");
    pub("twwp_" NODE_ID "_session_last_flow_dur_s", "Last Session Flow Time",   "session_last_flow_dur_s",   "s", "measurement");
    pub("twwp_" NODE_ID "_session_last_idle_s",     "Last Session Idle Time",   "session_last_idle_s",       "s", "measurement");
    pub("twwp_" NODE_ID "_session_last_vol_out",    "Last Session Volume Out",  "session_last_vol_out",      "L", "measurement");
    pub("twwp_" NODE_ID "_session_last_vol_in",     "Last Session Volume In",   "session_last_vol_in",       "L", "measurement");

    Serial.print("[MQTT] HA session discovery ");
    Serial.println(ok ? "published" : "partial");
    return ok;
}

static bool publishHaDiscoverySessionConfig() {
    bool ok = true;

    auto pubNumber = [&ok](const char* uid, const char* name,
                            const char* valueKey, const char* cmdKey,
                            const char* unit, float minVal, float maxVal, float step) {
        JsonDocument doc;
        doc["name"]       = name;
        doc["unique_id"]  = uid;
        doc["object_id"]  = uid;
        doc["entity_category"] = "config";
        doc["state_topic"] = TOPIC_STATUS;
        char tmpl[80];
        snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", valueKey);
        doc["value_template"]  = tmpl;
        doc["command_topic"]   = TOPIC_CMD;
        char cmdTmpl[80];
        snprintf(cmdTmpl, sizeof(cmdTmpl), "{\"%s\": {{ value }}}", cmdKey);
        doc["command_template"]    = cmdTmpl;
        doc["unit_of_measurement"] = unit;
        doc["min"]                 = minVal;
        doc["max"]                 = maxVal;
        doc["step"]                = step;
        doc["mode"]                = "box";
        doc["availability_topic"]   = TOPIC_LWT;
        doc["payload_available"]    = "online";
        doc["payload_not_available"] = "offline";
        fillHaDevice(doc);
        char payload[768];
        if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; return; }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/number/%s/config", uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    };

    pubNumber("twwp_" NODE_ID "_session_idle_timeout", "Session Idle Timeout",
              "session_idle_timeout_s", "set_session_idle_timeout",
              "s", 5, 100, 1);
    pubNumber("twwp_" NODE_ID "_flow_threshold", "Flow Detection Threshold",
              "flow_threshold_lpm", "set_flow_threshold",
              "L/min", 0.01f, 0.5f, 0.01f);

    Serial.print("[MQTT] HA session config discovery ");
    Serial.println(ok ? "published" : "partial");
    return ok;
}

static bool publishHaDiscoverySessionsRecent() {
    bool ok = true;

    // Sessions history sensor (state = count, attributes = sessions array)
    {
        JsonDocument doc;
        doc["name"]                   = "Recent Sessions";
        doc["unique_id"]              = "twwp_" NODE_ID "_sessions_recent";
        doc["object_id"]              = "twwp_" NODE_ID "_sessions_recent";
        doc["state_topic"]            = TOPIC_SESSIONS_RECENT;
        doc["value_template"]         = "{{ value_json.sessions | length }}";
        doc["json_attributes_topic"]  = TOPIC_SESSIONS_RECENT;
        doc["unit_of_measurement"]    = "sessions";
        doc["icon"]                   = "mdi:history";
        doc["availability_topic"]     = TOPIC_LWT;
        doc["payload_available"]      = "online";
        doc["payload_not_available"]  = "offline";
        fillHaDevice(doc);
        char payload[640];
        if (serializeDoc(doc, payload, sizeof(payload))) {
            ok &= netMqtt_publish(
                "homeassistant/sensor/twwp_" NODE_ID "_sessions_recent/config", payload, true);
        } else { ok = false; }
    }

    // Leak suspect binary sensors
    struct { const char* uid; const char* name; const char* valueKey; } leakEntries[] = {
        { "twwp_" NODE_ID "_leak_suspect_1", "Output Leak Suspect", "leak_suspect_1" },
        { "twwp_" NODE_ID "_leak_suspect_2", "Input Leak Suspect",  "leak_suspect_2" },
    };
    for (auto& e : leakEntries) {
        JsonDocument doc;
        doc["name"]              = e.name;
        doc["unique_id"]         = e.uid;
        doc["object_id"]         = e.uid;
        doc["device_class"]      = "moisture";
        doc["state_topic"]       = TOPIC_STATUS;
        char tmpl[80];
        snprintf(tmpl, sizeof(tmpl), "{{ 'ON' if value_json.%s else 'OFF' }}", e.valueKey);
        doc["value_template"]    = tmpl;
        doc["availability_topic"]   = TOPIC_LWT;
        doc["payload_available"]    = "online";
        doc["payload_not_available"] = "offline";
        fillHaDevice(doc);
        char payload[640];
        if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; continue; }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/binary_sensor/%s/config", e.uid);
        ok &= netMqtt_publish(topic, payload, true);
    }

    Serial.print("[MQTT] HA sessions_recent discovery ");
    Serial.println(ok ? "published" : "partial");
    return ok;
}

static bool publishHaDiscoveryResetButtons() {
    bool ok = true;

    auto pubButton = [&ok](const char* uid, const char* name, const char* payload_press,
                            const char* subId, const char* subName) {
        JsonDocument doc;
        doc["name"]              = name;
        doc["unique_id"]         = uid;
        doc["object_id"]         = uid;
        doc["entity_category"]   = "config";
        doc["command_topic"]     = TOPIC_CMD;
        doc["payload_press"]     = payload_press;
        doc["availability_topic"]  = TOPIC_LWT;
        doc["payload_available"]   = "online";
        doc["payload_not_available"] = "offline";
        if (subId && subId[0]) {
            fillHaSubDevice(doc, subId, subName);
        } else {
            fillHaDevice(doc);
        }
        char payload[640];
        if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; return; }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/button/%s/config", uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    };

    // RO Output (ch1) — granular period resets + full channel reset
    pubButton("twwp_" NODE_ID "_reset_today_1",  "Reset Today",
              "{\"reset_flow_today_1\": true}",  "twwp_" NODE_ID "_flow1", "RO Output");
    pubButton("twwp_" NODE_ID "_reset_week_1",   "Reset This Week",
              "{\"reset_flow_week_1\": true}",   "twwp_" NODE_ID "_flow1", "RO Output");
    pubButton("twwp_" NODE_ID "_reset_month_1",  "Reset This Month",
              "{\"reset_flow_month_1\": true}",  "twwp_" NODE_ID "_flow1", "RO Output");
    pubButton("twwp_" NODE_ID "_reset_year_1",   "Reset This Year",
              "{\"reset_flow_year_1\": true}",   "twwp_" NODE_ID "_flow1", "RO Output");
    pubButton("twwp_" NODE_ID "_reset_totals_1", "Reset All Data",
              "{\"reset_flow_totals_1\": true}", "twwp_" NODE_ID "_flow1", "RO Output");

    // RO Input (ch2) — granular period resets + full channel reset
    pubButton("twwp_" NODE_ID "_reset_today_2",  "Reset Today",
              "{\"reset_flow_today_2\": true}",  "twwp_" NODE_ID "_flow2", "RO Input");
    pubButton("twwp_" NODE_ID "_reset_week_2",   "Reset This Week",
              "{\"reset_flow_week_2\": true}",   "twwp_" NODE_ID "_flow2", "RO Input");
    pubButton("twwp_" NODE_ID "_reset_month_2",  "Reset This Month",
              "{\"reset_flow_month_2\": true}",  "twwp_" NODE_ID "_flow2", "RO Input");
    pubButton("twwp_" NODE_ID "_reset_year_2",   "Reset This Year",
              "{\"reset_flow_year_2\": true}",   "twwp_" NODE_ID "_flow2", "RO Input");
    pubButton("twwp_" NODE_ID "_reset_totals_2", "Reset All Data",
              "{\"reset_flow_totals_2\": true}", "twwp_" NODE_ID "_flow2", "RO Input");

    // Main node card — factory reset (all channels + session ID)
    pubButton("twwp_" NODE_ID "_factory_reset_flow", "Factory Reset All Flow Data",
              "{\"factory_reset_flow\": true}", nullptr, nullptr);

    Serial.print("[MQTT] HA reset button discovery ");
    Serial.println(ok ? "published" : "partial");
    return ok;
}

static bool publishHaDiscoverySessionSwitch() {
    JsonDocument doc;
    doc["name"]              = "Session Tracking";
    doc["unique_id"]         = "twwp_" NODE_ID "_session_enabled";
    doc["object_id"]         = "twwp_" NODE_ID "_session_enabled";
    doc["entity_category"]   = "config";
    doc["state_topic"]       = TOPIC_STATUS;
    doc["value_template"]    = "{{ 'ON' if value_json.session_enabled else 'OFF' }}";
    doc["command_topic"]     = TOPIC_CMD;
    doc["payload_on"]        = "{\"set_session_enabled\": true}";
    doc["payload_off"]       = "{\"set_session_enabled\": false}";
    doc["availability_topic"]   = TOPIC_LWT;
    doc["payload_available"]    = "online";
    doc["payload_not_available"] = "offline";
    fillHaDevice(doc);

    char payload[640];
    if (!serializeDoc(doc, payload, sizeof(payload))) {
        Serial.println("[MQTT] session switch JSON too large");
        return false;
    }
    bool ok = netMqtt_publish(
        "homeassistant/switch/twwp_" NODE_ID "_session_enabled/config", payload, true);
    Serial.print("[MQTT] HA session switch discovery ");
    Serial.println(ok ? "published" : "failed");
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

    // Restart device button
    {
        JsonDocument doc;
        doc["name"]               = "TWWP " NODE_ID " Restart Device";
        doc["unique_id"]          = "twwp_" NODE_ID "_restart_device";
        doc["object_id"]          = "twwp_" NODE_ID "_restart_device";
        doc["entity_category"]    = "config";
        doc["icon"]               = "mdi:restart";
        doc["command_topic"]      = TOPIC_CMD;
        doc["payload_press"]      = "{\"restart_device\": true}";
        doc["availability_topic"]  = TOPIC_LWT;
        doc["payload_available"]   = "online";
        doc["payload_not_available"] = "offline";
        fillHaDevice(doc);
        char payload[768];
        if (serializeDoc(doc, payload, sizeof(payload))) {
            ok &= netMqtt_publish("homeassistant/button/twwp_" NODE_ID "_restart_device/config", payload, true);
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

static bool publishHaDiscoveryOta() {
    bool ok = true;
    ok &= publishHaDiagSensor("twwp_" NODE_ID "_ota_state", "TWWP " NODE_ID " OTA State",
                              "ota_state", "", "", "measurement");
    ok &= publishHaDiagSensor("twwp_" NODE_ID "_ota_progress", "TWWP " NODE_ID " OTA Progress",
                              "ota_progress_pct", "%", "", "measurement");

    Serial.print("[MQTT] HA OTA discovery ");
    Serial.println(ok ? "published" : "partial");
    return ok;
}

static bool publishHaDiscoveryVoltage() {
    bool ok = true;

    // Voltage sensor
    {
        JsonDocument doc;
        doc["name"]              = "TWWP " NODE_ID " Supply Voltage";
        doc["unique_id"]         = "twwp_" NODE_ID "_supply_voltage";
        doc["object_id"]         = "twwp_" NODE_ID "_supply_voltage";
        doc["device_class"]      = "voltage";
        doc["state_class"]       = "measurement";
        doc["unit_of_measurement"] = "V";
        doc["state_topic"]       = TOPIC_STATUS;
        doc["value_template"]    = "{{ value_json.supply_voltage | round(2) }}";
        doc["availability_topic"]   = TOPIC_LWT;
        doc["payload_available"]    = "online";
        doc["payload_not_available"] = "offline";
        fillHaDevice(doc);
        char payload[640];
        if (serializeDoc(doc, payload, sizeof(payload))) {
            ok &= netMqtt_publish(
                "homeassistant/sensor/twwp_" NODE_ID "_supply_voltage/config", payload, true);
        } else { ok = false; }
    }

    // Battery % sensor
    {
        JsonDocument doc;
        doc["name"]              = "TWWP " NODE_ID " Battery";
        doc["unique_id"]         = "twwp_" NODE_ID "_supply_voltage_pct";
        doc["object_id"]         = "twwp_" NODE_ID "_supply_voltage_pct";
        doc["device_class"]      = "battery";
        doc["state_class"]       = "measurement";
        doc["unit_of_measurement"] = "%";
        doc["state_topic"]       = TOPIC_STATUS;
        doc["value_template"]    = "{{ value_json.supply_voltage_pct | round(1) }}";
        doc["availability_topic"]   = TOPIC_LWT;
        doc["payload_available"]    = "online";
        doc["payload_not_available"] = "offline";
        fillHaDevice(doc);
        char payload[640];
        if (serializeDoc(doc, payload, sizeof(payload))) {
            ok &= netMqtt_publish(
                "homeassistant/sensor/twwp_" NODE_ID "_supply_voltage_pct/config", payload, true);
        } else { ok = false; }
    }

    // Raw divider voltage diagnostic sensor
    {
        JsonDocument doc;
        doc["name"]                = "TWWP " NODE_ID " Supply Voltage Divider";
        doc["unique_id"]           = "twwp_" NODE_ID "_supply_voltage_divider";
        doc["object_id"]           = "twwp_" NODE_ID "_supply_voltage_divider";
        doc["device_class"]        = "voltage";
        doc["state_class"]         = "measurement";
        doc["entity_category"]     = "diagnostic";
        doc["unit_of_measurement"] = "V";
        doc["state_topic"]         = TOPIC_STATUS;
        doc["value_template"]      = "{{ value_json.supply_voltage_divider | round(3) }}";
        doc["availability_topic"]   = TOPIC_LWT;
        doc["payload_available"]    = "online";
        doc["payload_not_available"] = "offline";
        fillHaDevice(doc);
        char payload[640];
        if (serializeDoc(doc, payload, sizeof(payload))) {
            ok &= netMqtt_publish(
                "homeassistant/sensor/twwp_" NODE_ID "_supply_voltage_divider/config", payload, true);
        } else { ok = false; }
    }

    // Battery state sensor
    {
        JsonDocument doc;
        doc["name"]              = "TWWP " NODE_ID " Battery State";
        doc["unique_id"]         = "twwp_" NODE_ID "_supply_voltage_state";
        doc["object_id"]         = "twwp_" NODE_ID "_supply_voltage_state";
        doc["state_topic"]       = TOPIC_STATUS;
        doc["value_template"]    = "{{ value_json.supply_voltage_state }}";
        doc["icon"]              = "mdi:battery-charging";
        doc["availability_topic"]   = TOPIC_LWT;
        doc["payload_available"]    = "online";
        doc["payload_not_available"] = "offline";
        fillHaDevice(doc);
        char payload[640];
        if (serializeDoc(doc, payload, sizeof(payload))) {
            ok &= netMqtt_publish(
                "homeassistant/sensor/twwp_" NODE_ID "_supply_voltage_state/config", payload, true);
        } else { ok = false; }
    }

    // Config number: V Min
    auto pubVoltageNumber = [&ok](const char* uid, const char* name,
                                   const char* valueKey, const char* cmdKey,
                                   float minVal, float maxVal, float step) {
        JsonDocument doc;
        doc["name"]              = name;
        doc["unique_id"]         = uid;
        doc["object_id"]         = uid;
        doc["entity_category"]   = "config";
        doc["state_topic"]       = TOPIC_STATUS;
        char tmpl[80];
        snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", valueKey);
        doc["value_template"]    = tmpl;
        doc["command_topic"]     = TOPIC_CMD;
        char cmdTmpl[80];
        snprintf(cmdTmpl, sizeof(cmdTmpl), "{\"%s\": {{ value }}}", cmdKey);
        doc["command_template"]  = cmdTmpl;
        doc["unit_of_measurement"] = "V";
        doc["min"]               = minVal;
        doc["max"]               = maxVal;
        doc["step"]              = step;
        doc["mode"]              = "box";
        doc["availability_topic"]   = TOPIC_LWT;
        doc["payload_available"]    = "online";
        doc["payload_not_available"] = "offline";
        fillHaDevice(doc);
        char payload[768];
        if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; return; }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/number/%s/config", uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    };

    pubVoltageNumber("twwp_" NODE_ID "_voltage_v_min", "Battery Empty Voltage",
                     "voltage_v_min", "set_v_min", 9.0f, 13.0f, 0.1f);
    pubVoltageNumber("twwp_" NODE_ID "_voltage_v_max", "Battery Full Voltage",
                     "voltage_v_max", "set_v_max", 12.0f, 16.0f, 0.1f);

    // Config number: Cal Factor (unitless, but we use the same helper with "×" unit)
    {
        JsonDocument doc;
        doc["name"]              = "Battery Voltage Cal Factor";
        doc["unique_id"]         = "twwp_" NODE_ID "_voltage_cal_factor";
        doc["object_id"]         = "twwp_" NODE_ID "_voltage_cal_factor";
        doc["entity_category"]   = "config";
        doc["state_topic"]       = TOPIC_STATUS;
        doc["value_template"]    = "{{ value_json.voltage_cal_factor }}";
        doc["command_topic"]     = TOPIC_CMD;
        doc["command_template"]  = "{\"set_voltage_cal\": {{ value }}}";
        doc["min"]               = 0.8f;
        doc["max"]               = 1.2f;
        doc["step"]              = 0.001f;
        doc["mode"]              = "box";
        doc["icon"]              = "mdi:tune";
        doc["availability_topic"]   = TOPIC_LWT;
        doc["payload_available"]    = "online";
        doc["payload_not_available"] = "offline";
        fillHaDevice(doc);
        char payload[768];
        if (serializeDoc(doc, payload, sizeof(payload))) {
            ok &= netMqtt_publish(
                "homeassistant/number/twwp_" NODE_ID "_voltage_cal_factor/config", payload, true);
        } else { ok = false; }
    }

    Serial.print("[MQTT] HA voltage discovery ");
    Serial.println(ok ? "published" : "partial");
    return ok;
}

static bool publishHaDiscoveryValve() {
    JsonDocument doc;
    doc["name"]            = "TWWP " NODE_ID " Valve";
    doc["unique_id"]       = "twwp_" NODE_ID "_valve_open";
    doc["object_id"]       = "twwp_" NODE_ID "_valve_open";
    doc["device_class"]    = "opening";
    doc["state_topic"]     = TOPIC_STATUS;
    doc["value_template"]  = "{{ 'ON' if value_json.valve_open else 'OFF' }}";
    doc["availability_topic"]   = TOPIC_LWT;
    doc["payload_available"]    = "online";
    doc["payload_not_available"] = "offline";
    fillHaDevice(doc);

    char payload[640];
    if (!serializeDoc(doc, payload, sizeof(payload))) {
        Serial.println("[MQTT] valve HA discovery JSON too large");
        return false;
    }
    bool ok = netMqtt_publish(
        "homeassistant/binary_sensor/twwp_" NODE_ID "_valve_open/config", payload, true);
    Serial.print("[MQTT] HA valve discovery ");
    Serial.println(ok ? "published" : "failed");
    return ok;
}

static bool publishHaDiscoveryValveConfig() {
    bool ok = true;

    // 2 × select: valve_type, trigger_source
    struct { const char* uid; const char* name; const char* valueKey; const char* cmdKey;
             const char* opt0; const char* opt1; const char* opt2; } selects[] = {
        { "twwp_" NODE_ID "_valve_type", "Valve Type",
          "valve_type", "set_valve_type",
          "test", "solenoid", "ball_valve" },
        { "twwp_" NODE_ID "_trigger_source", "Valve Trigger Source",
          "trigger_source", "set_trigger_source",
          "flow", "manual", nullptr },
    };
    for (auto& s : selects) {
        JsonDocument doc;
        doc["name"]             = s.name;
        doc["unique_id"]        = s.uid;
        doc["object_id"]        = s.uid;
        doc["entity_category"]  = "config";
        doc["state_topic"]      = TOPIC_STATUS;
        char tmpl[80];
        snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", s.valueKey);
        doc["value_template"]   = tmpl;
        doc["command_topic"]    = TOPIC_CMD;
        char cmdTmpl[80];
        snprintf(cmdTmpl, sizeof(cmdTmpl), "{\"%s\": \"{{ value }}\"}", s.cmdKey);
        doc["command_template"] = cmdTmpl;
        JsonArray opts = doc["options"].to<JsonArray>();
        opts.add(s.opt0); opts.add(s.opt1);
        if (s.opt2) opts.add(s.opt2);
        doc["availability_topic"]    = TOPIC_LWT;
        doc["payload_available"]     = "online";
        doc["payload_not_available"] = "offline";
        fillHaDevice(doc);
        char payload[768];
        if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; continue; }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/select/%s/config", s.uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    }

    // 2 × number: idle_timeout_s, max_open_s
    struct { const char* uid; const char* name; const char* valueKey; const char* cmdKey; } nums[] = {
        { "twwp_" NODE_ID "_valve_idle_timeout", "Valve Idle Timeout",
          "valve_idle_timeout_s", "set_valve_idle_timeout" },
        { "twwp_" NODE_ID "_valve_max_open", "Valve Max Open Time",
          "valve_max_open_s", "set_valve_max_open" },
    };
    for (auto& n : nums) {
        JsonDocument doc;
        doc["name"]             = n.name;
        doc["unique_id"]        = n.uid;
        doc["object_id"]        = n.uid;
        doc["entity_category"]  = "config";
        doc["state_topic"]      = TOPIC_STATUS;
        char tmpl[80];
        snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", n.valueKey);
        doc["value_template"]   = tmpl;
        doc["command_topic"]    = TOPIC_CMD;
        char cmdTmpl[80];
        snprintf(cmdTmpl, sizeof(cmdTmpl), "{\"%s\": {{ value | int }}}", n.cmdKey);
        doc["command_template"] = cmdTmpl;
        doc["unit_of_measurement"] = "s";
        doc["min"]  = 0;
        doc["max"]  = 3600;
        doc["step"] = 1;
        doc["mode"] = "box";
        doc["icon"] = "mdi:timer-outline";
        doc["availability_topic"]    = TOPIC_LWT;
        doc["payload_available"]     = "online";
        doc["payload_not_available"] = "offline";
        fillHaDevice(doc);
        char payload[768];
        if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; continue; }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/number/%s/config", n.uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    }

    // 2 × switch: timeout_disable_auto, timeout_alert
    struct { const char* uid; const char* name; const char* valueKey;
             const char* payloadOn; const char* payloadOff; } switches[] = {
        { "twwp_" NODE_ID "_valve_timeout_disable_auto", "Timeout Disables Auto",
          "valve_timeout_disable_auto",
          "{\"set_valve_timeout_disable_auto\": true}",
          "{\"set_valve_timeout_disable_auto\": false}" },
        { "twwp_" NODE_ID "_valve_timeout_alert", "Timeout Publishes Alert",
          "valve_timeout_alert",
          "{\"set_valve_timeout_alert\": true}",
          "{\"set_valve_timeout_alert\": false}" },
    };
    for (auto& sw : switches) {
        JsonDocument doc;
        doc["name"]             = sw.name;
        doc["unique_id"]        = sw.uid;
        doc["object_id"]        = sw.uid;
        doc["entity_category"]  = "config";
        doc["state_topic"]      = TOPIC_STATUS;
        char tmpl[96];
        snprintf(tmpl, sizeof(tmpl), "{{ 'ON' if value_json.%s else 'OFF' }}", sw.valueKey);
        doc["value_template"]   = tmpl;
        doc["command_topic"]    = TOPIC_CMD;
        doc["payload_on"]       = sw.payloadOn;
        doc["payload_off"]      = sw.payloadOff;
        doc["availability_topic"]    = TOPIC_LWT;
        doc["payload_available"]     = "online";
        doc["payload_not_available"] = "offline";
        fillHaDevice(doc);
        char payload[768];
        if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; continue; }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/switch/%s/config", sw.uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    }

    Serial.print("[MQTT] HA valve config discovery ");
    Serial.println(ok ? "published" : "partial");
    return ok;
}

static bool publishHaDiscoveryEfficiency() {
    bool ok = true;

    struct { const char* uid; const char* name; const char* valueKey; } entries[] = {
        { "twwp_" NODE_ID "_waste_ratio_today", "Waste Ratio Today",      "waste_ratio_today"  },
        { "twwp_" NODE_ID "_waste_ratio_week",  "Waste Ratio This Week",  "waste_ratio_week"   },
        { "twwp_" NODE_ID "_waste_ratio_month", "Waste Ratio This Month", "waste_ratio_month"  },
        { "twwp_" NODE_ID "_waste_ratio_year",  "Waste Ratio This Year",  "waste_ratio_year"   },
    };

    for (auto& e : entries) {
        JsonDocument doc;
        doc["name"]              = e.name;
        doc["unique_id"]         = e.uid;
        doc["object_id"]         = e.uid;
        doc["state_topic"]       = TOPIC_STATUS;
        char tmpl[80];
        snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", e.valueKey);
        doc["value_template"]      = tmpl;
        doc["unit_of_measurement"] = ":1";
        doc["state_class"]         = "measurement";
        doc["icon"]                = "mdi:water-percent";
        doc["availability_topic"]   = TOPIC_LWT;
        doc["payload_available"]    = "online";
        doc["payload_not_available"] = "offline";
        fillHaSubDevice(doc, "twwp_" NODE_ID "_filter", "RO Filter");
        doc["device"]["model"] = "Calculated";

        char payload[640];
        if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; continue; }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/config", e.uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    }

    Serial.print("[MQTT] HA efficiency discovery ");
    Serial.println(ok ? "published" : "partial");
    return ok;
}

static bool publishHaWaterQualitySensor(const char* uid, const char* name, const char* valueKey,
                                        const char* unit, const char* deviceClass,
                                        const char* stateClass, const char* subId,
                                        const char* subName) {
    JsonDocument doc;
    doc["name"]       = name;
    doc["unique_id"]  = uid;
    doc["object_id"]  = uid;
    doc["state_topic"] = TOPIC_STATUS;
    char tmpl[80];
    snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", valueKey);
    doc["value_template"] = tmpl;
    if (unit && unit[0]) {
        doc["unit_of_measurement"] = unit;
    }
    if (deviceClass && deviceClass[0]) {
        doc["device_class"] = deviceClass;
    }
    if (stateClass && stateClass[0]) {
        doc["state_class"] = stateClass;
    }
    doc["availability_topic"] = TOPIC_LWT;
    doc["payload_available"] = "online";
    doc["payload_not_available"] = "offline";
    fillHaSubDevice(doc, subId, subName);
    doc["device"]["model"] = "YiErYi 3178 RS485 Modbus";

    char payload[768];
    if (!serializeDoc(doc, payload, sizeof(payload))) {
        Serial.println("[MQTT] water quality HA discovery JSON too large");
        return false;
    }
    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/config", uid);
    return netMqtt_publish(topic, payload, true);
}

static bool publishHaDiscoveryWaterQuality() {
    bool ok = true;

    struct ZoneDef {
        const char* suffix;
        const char* name;
        const char* subId;
    };
    const ZoneDef zoneDefs[] = {
        { "pre_ro",  "Pre-RO Water Quality",        "twwp_" NODE_ID "_wq_pre_ro" },
        { "post_ro", "Post-RO Water Quality",       "twwp_" NODE_ID "_wq_post_ro" },
        { "remin",   "Remineralised Water Quality", "twwp_" NODE_ID "_wq_remin" },
    };

    for (const auto& z : zoneDefs) {
        char uid[64];
        char key[32];
        char name[64];

        snprintf(uid, sizeof(uid), "twwp_" NODE_ID "_wq_%s_ph", z.suffix);
        snprintf(key, sizeof(key), "wq_%s_ph", z.suffix);
        snprintf(name, sizeof(name), "%s pH", z.name);
        ok &= publishHaWaterQualitySensor(uid, name, key, "pH", "", "measurement", z.subId, z.name);

        snprintf(uid, sizeof(uid), "twwp_" NODE_ID "_wq_%s_orp", z.suffix);
        snprintf(key, sizeof(key), "wq_%s_orp", z.suffix);
        snprintf(name, sizeof(name), "%s ORP", z.name);
        ok &= publishHaWaterQualitySensor(uid, name, key, "mV", "voltage", "measurement", z.subId, z.name);

        snprintf(uid, sizeof(uid), "twwp_" NODE_ID "_wq_%s_ec", z.suffix);
        snprintf(key, sizeof(key), "wq_%s_ec", z.suffix);
        snprintf(name, sizeof(name), "%s EC", z.name);
        ok &= publishHaWaterQualitySensor(uid, name, key, "µS/cm", "", "measurement", z.subId, z.name);

        snprintf(uid, sizeof(uid), "twwp_" NODE_ID "_wq_%s_tds_ppm", z.suffix);
        snprintf(key, sizeof(key), "wq_%s_tds_ppm", z.suffix);
        snprintf(name, sizeof(name), "%s TDS", z.name);
        ok &= publishHaWaterQualitySensor(uid, name, key, "ppm", "", "measurement", z.subId, z.name);

        snprintf(uid, sizeof(uid), "twwp_" NODE_ID "_wq_%s_temp", z.suffix);
        snprintf(key, sizeof(key), "wq_%s_temp", z.suffix);
        snprintf(name, sizeof(name), "%s Temperature", z.name);
        ok &= publishHaWaterQualitySensor(uid, name, key, "°C", "temperature", "measurement", z.subId, z.name);

        snprintf(uid, sizeof(uid), "twwp_" NODE_ID "_wq_%s_ph_cal_date", z.suffix);
        snprintf(key, sizeof(key), "wq_%s_ph_cal_date", z.suffix);
        snprintf(name, sizeof(name), "%s pH Cal Date", z.name);
        ok &= publishHaWaterQualitySensor(uid, name, key, "", "", "", z.subId, z.name);

        snprintf(uid, sizeof(uid), "twwp_" NODE_ID "_wq_%s_orp_cal_date", z.suffix);
        snprintf(key, sizeof(key), "wq_%s_orp_cal_date", z.suffix);
        snprintf(name, sizeof(name), "%s ORP Cal Date", z.name);
        ok &= publishHaWaterQualitySensor(uid, name, key, "", "", "", z.subId, z.name);

        snprintf(uid, sizeof(uid), "twwp_" NODE_ID "_wq_%s_ec_cal_date", z.suffix);
        snprintf(key, sizeof(key), "wq_%s_ec_cal_date", z.suffix);
        snprintf(name, sizeof(name), "%s EC Cal Date", z.name);
        ok &= publishHaWaterQualitySensor(uid, name, key, "", "", "", z.subId, z.name);
    }

    Serial.print("[MQTT] HA water quality discovery ");
    Serial.println(ok ? "published" : "partial");
    return ok;
}

static bool publishHaDiscoveryTdsMeter() {
    bool ok = true;

    struct ZoneDef {
        const char* suffix;
        const char* name;
        const char* subId;
        uint8_t     zone;
    };
    const ZoneDef zoneDefs[] = {
        { "pre_ro",  "Pre-RO TDS Meter",  "twwp_" NODE_ID "_tds_pre_ro",  TDS_ZONE_PRE_RO  },
        { "post_ro", "Post-RO TDS Meter", "twwp_" NODE_ID "_tds_post_ro", TDS_ZONE_POST_RO },
    };

    for (const auto& z : zoneDefs) {
        char uid[64];
        char key[32];
        char name[64];

        snprintf(uid, sizeof(uid), "twwp_" NODE_ID "_tds_%s_ec", z.suffix);
        snprintf(key, sizeof(key), "tds_%s_ec", z.suffix);
        snprintf(name, sizeof(name), "%s EC", z.name);
        ok &= publishHaWaterQualitySensor(uid, name, key, "µS/cm", "", "measurement", z.subId, z.name);

        snprintf(uid, sizeof(uid), "twwp_" NODE_ID "_tds_%s_temp", z.suffix);
        snprintf(key, sizeof(key), "tds_%s_temp", z.suffix);
        snprintf(name, sizeof(name), "%s Temperature", z.name);
        ok &= publishHaWaterQualitySensor(uid, name, key, "°C", "temperature", "measurement", z.subId, z.name);

        snprintf(uid, sizeof(uid), "twwp_" NODE_ID "_tds_%s_ppm", z.suffix);
        snprintf(key, sizeof(key), "tds_%s_ppm", z.suffix);
        snprintf(name, sizeof(name), "%s TDS", z.name);
        ok &= publishHaWaterQualitySensor(uid, name, key, "ppm", "", "measurement", z.subId, z.name);
    }

    Serial.print("[MQTT] HA TDS meter discovery ");
    Serial.println(ok ? "published" : "partial");
    return ok;
}

static bool publishHaDiscoveryWqConfig() {
    bool ok = true;
    const char* stateTopic = TOPIC_WQ_CONFIG;

    struct NumEnt { const char* uid; const char* name; const char* vk; const char* ck;
                    float mn; float mx; float st; } nums[] = {
        { "twwp_" NODE_ID "_wq_pre_ro_max",        "Pre-RO Max TDS",    "pre_ro_max",       "wq_pre_ro_max",        0, 500, 1   },
        { "twwp_" NODE_ID "_wq_post_ro_good_max",  "Post-RO Good Max",  "post_ro_good_max", "wq_post_ro_good_max",  0,  50, 0.5 },
        { "twwp_" NODE_ID "_wq_post_ro_check_max", "Post-RO Check Max", "post_ro_check_max","wq_post_ro_check_max", 0,  50, 0.5 },
        { "twwp_" NODE_ID "_wq_remin_min",          "Remin Min TDS",     "remin_min",        "wq_remin_min",         0, 100, 1   },
        { "twwp_" NODE_ID "_wq_remin_max",          "Remin Max TDS",     "remin_max",        "wq_remin_max",         0, 100, 1   },
    };
    for (auto& n : nums) {
        JsonDocument doc;
        doc["name"]             = n.name;
        doc["unique_id"]        = n.uid;
        doc["object_id"]        = n.uid;
        doc["entity_category"]  = "config";
        doc["state_topic"]      = stateTopic;
        char tmpl[80];
        snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", n.vk);
        doc["value_template"]   = tmpl;
        doc["command_topic"]    = TOPIC_CMD;
        char ctmpl[80];
        snprintf(ctmpl, sizeof(ctmpl), "{\"%s\": {{ value }}}", n.ck);
        doc["command_template"] = ctmpl;
        doc["unit_of_measurement"] = "ppm";
        doc["min"]  = n.mn;
        doc["max"]  = n.mx;
        doc["step"] = n.st;
        doc["mode"] = "box";
        doc["icon"] = "mdi:water-check";
        doc["availability_topic"]    = TOPIC_LWT;
        doc["payload_available"]     = "online";
        doc["payload_not_available"] = "offline";
        fillHaDevice(doc);
        char payload[768];
        if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; continue; }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/number/%s/config", n.uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    }

    struct TxtEnt { const char* uid; const char* name; const char* vk; const char* ck; } txts[] = {
        { "twwp_" NODE_ID "_wq_pre_ro_name",          "Pre-RO Name",            "pre_ro_name",         "wq_pre_ro_name"          },
        { "twwp_" NODE_ID "_wq_post_ro_name",         "Post-RO Name",           "post_ro_name",        "wq_post_ro_name"         },
        { "twwp_" NODE_ID "_wq_remin_name",           "Remin Name",             "remin_name",          "wq_remin_name"           },
        { "twwp_" NODE_ID "_wq_pre_ro_ok_label",      "Pre-RO OK Label",        "pre_ro_ok_label",     "wq_pre_ro_ok_label"      },
        { "twwp_" NODE_ID "_wq_pre_ro_warn_label",    "Pre-RO Warn Label",      "pre_ro_warn_label",   "wq_pre_ro_warn_label"    },
        { "twwp_" NODE_ID "_wq_post_ro_good_label",   "Post-RO Good Label",     "post_ro_good_label",  "wq_post_ro_good_label"   },
        { "twwp_" NODE_ID "_wq_post_ro_check_label",  "Post-RO Check Label",    "post_ro_check_label", "wq_post_ro_check_label"  },
        { "twwp_" NODE_ID "_wq_post_ro_change_label", "Post-RO Change Label",   "post_ro_change_label","wq_post_ro_change_label" },
        { "twwp_" NODE_ID "_wq_remin_low_label",      "Remin Low Label",        "remin_low_label",     "wq_remin_low_label"      },
        { "twwp_" NODE_ID "_wq_remin_ok_label",       "Remin OK Label",         "remin_ok_label",      "wq_remin_ok_label"       },
        { "twwp_" NODE_ID "_wq_remin_high_label",     "Remin High Label",       "remin_high_label",    "wq_remin_high_label"     },
    };
    for (auto& t : txts) {
        JsonDocument doc;
        doc["name"]             = t.name;
        doc["unique_id"]        = t.uid;
        doc["object_id"]        = t.uid;
        doc["entity_category"]  = "config";
        doc["state_topic"]      = stateTopic;
        char tmpl[80];
        snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", t.vk);
        doc["value_template"]   = tmpl;
        doc["command_topic"]    = TOPIC_CMD;
        char ctmpl[80];
        snprintf(ctmpl, sizeof(ctmpl), "{\"%s\": \"{{ value }}\"}", t.ck);
        doc["command_template"] = ctmpl;
        doc["max"]  = 15;
        doc["icon"] = "mdi:label-outline";
        doc["availability_topic"]    = TOPIC_LWT;
        doc["payload_available"]     = "online";
        doc["payload_not_available"] = "offline";
        fillHaDevice(doc);
        char payload[768];
        if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; continue; }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/text/%s/config", t.uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    }

    Serial.print("[MQTT] HA WQ config discovery ");
    Serial.println(ok ? "published" : "partial");
    return ok;
}

static bool publishHaDiscoveryCalibration() {
    bool ok = true;

    // Shared helpers ──────────────────────────────────────────────────────────

    auto pubButton = [&ok](const char* uid, const char* name, const char* payloadPress,
                            const char* subId, const char* subName) {
        JsonDocument doc;
        doc["name"]              = name;
        doc["unique_id"]         = uid;
        doc["object_id"]         = uid;
        doc["entity_category"]   = "config";
        doc["command_topic"]     = TOPIC_CMD;
        doc["payload_press"]     = payloadPress;
        doc["availability_topic"]    = TOPIC_LWT;
        doc["payload_available"]     = "online";
        doc["payload_not_available"] = "offline";
        fillHaSubDevice(doc, subId, subName);
        char payload[640];
        if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; return; }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/button/%s/config", uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    };

    auto pubNumber = [&ok](const char* uid, const char* name,
                            const char* valueKey, const char* cmdKey,
                            const char* unit, float mn, float mx, float step,
                            const char* subId, const char* subName,
                            const char* entityCategory = "config") {
        JsonDocument doc;
        doc["name"]             = name;
        doc["unique_id"]        = uid;
        doc["object_id"]        = uid;
        doc["entity_category"]  = entityCategory;
        doc["state_topic"]      = TOPIC_STATUS;
        char tmpl[80];
        snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", valueKey);
        doc["value_template"]   = tmpl;
        doc["command_topic"]    = TOPIC_CMD;
        char ctmpl[80];
        snprintf(ctmpl, sizeof(ctmpl), "{\"%s\": {{ value }}}", cmdKey);
        doc["command_template"] = ctmpl;
        if (unit && unit[0]) doc["unit_of_measurement"] = unit;
        doc["min"]  = mn;
        doc["max"]  = mx;
        doc["step"] = step;
        doc["mode"] = "box";
        doc["availability_topic"]    = TOPIC_LWT;
        doc["payload_available"]     = "online";
        doc["payload_not_available"] = "offline";
        fillHaSubDevice(doc, subId, subName);
        char payload[768];
        if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; return; }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/number/%s/config", uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    };

    auto pubDiagSensor = [&ok](const char* uid, const char* name,
                                const char* valueKey, const char* unit,
                                const char* subId, const char* subName) {
        JsonDocument doc;
        doc["name"]             = name;
        doc["unique_id"]        = uid;
        doc["object_id"]        = uid;
        doc["entity_category"]  = "diagnostic";
        doc["state_topic"]      = TOPIC_STATUS;
        char tmpl[80];
        snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", valueKey);
        doc["value_template"]   = tmpl;
        if (unit && unit[0]) doc["unit_of_measurement"] = unit;
        doc["availability_topic"]    = TOPIC_LWT;
        doc["payload_available"]     = "online";
        doc["payload_not_available"] = "offline";
        fillHaSubDevice(doc, subId, subName);
        char payload[640];
        if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; return; }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/config", uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    };

    auto pubCalDateText = [&ok](const char* uid, const char* name,
                                 const char* valueKey, const char* cmdKey,
                                 const char* subId, const char* subName) {
        JsonDocument doc;
        doc["name"]             = name;
        doc["unique_id"]        = uid;
        doc["object_id"]        = uid;
        doc["entity_category"]  = "config";
        doc["state_topic"]      = TOPIC_STATUS;
        char tmpl[80];
        snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", valueKey);
        doc["value_template"]   = tmpl;
        doc["command_topic"]    = TOPIC_CMD;
        char ctmpl[80];
        snprintf(ctmpl, sizeof(ctmpl), "{\"%s\": \"{{ value }}\"}", cmdKey);
        doc["command_template"] = ctmpl;
        doc["max"]  = 24;
        doc["icon"] = "mdi:calendar-check";
        doc["availability_topic"]    = TOPIC_LWT;
        doc["payload_available"]     = "online";
        doc["payload_not_available"] = "offline";
        fillHaSubDevice(doc, subId, subName);
        char payload[640];
        if (!serializeDoc(doc, payload, sizeof(payload))) { ok = false; return; }
        char topic[128];
        snprintf(topic, sizeof(topic), "homeassistant/text/%s/config", uid);
        if (!netMqtt_publish(topic, payload, true)) ok = false;
    };

    // ── Flow calibration (ch1 = Output, ch2 = Input) ─────────────────────────

    struct FlowChDef { const char* suffix; const char* label; const char* subId; int ch; };
    const FlowChDef flowChs[] = {
        { "1", "Output", "twwp_" NODE_ID "_flow1", 1 },
        { "2", "Input",  "twwp_" NODE_ID "_flow2", 2 },
    };

    for (const auto& ch : flowChs) {
        char uid[80], name[80], vk[48], ck[48], pp[64];

        // Reference volume number
        snprintf(uid,  sizeof(uid),  "twwp_" NODE_ID "_cal_ref_vol_%s",      ch.suffix);
        snprintf(name, sizeof(name), "%s Cal Reference Volume",               ch.label);
        snprintf(vk,   sizeof(vk),   "cal_ref_vol_%s",                        ch.suffix);
        snprintf(ck,   sizeof(ck),   "set_cal_ref_vol_%s",                    ch.suffix);
        pubNumber(uid, name, vk, ck, "L", 0.5f, 50.0f, 0.1f, ch.subId, ch.label);

        // Begin button
        snprintf(uid,  sizeof(uid),  "twwp_" NODE_ID "_flow_cal_begin_%s",   ch.suffix);
        snprintf(name, sizeof(name), "Start %s Flow Cal",                     ch.label);
        snprintf(pp,   sizeof(pp),   "{\"flow_cal_begin\": %s}",              ch.suffix);
        pubButton(uid, name, pp, ch.subId, ch.label);

        // Commit button
        snprintf(uid,  sizeof(uid),  "twwp_" NODE_ID "_flow_cal_commit_%s",  ch.suffix);
        snprintf(name, sizeof(name), "Commit %s Flow Cal",                    ch.label);
        snprintf(pp,   sizeof(pp),   "{\"flow_cal_commit\": %s}",             ch.suffix);
        pubButton(uid, name, pp, ch.subId, ch.label);

        // Accept button
        snprintf(uid,  sizeof(uid),  "twwp_" NODE_ID "_flow_cal_accept_%s",  ch.suffix);
        snprintf(name, sizeof(name), "Accept %s Cal K",                       ch.label);
        snprintf(pp,   sizeof(pp),   "{\"flow_cal_accept\": %s}",             ch.suffix);
        pubButton(uid, name, pp, ch.subId, ch.label);

        // Abort button
        snprintf(uid,  sizeof(uid),  "twwp_" NODE_ID "_flow_cal_abort_%s",   ch.suffix);
        snprintf(name, sizeof(name), "Abort %s Flow Cal",                     ch.label);
        snprintf(pp,   sizeof(pp),   "{\"flow_cal_abort\": %s}",              ch.suffix);
        pubButton(uid, name, pp, ch.subId, ch.label);

        // Suggested K diagnostic sensor
        snprintf(uid,  sizeof(uid),  "twwp_" NODE_ID "_cal_suggested_k_%s",  ch.suffix);
        snprintf(name, sizeof(name), "%s Cal Suggested K",                    ch.label);
        snprintf(vk,   sizeof(vk),   "cal_suggested_k_%s",                    ch.suffix);
        pubDiagSensor(uid, name, vk, "pulses/L", ch.subId, ch.label);
    }

    // ── TDS calibration (zone 0 = Pre-RO, zone 1 = Post-RO) ─────────────────

    struct TdsZoneDef { const char* suffix; const char* label; const char* subId; int zone; };
    const TdsZoneDef tdsZones[] = {
        { "pre_ro",  "Pre-RO",  "twwp_" NODE_ID "_tds_pre_ro",  0 },
        { "post_ro", "Post-RO", "twwp_" NODE_ID "_tds_post_ro", 1 },
    };

    for (const auto& z : tdsZones) {
        char uid[80], name[80], vk[64], ck[64], pp[80];

        // EC cal factor number (manual override)
        snprintf(uid,  sizeof(uid),  "twwp_" NODE_ID "_tds_%s_ec_cal_factor",    z.suffix);
        snprintf(name, sizeof(name), "%s EC Cal Factor",                          z.label);
        snprintf(vk,   sizeof(vk),   "tds_%s_ec_cal_factor",                     z.suffix);
        snprintf(ck,   sizeof(ck),   "set_tds_%s_ec_cal_factor",                 z.suffix);
        pubNumber(uid, name, vk, ck, "", 0.5f, 2.0f, 0.001f, z.subId, z.label);

        // Cal reference EC number (stored in sensor RAM)
        snprintf(uid,  sizeof(uid),  "twwp_" NODE_ID "_tds_%s_cal_ref_ec",       z.suffix);
        snprintf(name, sizeof(name), "%s Cal Reference EC",                       z.label);
        snprintf(vk,   sizeof(vk),   "tds_%s_cal_ref_ec",                        z.suffix);
        snprintf(ck,   sizeof(ck),   "set_tds_cal_ref_ec_%d",                    z.zone);
        pubNumber(uid, name, vk, ck, "µS/cm", 1.0f, 50000.0f, 1.0f, z.subId, z.label);

        // Begin button
        snprintf(uid,  sizeof(uid),  "twwp_" NODE_ID "_tds_%s_cal_begin",        z.suffix);
        snprintf(name, sizeof(name), "Start %s EC Cal",                           z.label);
        snprintf(pp,   sizeof(pp),   "{\"tds_cal_begin\": %d}",                  z.zone);
        pubButton(uid, name, pp, z.subId, z.label);

        // Commit button
        snprintf(uid,  sizeof(uid),  "twwp_" NODE_ID "_tds_%s_cal_commit",       z.suffix);
        snprintf(name, sizeof(name), "Commit %s EC Cal",                          z.label);
        snprintf(pp,   sizeof(pp),   "{\"tds_cal_commit\": %d}",                 z.zone);
        pubButton(uid, name, pp, z.subId, z.label);

        // Accept button
        snprintf(uid,  sizeof(uid),  "twwp_" NODE_ID "_tds_%s_cal_accept",       z.suffix);
        snprintf(name, sizeof(name), "Accept %s EC Cal",                          z.label);
        snprintf(pp,   sizeof(pp),   "{\"tds_cal_accept\": %d}",                 z.zone);
        pubButton(uid, name, pp, z.subId, z.label);

        // Abort button
        snprintf(uid,  sizeof(uid),  "twwp_" NODE_ID "_tds_%s_cal_abort",        z.suffix);
        snprintf(name, sizeof(name), "Abort %s EC Cal",                           z.label);
        snprintf(pp,   sizeof(pp),   "{\"tds_cal_abort\": %d}",                  z.zone);
        pubButton(uid, name, pp, z.subId, z.label);

        // Suggested factor diagnostic sensor
        snprintf(uid,  sizeof(uid),  "twwp_" NODE_ID "_tds_%s_cal_suggested_factor", z.suffix);
        snprintf(name, sizeof(name), "%s Cal Suggested Factor",                       z.label);
        snprintf(vk,   sizeof(vk),   "tds_%s_cal_suggested_factor",                  z.suffix);
        pubDiagSensor(uid, name, vk, "", z.subId, z.label);

        // Cal date text
        snprintf(uid,  sizeof(uid),  "twwp_" NODE_ID "_tds_%s_cal_date",         z.suffix);
        snprintf(name, sizeof(name), "%s EC Cal Date",                            z.label);
        snprintf(vk,   sizeof(vk),   "tds_%s_cal_date",                          z.suffix);
        snprintf(ck,   sizeof(ck),   "set_tds_%s_cal_date",                      z.suffix);
        pubCalDateText(uid, name, vk, ck, z.subId, z.label);
    }

    Serial.print("[MQTT] HA calibration discovery ");
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
    publishHaDiscoveryOta();
    publishHaDiscoverySession();
    publishHaDiscoverySessionConfig();
    publishHaDiscoverySessionsRecent();
    publishHaDiscoveryResetButtons();
    publishHaDiscoverySessionSwitch();
    publishHaDiscoveryEfficiency();
    publishHaDiscoveryVoltage();
    publishHaDiscoveryValve();
    publishHaDiscoveryValveConfig();
    publishHaDiscoveryWaterQuality();
    publishHaDiscoveryTdsMeter();
    publishHaDiscoveryWqConfig();
    publishHaDiscoveryCalibration();
    wqConfig_publishState();
    publishM0Status(true, false);
    sessionFlow_republishRecentSessions();
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

    wqConfig_handleCmd(payload);

    if (!doc["set_k_factor_1"].isNull()) {
        sensorFlow_setKFactor(1, doc["set_k_factor_1"].as<float>());
    }
    if (!doc["set_k_factor_2"].isNull()) {
        sensorFlow_setKFactor(2, doc["set_k_factor_2"].as<float>());
    }
    if (doc["restart_device"] | false) {
        storeSd_logEvent("[CMD] restart_device received — rebooting in 2s");
        Serial.println("[CMD] restart_device — rebooting in 2s");
        s_restartMs = millis() + 2000;
    }
    if (doc["restart_wifi"] | false) {
        storeSd_logEvent("[CMD] restart_wifi received");
        netWifi_reconnect();
    }
    if (doc["rotate_upload_token"] | false) {
        if (netAp_rotateUploadToken()) {
            publishM0Status(false, false);
        }
    }
    if (doc["start_ap"] | false) {
        uint32_t duration = static_cast<uint32_t>(constrain(doc["duration_s"] | static_cast<int>(AP_DEFAULT_DURATION_S),
                                                            30, 3600));
        if (!netAp_start(duration)) {
            Serial.println("[CMD] start_ap failed");
        }
    }
    if (doc["reset_flow_today_1"]  | false) sensorFlow_resetToday(1);
    if (doc["reset_flow_today_2"]  | false) sensorFlow_resetToday(2);
    if (doc["reset_flow_today"]    | false) sensorFlow_resetToday(0);
    if (doc["reset_flow_week_1"]   | false) sensorFlow_resetWeek(1);
    if (doc["reset_flow_week_2"]   | false) sensorFlow_resetWeek(2);
    if (doc["reset_flow_week"]     | false) sensorFlow_resetWeek(0);
    if (doc["reset_flow_month_1"]  | false) sensorFlow_resetMonth(1);
    if (doc["reset_flow_month_2"]  | false) sensorFlow_resetMonth(2);
    if (doc["reset_flow_month"]    | false) sensorFlow_resetMonth(0);
    if (doc["reset_flow_year_1"]   | false) sensorFlow_resetYear(1);
    if (doc["reset_flow_year_2"]   | false) sensorFlow_resetYear(2);
    if (doc["reset_flow_year"]     | false) sensorFlow_resetYear(0);
    if (doc["reset_flow_totals_1"] | false) sensorFlow_resetTotals(1);
    if (doc["reset_flow_totals_2"] | false) sensorFlow_resetTotals(2);
    if (doc["reset_flow_totals"]   | false) sensorFlow_resetTotals(0);
    if (doc["factory_reset_flow"]  | false) {
        sensorFlow_factoryReset();
        sessionFlow_factoryReset();
    }
    if (!doc["set_session_enabled"].isNull()) {
        sessionFlow_setEnabled(doc["set_session_enabled"].as<bool>());
    }
    if (!doc["set_session_idle_timeout"].isNull()) {
        uint32_t s = (uint32_t)constrain(doc["set_session_idle_timeout"].as<int>(), 5, 100);
        sessionFlow_setIdleTimeout(s);
    }
    if (!doc["set_flow_threshold"].isNull()) {
        float lpm = constrain(doc["set_flow_threshold"].as<float>(), 0.01f, 0.5f);
        sessionFlow_setFlowThreshold(lpm);
    }
    if (!doc["set_k_table_1"].isNull()) {
        sensorFlow_setKTable(1, doc["set_k_table_1"].as<const char*>());
    }
    if (!doc["set_k_table_2"].isNull()) {
        sensorFlow_setKTable(2, doc["set_k_table_2"].as<const char*>());
    }
    if (!doc["set_debounce_us_1"].isNull()) {
        sensorFlow_setDebounceUs(1, (uint32_t)constrain(doc["set_debounce_us_1"].as<int>(), 100, 10000));
    }
    if (!doc["set_debounce_us_2"].isNull()) {
        sensorFlow_setDebounceUs(2, (uint32_t)constrain(doc["set_debounce_us_2"].as<int>(), 100, 10000));
    }
    if (!doc["set_flow_avg_window"].isNull()) {
        sensorFlow_setFlowAvgWindow((uint8_t)constrain(doc["set_flow_avg_window"].as<int>(), 1, 20));
    }
    if (!doc["set_sensor_model_1"].isNull()) {
        sensorFlow_setModel(1, doc["set_sensor_model_1"].as<const char*>());
    }
    if (!doc["set_sensor_model_2"].isNull()) {
        sensorFlow_setModel(2, doc["set_sensor_model_2"].as<const char*>());
    }
    if (!doc["set_v_min"].isNull()) {
        sensorVoltage_setVMin(doc["set_v_min"].as<float>());
    }
    if (!doc["set_v_max"].isNull()) {
        sensorVoltage_setVMax(doc["set_v_max"].as<float>());
    }
    if (!doc["set_voltage_cal"].isNull()) {
        sensorVoltage_setCalFactor(doc["set_voltage_cal"].as<float>());
    }
    if (!doc["ota_url"].isNull()) {
        const char* url = doc["ota_url"].as<const char*>();
        const char* md5 = doc["ota_md5"].isNull() ? nullptr : doc["ota_md5"].as<const char*>();
        if (!netOta_beginUpdate(url, md5)) {
            Serial.print("[OTA] failed to start update: ");
            Serial.println(netOta_getError() ? netOta_getError() : "unknown");
        }
    }
    {
        static const struct { const char* suffix; uint8_t zone; } wqZones[] = {
            { "pre_ro",  YIERYI_ZONE_PRE_RO  },
            { "post_ro", YIERYI_ZONE_POST_RO },
            { "remin",   YIERYI_ZONE_REMIN   },
        };
        char key[56];
        for (const auto& z : wqZones) {
            snprintf(key, sizeof(key), "set_wq_%s_ph_cal_date", z.suffix);
            if (!doc[key].isNull()) sensorYieryi_setPhCalDate(z.zone, doc[key].as<const char*>());
            snprintf(key, sizeof(key), "set_wq_%s_orp_cal_date", z.suffix);
            if (!doc[key].isNull()) sensorYieryi_setOrpCalDate(z.zone, doc[key].as<const char*>());
            snprintf(key, sizeof(key), "set_wq_%s_ec_cal_date", z.suffix);
            if (!doc[key].isNull()) sensorYieryi_setEcCalDate(z.zone, doc[key].as<const char*>());
        }
    }
    if (!doc["valve_open"].isNull()) {
        actuatorValve_setAuto(false);
        if (doc["valve_open"].as<bool>()) actuatorValve_open();
        else actuatorValve_close();
    }
    if (!doc["valve_auto"].isNull()) {
        actuatorValve_setAuto(doc["valve_auto"].as<bool>());
    }
    if (!doc["set_valve_type"].isNull()) {
        actuatorValve_setValveType(doc["set_valve_type"].as<const char*>());
        actuatorValve_saveToNvs();
    }
    if (!doc["set_trigger_source"].isNull()) {
        actuatorValve_setTriggerSource(doc["set_trigger_source"].as<const char*>());
        actuatorValve_saveToNvs();
    }
    if (!doc["set_valve_idle_timeout"].isNull()) {
        actuatorValve_setIdleTimeoutS(doc["set_valve_idle_timeout"].as<uint32_t>());
        actuatorValve_saveToNvs();
    }
    if (!doc["set_valve_max_open"].isNull()) {
        actuatorValve_setMaxOpenS(doc["set_valve_max_open"].as<uint32_t>());
        actuatorValve_saveToNvs();
    }
    if (!doc["set_valve_timeout_disable_auto"].isNull()) {
        actuatorValve_setTimeoutDisableAuto(doc["set_valve_timeout_disable_auto"].as<bool>());
        actuatorValve_saveToNvs();
    }
    if (!doc["set_valve_timeout_alert"].isNull()) {
        actuatorValve_setTimeoutAlert(doc["set_valve_timeout_alert"].as<bool>());
        actuatorValve_saveToNvs();
    }

    // Flow calibration
    if (!doc["flow_cal_begin"].isNull())  sensorFlow_calBegin(doc["flow_cal_begin"].as<int>());
    if (!doc["flow_cal_commit"].isNull()) sensorFlow_calCommit(doc["flow_cal_commit"].as<int>());
    if (!doc["flow_cal_accept"].isNull()) sensorFlow_calAccept(doc["flow_cal_accept"].as<int>());
    if (!doc["flow_cal_abort"].isNull())  sensorFlow_calAbort(doc["flow_cal_abort"].as<int>());
    if (!doc["set_cal_ref_vol_1"].isNull()) sensorFlow_setCalRefVol(1, doc["set_cal_ref_vol_1"].as<float>());
    if (!doc["set_cal_ref_vol_2"].isNull()) sensorFlow_setCalRefVol(2, doc["set_cal_ref_vol_2"].as<float>());

    // TDS calibration
    if (!doc["set_tds_cal_ref_ec_0"].isNull()) sensorTdsMeter_setCalRefEc(0, doc["set_tds_cal_ref_ec_0"].as<float>());
    if (!doc["set_tds_cal_ref_ec_1"].isNull()) sensorTdsMeter_setCalRefEc(1, doc["set_tds_cal_ref_ec_1"].as<float>());
    if (!doc["tds_cal_begin"].isNull())  sensorTdsMeter_calBegin((uint8_t)doc["tds_cal_begin"].as<int>());
    if (!doc["tds_cal_commit"].isNull()) {
        uint8_t z = (uint8_t)doc["tds_cal_commit"].as<int>();
        sensorTdsMeter_calCommit(z, sensorTdsMeter_getCalRefEc(z));
    }
    if (!doc["tds_cal_accept"].isNull()) sensorTdsMeter_calAccept((uint8_t)doc["tds_cal_accept"].as<int>());
    if (!doc["tds_cal_abort"].isNull())  sensorTdsMeter_calAbort((uint8_t)doc["tds_cal_abort"].as<int>());
    if (!doc["set_tds_pre_ro_ec_cal_factor"].isNull())  sensorTdsMeter_setEcCalFactor(0, doc["set_tds_pre_ro_ec_cal_factor"].as<float>());
    if (!doc["set_tds_post_ro_ec_cal_factor"].isNull()) sensorTdsMeter_setEcCalFactor(1, doc["set_tds_post_ro_ec_cal_factor"].as<float>());
    if (!doc["set_tds_pre_ro_cal_date"].isNull())  sensorTdsMeter_setCalDate(0, doc["set_tds_pre_ro_cal_date"].as<const char*>());
    if (!doc["set_tds_post_ro_cal_date"].isNull()) sensorTdsMeter_setCalDate(1, doc["set_tds_post_ro_cal_date"].as<const char*>());
}

static const char* DATA_LOG_HEADER =
    "ts,"
    "flow_rate_1,flow_total_1,flow_today_1,"
    "flow_rate_2,flow_total_2,flow_today_2,"
    "leak,"
    "supply_voltage,"
    "wq_pre_ro_ph,wq_pre_ro_orp,wq_pre_ro_ec,wq_pre_ro_temp,"
    "wq_post_ro_ph,wq_post_ro_orp,wq_post_ro_ec,wq_post_ro_temp,"
    "wq_remin_ph,wq_remin_orp,wq_remin_ec,wq_remin_temp,"
    "tds_pre_ro_ec,tds_pre_ro_temp,tds_pre_ro_ppm,"
    "tds_post_ro_ec,tds_post_ro_temp,tds_post_ro_ppm";

static void handleDataLog() {
    static unsigned long lastDataLogMs = 0;
    unsigned long now = millis();
    if (now - lastDataLogMs < DATA_LOG_INTERVAL_MS) {
        return;
    }
    lastDataLogMs = now;

    char prePh[16], preOrp[16], preEc[16], preTemp[16];
    char postPh[16], postOrp[16], postEc[16], postTemp[16];
    char reminPh[16], reminOrp[16], reminEc[16], reminTemp[16];
    char tdsP0ec[16], tdsP0tmp[16], tdsP0ppm[16];
    char tdsP1ec[16], tdsP1tmp[16], tdsP1ppm[16];
    bool tdsP0 = sensorTdsMeter_isOnline(TDS_ZONE_PRE_RO);
    bool tdsP1 = sensorTdsMeter_isOnline(TDS_ZONE_POST_RO);

    char row[480];
    snprintf(row, sizeof(row),
        "%lu,"
        "%.3f,%.3f,%.3f,"
        "%.3f,%.3f,%.3f,"
        "%d,"
        "%.3f,"
        "%s,%s,%s,%s,"
        "%s,%s,%s,%s,"
        "%s,%s,%s,%s,"
        "%s,%s,%s,"
        "%s,%s,%s",
        (unsigned long)timeRtc_getUnixTime(),
        sensorFlow_getRateLpm(1), sensorFlow_getTotalL(1), sensorFlow_getTodayL(1),
        sensorFlow_getRateLpm(2), sensorFlow_getTotalL(2), sensorFlow_getTodayL(2),
        sensorLeak_isWet() ? 1 : 0,
        sensorVoltage_getVoltageV(),
        csvFloatOrBlank(prePh, sizeof(prePh), sensorYieryi_hasPh(YIERYI_ZONE_PRE_RO), sensorYieryi_getPh(YIERYI_ZONE_PRE_RO), 2),
        csvIntOrBlank(preOrp, sizeof(preOrp), sensorYieryi_hasOrp(YIERYI_ZONE_PRE_RO), sensorYieryi_getOrpMv(YIERYI_ZONE_PRE_RO)),
        csvFloatOrBlank(preEc, sizeof(preEc), sensorYieryi_hasEc(YIERYI_ZONE_PRE_RO), sensorYieryi_getEcUsCm(YIERYI_ZONE_PRE_RO), 0),
        csvFloatOrBlank(preTemp, sizeof(preTemp), sensorYieryi_hasTemp(YIERYI_ZONE_PRE_RO), sensorYieryi_getTempC(YIERYI_ZONE_PRE_RO), 1),
        csvFloatOrBlank(postPh, sizeof(postPh), sensorYieryi_hasPh(YIERYI_ZONE_POST_RO), sensorYieryi_getPh(YIERYI_ZONE_POST_RO), 2),
        csvIntOrBlank(postOrp, sizeof(postOrp), sensorYieryi_hasOrp(YIERYI_ZONE_POST_RO), sensorYieryi_getOrpMv(YIERYI_ZONE_POST_RO)),
        csvFloatOrBlank(postEc, sizeof(postEc), sensorYieryi_hasEc(YIERYI_ZONE_POST_RO), sensorYieryi_getEcUsCm(YIERYI_ZONE_POST_RO), 0),
        csvFloatOrBlank(postTemp, sizeof(postTemp), sensorYieryi_hasTemp(YIERYI_ZONE_POST_RO), sensorYieryi_getTempC(YIERYI_ZONE_POST_RO), 1),
        csvFloatOrBlank(reminPh, sizeof(reminPh), sensorYieryi_hasPh(YIERYI_ZONE_REMIN), sensorYieryi_getPh(YIERYI_ZONE_REMIN), 2),
        csvIntOrBlank(reminOrp, sizeof(reminOrp), sensorYieryi_hasOrp(YIERYI_ZONE_REMIN), sensorYieryi_getOrpMv(YIERYI_ZONE_REMIN)),
        csvFloatOrBlank(reminEc, sizeof(reminEc), sensorYieryi_hasEc(YIERYI_ZONE_REMIN), sensorYieryi_getEcUsCm(YIERYI_ZONE_REMIN), 0),
        csvFloatOrBlank(reminTemp, sizeof(reminTemp), sensorYieryi_hasTemp(YIERYI_ZONE_REMIN), sensorYieryi_getTempC(YIERYI_ZONE_REMIN), 1),
        csvFloatOrBlank(tdsP0ec,  sizeof(tdsP0ec),  tdsP0, sensorTdsMeter_getEc(TDS_ZONE_PRE_RO),   0),
        csvFloatOrBlank(tdsP0tmp, sizeof(tdsP0tmp), tdsP0, sensorTdsMeter_getTemp(TDS_ZONE_PRE_RO),  1),
        csvFloatOrBlank(tdsP0ppm, sizeof(tdsP0ppm), tdsP0, sensorTdsMeter_getTds(TDS_ZONE_PRE_RO),   0),
        csvFloatOrBlank(tdsP1ec,  sizeof(tdsP1ec),  tdsP1, sensorTdsMeter_getEc(TDS_ZONE_POST_RO),  0),
        csvFloatOrBlank(tdsP1tmp, sizeof(tdsP1tmp), tdsP1, sensorTdsMeter_getTemp(TDS_ZONE_POST_RO), 1),
        csvFloatOrBlank(tdsP1ppm, sizeof(tdsP1ppm), tdsP1, sensorTdsMeter_getTds(TDS_ZONE_POST_RO),  0));

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
    Serial.setTxTimeoutMs(0);  // don't block on TX if no USB host connected
    delay(500);
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
    sessionFlow_begin();
    sensorVoltage_begin();
    rs485Mux_begin();
    sensorYieryi_begin();
    sensorTdsMeter_begin();
    wqConfig_begin();
    sensorPressure_begin();
    sensorTemp_begin();
    displayOled_begin();
    actuatorValve_begin();
    actuatorValve_loadConfig();

    netWifi_begin();
    netAp_begin();
    watchdog_begin();
    netMqtt_begin();
    netOta_begin();
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
    sessionFlow_loop();
    sensorVoltage_loop();
    rs485Mux_loop();
    sensorYieryi_loop();
    sensorTdsMeter_loop();
    sensorPressure_loop();
    sensorTemp_loop();
    displayOled_loop();
    actuatorValve_loop();

    netWifi_loop();
    netAp_loop();
    netMqtt_loop();
    netOta_loop();

    if (netMqtt_takeJustConnected()) {
        publishOnlineState();
    }

    handleLeakTransition();
    handleHeartbeat();
    handleDataLog();
    handleResetCredsButton();
    updateM0Led();

    if (s_restartMs && millis() >= s_restartMs) {
        ESP.restart();
    }
}
