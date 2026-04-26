#include "store_sd.h"

#ifndef DISABLE_FS_H_WARNING
#define DISABLE_FS_H_WARNING
#endif

#include <ArduinoJson.h>
#include <SPI.h>
#include <SdFat.h>

#include "config.h"
#include "net_mqtt.h"
#include "pins.h"
#include "time_rtc.h"

static SPIClass sdSpi(HSPI);
static SdFs sd;
static bool sdReady = false;
static uint32_t nextBufferSeq = 0;

static bool parseSeqFromName(const char* name, uint32_t& seqOut) {
    const char* dot = strrchr(name, '.');
    if (dot == nullptr || strcmp(dot, ".json") != 0) {
        return false;
    }

    char buf[32];
    size_t len = static_cast<size_t>(dot - name);
    if (len == 0 || len >= sizeof(buf)) {
        return false;
    }

    memcpy(buf, name, len);
    buf[len] = '\0';

    char* end = nullptr;
    unsigned long seq = strtoul(buf, &end, 10);
    if (end == buf || *end != '\0') {
        return false;
    }

    seqOut = static_cast<uint32_t>(seq);
    return true;
}

static bool findOldestBufferFile(char* pathOut, size_t pathLen, uint32_t& seqOut) {
    FsFile dir;
    FsFile entry;
    char name[64];
    bool found = false;
    uint32_t bestSeq = 0;

    if (!dir.open(SD_BUF_DIR)) {
        return false;
    }

    dir.rewind();
    while (entry.openNext(&dir, O_RDONLY)) {
        if (!entry.isDir()) {
            if (entry.getName(name, sizeof(name)) > 0) {
                uint32_t seq = 0;
                if (parseSeqFromName(name, seq)) {
                    if (!found || seq < bestSeq) {
                        bestSeq = seq;
                        if (snprintf(pathOut, pathLen, "%s/%s", SD_BUF_DIR, name) < 0) {
                            entry.close();
                            dir.close();
                            return false;
                        }
                        found = true;
                    }
                }
            }
        }
        entry.close();
    }

    dir.close();

    if (found) {
        seqOut = bestSeq;
    }
    return found;
}

static uint32_t countBufferFilesAndMaxSeq(void) {
    FsFile dir;
    FsFile entry;
    char name[64];
    uint32_t count = 0;
    uint32_t maxSeq = 0;

    if (!dir.open(SD_BUF_DIR)) {
        return 0;
    }

    dir.rewind();
    while (entry.openNext(&dir, O_RDONLY)) {
        if (!entry.isDir()) {
            if (entry.getName(name, sizeof(name)) > 0) {
                uint32_t seq = 0;
                if (parseSeqFromName(name, seq)) {
                    ++count;
                    if (seq >= maxSeq) {
                        maxSeq = seq + 1;
                    }
                }
            }
        }
        entry.close();
    }

    dir.close();
    nextBufferSeq = maxSeq;
    return count;
}

bool storeSd_begin() {
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);

    sdSpi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);

    if (!sd.begin(SdSpiConfig(PIN_SD_CS, DEDICATED_SPI, SD_SCK_MHZ(4), &sdSpi))) {
        Serial.println("[SD] mount failed");
        sdReady = false;
        return false;
    }

    if (!sd.exists(SD_LOG_DIR) && !sd.mkdir(SD_LOG_DIR)) {
        Serial.println("[SD] failed to create log dir");
        sdReady = false;
        return false;
    }

    if (!sd.exists(SD_BUF_DIR) && !sd.mkdir(SD_BUF_DIR)) {
        Serial.println("[SD] failed to create buf dir");
        sdReady = false;
        return false;
    }

    countBufferFilesAndMaxSeq();
    sdReady = true;
    return true;
}

void storeSd_loop() {
}

bool storeSd_logEvent(const char* msg) {
    if (!sdReady) {
        return false;
    }

    String path = String(SD_LOG_DIR) + "/" + timeRtc_getDateString() + ".csv";
    FsFile file;
    if (!file.open(path.c_str(), FILE_WRITE)) {
        return false;
    }

    file.print(timeRtc_getUnixTime());
    file.print(',');
    file.println(msg);
    file.close();
    return true;
}

bool storeSd_bufferMessage(const char* topic, const char* payload) {
    if (!sdReady) {
        return false;
    }

    uint32_t count = countBufferFilesAndMaxSeq();
    while (count >= SD_MAX_BUFFER_LINES) {
        char oldestPath[96];
        uint32_t oldestSeq = 0;
        if (!findOldestBufferFile(oldestPath, sizeof(oldestPath), oldestSeq)) {
            break;
        }
        if (!sd.remove(oldestPath)) {
            break;
        }
        if (count > 0) {
            --count;
        } else {
            break;
        }
    }

    char path[96];
    snprintf(path, sizeof(path), "%s/%010lu.json", SD_BUF_DIR,
             static_cast<unsigned long>(nextBufferSeq++));

    JsonDocument doc;
    doc["t"] = topic;
    doc["p"] = payload;

    FsFile file;
    if (!file.open(path, FILE_WRITE)) {
        return false;
    }

    if (serializeJson(doc, file) == 0) {
        file.close();
        sd.remove(path);
        return false;
    }

    file.println();
    file.close();
    return true;
}

bool storeSd_drainBuffer(uint8_t maxMessages) {
    if (!sdReady) {
        return false;
    }

    bool allOk = true;
    uint8_t processed = 0;

    while (processed < maxMessages) {
        char path[96];
        uint32_t seq = 0;
        if (!findOldestBufferFile(path, sizeof(path), seq)) {
            break;
        }

        FsFile file;
        if (!file.open(path, O_RDONLY)) {
            allOk = false;
            break;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, file);
        file.close();

        if (err) {
            Serial.print("[SD] dropping invalid buffer file ");
            Serial.println(path);
            sd.remove(path);
            ++processed;
            continue;
        }

        const char* topic = doc["t"] | "";
        const char* payload = doc["p"] | "";

        if (!netMqtt_publish(topic, payload, false)) {
            allOk = false;
            break;
        }

        if (!sd.remove(path)) {
            allOk = false;
            break;
        }

        ++processed;
    }

    return allOk;
}
