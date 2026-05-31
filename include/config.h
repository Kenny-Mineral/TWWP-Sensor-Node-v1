#pragma once
#include "secrets.h"

// MQTT topics — all use NODE_ID from secrets.h
#define TOPIC_STATUS        "twwp/" NODE_ID "/status"
#define TOPIC_STATUS_DIAG   "twwp/" NODE_ID "/status_diag"
#define TOPIC_STATUS_CFG    "twwp/" NODE_ID "/status_cfg"
#define TOPIC_ALERT         "twwp/" NODE_ID "/alert"
#define TOPIC_LOG           "twwp/" NODE_ID "/log"
#define TOPIC_LWT           "twwp/" NODE_ID "/lwt"
#define TOPIC_CMD           "twwp/" NODE_ID "/cmd"
#define TOPIC_REGISTER      "twwp/register"

// Timing
#define HEARTBEAT_INTERVAL_MS      10000UL
#define DIAG_INTERVAL_MS           60000UL
#define MQTT_RECONNECT_DELAY_MS     5000UL
#define RESET_CREDS_HOLD_MS         5000UL
#define WATCHDOG_TIMEOUT_S            30

// Upload AP / local portal
#define AP_DEFAULT_DURATION_S      300UL
#define AP_AUTO_DURATION_S         600UL
#define AP_AUTO_TRIGGER_LOSS_MS  60000UL
#define AP_AUTO_TRIGGER_RSSI_THRESHOLD -75
#define AP_PORT                       80
#define AP_GATEWAY_IP_STR     "192.168.4.1"
#define SD_UPLOAD_HTML_PATH   "/config/upload.html"
#define SD_UPLOAD_TOKEN_PATH  "/config/upload_token.json"
#define UPLOAD_RELAY_URL      "https://twwp-iot.duckdns.org/api/v1/node-upload"

// SD
#define SD_MAX_BUFFER_LINES         500
#define SD_LOG_DIR                  "/log"
#define SD_BUF_DIR                  "/buf"
#define SD_DATA_DIR                 "/data"
#define SD_CONFIG_PATH              "/config/node.json"
#define SD_CRASH_LOG                "/log/crashes.txt"
#define SD_FLOW_TOTAL_PATH          "/config/flow_total.json"

// Data logging
#define DATA_LOG_INTERVAL_MS        60000UL   // time-series CSV snapshot interval
#define NVS_FLOW_SAVE_INTERVAL_MS   10000UL   // NVS flow total save interval

// Flow sensor calibration
// Ch1 (GPIO4) = DWS-MH-02 — user tap output (RO-to-user)
// Ch2 (GPIO5) = USN-HS06PE — RO output / broader system flow
// Ch3 (GPIO7) = USN-HS06PS — RO input / grey water line
#define FLOW_K_FACTOR_DEFAULT_CH1      780.0f   // DWS-MH-02 mid-range; K-table preferred
#define FLOW_K_FACTOR_DEFAULT_CH2      5500.0f  // USN-HS06PE
#define FLOW_K_FACTOR_DEFAULT_CH3      20700.0f // USN-HS06PS
#define FLOW_K_TABLE_MAX_POINTS        5
#define FLOW_AVG_WINDOW_MAX            20       // maximum moving average window (runtime configurable 1-20)
#define FLOW_AVG_WINDOW_DEFAULT        5
#define FLOW_SENSOR_MODEL_DEFAULT_CH1  "DWS-MH-02"
#define FLOW_SENSOR_MODEL_DEFAULT_CH2  "USN-HS06PE"
#define FLOW_SENSOR_MODEL_DEFAULT_CH3  "USN-HS06PS"
// Flow calibration safety
#define FLOW_CAL_IDLE_TIMEOUT_MS   90000UL  // auto-abort COLLECTING if no pulses for this long
#define FLOW_CAL_MIN_PULSES        100UL    // minimum pulses required to accept a commit
#define FLOW_CAL_ERROR_HOLD_MS     5000UL   // how long error state string persists before clearing

// Session tracking
#define TOPIC_SESSION               "twwp/" NODE_ID "/session"
#define TOPIC_SESSIONS_RECENT       "twwp/" NODE_ID "/sessions_recent"
#define TOPIC_OTA_STATE             "twwp/" NODE_ID "/ota_state"
#define TOPIC_WQ_CONFIG             "twwp/" NODE_ID "/wq_config"
#define SD_SESSION_LOG_PATH         "/log/sessions.csv"
#define SD_SESSIONS_RECENT_PATH     "/config/sessions_recent.json"
#define SESSION_IDLE_TIMEOUT_MS     90000UL
#define FLOW_ACTIVE_THRESHOLD_LPM   0.05f
#define SESSIONS_RECENT_MAX         10

// Calibration sessions
#define TOPIC_CAL_SESSION           "twwp/" NODE_ID "/cal_session"
#define SD_CAL_SESSION_LOG_PATH     "/log/cal_sessions.csv"
#define CAL_SESSION_LOG_HEADER      "ts,type,channel_or_zone,old_value,new_value,ref_value,duration_s"
#define CAL_FAST_PUBLISH_INTERVAL_MS 2000UL

// Tank monitoring (software volume integration)
#define TANK_CAPACITY_DEFAULT_L     19.0f   // 2× 3.2 gal pressure tanks ≈ 24.2L max, usable ~19L
#define TANK_FULL_STOP_THRESHOLD_LPM 0.05f  // Ch2 below this = RO output stopped
#define TANK_FULL_STOP_DURATION_MS  30000UL // flow must be stopped this long before declaring full
#define TANK_FULL_LEVEL_FRACTION    0.90f   // level must be ≥ 90% of capacity to declare full
#define TANK_EMPTY_THRESHOLD_L      0.5f    // level below this → snap to 0 (treat as empty)
#define TANK_NVS_SAVE_INTERVAL_MS   60000UL // persist level to NVS every 60s
#define NVS_TANK_KEY                "tank_l"
#define NVS_TANK_CAPACITY_KEY       "tank_cap_l"

// OTA (M4)
#define OTA_ROLLBACK_TIMEOUT_MS     60000UL
#define OTA_HTTP_TIMEOUT_MS         300000UL
#define OTA_PROGRESS_INTERVAL_MS    2000UL
