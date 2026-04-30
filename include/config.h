#pragma once
#include "secrets.h"

// MQTT topics — all use NODE_ID from secrets.h
#define TOPIC_STATUS    "twwp/" NODE_ID "/status"
#define TOPIC_ALERT     "twwp/" NODE_ID "/alert"
#define TOPIC_LOG       "twwp/" NODE_ID "/log"
#define TOPIC_LWT       "twwp/" NODE_ID "/lwt"
#define TOPIC_CMD       "twwp/" NODE_ID "/cmd"
#define TOPIC_REGISTER  "twwp/register"

// Timing
#define HEARTBEAT_INTERVAL_MS     10000UL
#define MQTT_RECONNECT_DELAY_MS    5000UL
#define RESET_CREDS_HOLD_MS        5000UL
#define WATCHDOG_TIMEOUT_S           30

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
#define FLOW_K_FACTOR_DEFAULT_CH1   5500.0f
#define FLOW_K_FACTOR_DEFAULT_CH2   20700.0f
#define FLOW_K_TABLE_MAX_POINTS     5
#define FLOW_AVG_WINDOW             5

// Session tracking
#define TOPIC_SESSION               "twwp/" NODE_ID "/session"
#define TOPIC_SESSIONS_RECENT       "twwp/" NODE_ID "/sessions_recent"
#define SD_SESSION_LOG_PATH         "/log/sessions.csv"
#define SD_SESSIONS_RECENT_PATH     "/config/sessions_recent.json"
#define SESSION_IDLE_TIMEOUT_MS     90000UL
#define FLOW_ACTIVE_THRESHOLD_LPM   0.05f
#define SESSIONS_RECENT_MAX         10
