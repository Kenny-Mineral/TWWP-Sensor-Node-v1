#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

struct StoreSdBufferStats {
    uint32_t count = 0;
    uint32_t oldestTs = 0;
    uint32_t newestTs = 0;
    uint32_t estBytes = 0;
};

bool storeSd_begin();
void storeSd_loop();
bool storeSd_logEvent(const char* msg);           // Append to daily CSV
bool storeSd_bufferMessage(const char* topic, const char* payload); // Ring-buffer for offline MQTT
bool storeSd_drainBuffer(uint8_t maxMessages = 10); // Drain buffered msgs to MQTT
bool storeSd_printDirectory(const char* path, Print& out);
bool storeSd_printFile(const char* path, Print& out);
bool storeSd_removePath(const char* path, Print& out);
bool storeSd_printInfo(Print& out);
uint16_t storeSd_pruneLogs(Print& out);
uint32_t storeSd_bufferCount();
bool storeSd_getBufferStats(StoreSdBufferStats& stats);
bool storeSd_fetchOldestBufferJson(uint16_t maxMessages, String& outJson);
uint16_t storeSd_ackOldestBuffer(uint16_t count);
bool storeSd_readJsonFile(const char* path, JsonDocument& doc);
bool storeSd_writeJsonFile(const char* path, JsonDocument& doc);
bool storeSd_readTextFile(const char* path, String& outText);
bool storeSd_writeTextFile(const char* path, const char* text);
bool storeSd_deleteFile(const char* path);
bool storeSd_logDataRow(const char* row, const char* header);
bool storeSd_appendCsvRow(const char* path, const char* row, const char* header);
