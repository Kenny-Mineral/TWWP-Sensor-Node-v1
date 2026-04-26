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
static uint16_t sdRetentionDays = 0;
static bool sdAutoPrune = false;
static bool sdSerialCommandsEnabled = true;

static bool parseLogDateFromName(const char* name, int& year, int& month, int& day) {
    if (strlen(name) != 14 || strcmp(name + 10, ".csv") != 0) {
        return false;
    }

    if (name[4] != '-' || name[7] != '-') {
        return false;
    }

    for (uint8_t i = 0; i < 10; ++i) {
        if (i == 4 || i == 7) {
            continue;
        }
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
    }

    year = atoi(name);
    month = atoi(name + 5);
    day = atoi(name + 8);
    return year >= 2024 && month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

static int32_t daysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

static void loadSdConfig() {
    sdRetentionDays = 0;
    sdAutoPrune = false;
    sdSerialCommandsEnabled = true;

    FsFile file;
    if (!file.open(SD_CONFIG_PATH, O_RDONLY)) {
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        Serial.print("[SD] config read failed: ");
        Serial.println(err.c_str());
        return;
    }

    sdRetentionDays = doc["sd"]["retention_days"] | 0;
    sdAutoPrune = doc["sd"]["auto_prune"] | false;
    sdSerialCommandsEnabled = doc["sd"]["serial_commands_enabled"] | true;
}

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

    if (!sd.exists("/config") && !sd.mkdir("/config")) {
        Serial.println("[SD] failed to create config dir");
    }

    loadSdConfig();
    countBufferFilesAndMaxSeq();
    sdReady = true;

    if (sdAutoPrune) {
        storeSd_pruneLogs(Serial);
    }

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

bool storeSd_printDirectory(const char* path, Print& out) {
    if (!sdReady) {
        out.println("[SD] not mounted");
        return false;
    }
    if (!sdSerialCommandsEnabled) {
        out.println("[SD] serial commands disabled");
        return false;
    }

    FsFile dir;
    FsFile entry;
    char name[64];

    if (!dir.open(path, O_RDONLY) || !dir.isDir()) {
        out.print("[SD] directory not found: ");
        out.println(path);
        return false;
    }

    out.print("[SD] listing ");
    out.println(path);
    dir.rewind();
    while (entry.openNext(&dir, O_RDONLY)) {
        if (entry.getName(name, sizeof(name)) > 0) {
            out.print(entry.isDir() ? "dir  " : "file ");
            out.print(name);
            if (!entry.isDir()) {
                out.print(" ");
                out.print(entry.fileSize());
                out.print(" bytes");
            }
            out.println();
        }
        entry.close();
        delay(0);
    }

    dir.close();
    return true;
}

bool storeSd_printFile(const char* path, Print& out) {
    if (!sdReady) {
        out.println("[SD] not mounted");
        return false;
    }
    if (!sdSerialCommandsEnabled) {
        out.println("[SD] serial commands disabled");
        return false;
    }

    FsFile file;
    if (!file.open(path, O_RDONLY) || file.isDir()) {
        out.print("[SD] file not found: ");
        out.println(path);
        return false;
    }

    out.print("[SD] begin ");
    out.println(path);

    char buf[96];
    int bytesRead = 0;
    char lastChar = '\0';
    while ((bytesRead = file.read(buf, sizeof(buf))) > 0) {
        out.write(reinterpret_cast<const uint8_t*>(buf), bytesRead);
        lastChar = buf[bytesRead - 1];
        delay(0);
    }

    if (bytesRead < 0) {
        out.println();
        out.print("[SD] read failed: ");
        out.println(path);
        file.close();
        return false;
    }

    if (file.fileSize() == 0 || lastChar != '\n') {
        out.println();
    }
    out.print("[SD] end ");
    out.println(path);
    file.close();
    return true;
}

bool storeSd_removePath(const char* path, Print& out) {
    if (!sdReady) {
        out.println("[SD] not mounted");
        return false;
    }
    if (!sdSerialCommandsEnabled) {
        out.println("[SD] serial commands disabled");
        return false;
    }
    if (strcmp(path, "/") == 0 || strcmp(path, SD_LOG_DIR) == 0 ||
        strcmp(path, SD_BUF_DIR) == 0 || strcmp(path, "/config") == 0) {
        out.print("[SD] refusing to remove protected path: ");
        out.println(path);
        return false;
    }

    FsFile file;
    if (!file.open(path, O_RDONLY)) {
        out.print("[SD] path not found: ");
        out.println(path);
        return false;
    }
    bool isDir = file.isDir();
    file.close();

    bool ok = isDir ? sd.rmdir(path) : sd.remove(path);
    out.print(ok ? "[SD] removed " : "[SD] remove failed: ");
    out.println(path);
    return ok;
}

bool storeSd_printInfo(Print& out) {
    if (!sdReady) {
        out.println("[SD] not mounted");
        return false;
    }

    uint32_t bufferCount = countBufferFilesAndMaxSeq();
    out.println("[SD] info");
    out.print("ready: yes\nretention_days: ");
    out.println(sdRetentionDays);
    out.print("auto_prune: ");
    out.println(sdAutoPrune ? "on" : "off");
    out.print("serial_commands: ");
    out.println(sdSerialCommandsEnabled ? "on" : "off");
    out.print("buffer_count: ");
    out.println(bufferCount);
    out.print("next_buffer_seq: ");
    out.println(nextBufferSeq);
    return true;
}

uint16_t storeSd_pruneLogs(Print& out) {
    if (!sdReady) {
        out.println("[SD] not mounted");
        return 0;
    }
    if (sdRetentionDays == 0) {
        out.println("[SD] retention disabled");
        return 0;
    }

    uint32_t nowUnix = timeRtc_getUnixTime();
    if (nowUnix == 0) {
        out.println("[SD] retention skipped: RTC not valid");
        return 0;
    }

    int32_t currentDay = static_cast<int32_t>(nowUnix / 86400UL);
    uint16_t removed = 0;
    FsFile dir;
    FsFile entry;
    char name[64];

    if (!dir.open(SD_LOG_DIR, O_RDONLY) || !dir.isDir()) {
        out.println("[SD] retention skipped: log dir missing");
        return 0;
    }

    dir.rewind();
    while (entry.openNext(&dir, O_RDONLY)) {
        if (!entry.isDir() && entry.getName(name, sizeof(name)) > 0) {
            int year = 0;
            int month = 0;
            int day = 0;
            if (parseLogDateFromName(name, year, month, day)) {
                int32_t fileDay = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
                if (currentDay - fileDay > static_cast<int32_t>(sdRetentionDays)) {
                    char path[96];
                    snprintf(path, sizeof(path), "%s/%s", SD_LOG_DIR, name);
                    entry.close();
                    if (sd.remove(path)) {
                        ++removed;
                        out.print("[SD] pruned ");
                        out.println(path);
                    }
                    delay(0);
                    continue;
                }
            }
        }
        entry.close();
        delay(0);
    }

    dir.close();
    out.print("[SD] prune complete, removed ");
    out.println(removed);
    return removed;
}
