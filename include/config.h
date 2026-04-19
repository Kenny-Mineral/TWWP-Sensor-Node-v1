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
#define SD_CONFIG_PATH              "/config/node.json"
#define SD_CRASH_LOG                "/log/crashes.txt"
