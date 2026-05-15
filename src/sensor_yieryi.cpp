#include "sensor_yieryi.h"

#include <ArduinoJson.h>
#include <math.h>

#include "config.h"
#include "pins.h"
#include "rs485_mux.h"
#include "store_sd.h"

static const uint32_t YIERYI_BAUD = 9600;
static const uint32_t YIERYI_DEFAULT_POLL_MS = 15000UL;
static const uint32_t YIERYI_MIN_POLL_MS = 5000UL;
static const uint32_t YIERYI_RESPONSE_TIMEOUT_MS = 700UL;
static const uint32_t YIERYI_MODE_SETTLE_MS = 500UL;
static const uint32_t YIERYI_STALE_MS = 60000UL;
static const uint8_t  YIERYI_READ_LEN          = 16;
static const uint8_t  YIERYI_WRITE_ACK_LEN     = 8;
static const float    YIERYI_PH_MIN            = 0.0f;
static const float    YIERYI_PH_MAX            = 14.0f;
static const int16_t  YIERYI_ORP_MAX_DELTA_MV  = 300;

enum class PollState : uint8_t {
    Idle,
    SendMode,
    WaitModeAck,
    SettleMode,
    SendRead,
    WaitRead
};

enum class MeterMode : uint8_t {
    Ph = 0,
    Orp = 1
};

struct ZoneState {
    const char* key;
    const char* label;
    uint8_t address;
    bool enabled;
    bool readOrp;
    bool phValid;
    bool orpValid;
    bool commonValid;
    float ph;
    int16_t orpMv;
    float ecUsCm;
    float tempC;
    uint16_t humidityPct;
    float tdsPpm;
    unsigned long lastSuccessMs;
    uint16_t failCount;
    char lastError[40];
    char rawHex[64];
    char phCalDate[12];
    char orpCalDate[12];
    char ecCalDate[12];
};

static ZoneState zones[YIERYI_ZONE_COUNT] = {
    { "pre_ro",  "Pre-RO",        1, false, true,  false, false, false, NAN, 0, NAN, NAN, 0, 0.0f, 0, 0, "disabled",   "" },
    { "post_ro", "Post-RO",       2, false, true,  false, false, false, NAN, 0, NAN, NAN, 0, 0.0f, 0, 0, "disabled",   "" },
    { "remin",   "Remineralised", 1, true,  true,  false, false, false, NAN, 0, NAN, NAN, 0, 0.0f, 0, 0, "not polled", "" },
};

static PollState pollState = PollState::Idle;
static uint8_t activeZone = 0;
static MeterMode activeMode = MeterMode::Ph;
static unsigned long stateDeadlineMs = 0;
static unsigned long nextPollMs = 0;
static uint32_t pollIntervalMs = YIERYI_DEFAULT_POLL_MS;
static uint8_t rxBuf[40];
static uint8_t rxLen = 0;
static bool activeJobEndsRound = false;

static bool validZone(uint8_t zone) {
    return zone < YIERYI_ZONE_COUNT;
}

static uint16_t crc16Modbus(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static void appendCrc(uint8_t* frame, size_t lenWithoutCrc) {
    uint16_t crc = crc16Modbus(frame, lenWithoutCrc);
    frame[lenWithoutCrc] = crc & 0xFF;
    frame[lenWithoutCrc + 1] = (crc >> 8) & 0xFF;
}

static void setError(ZoneState& zone, const char* msg) {
    strncpy(zone.lastError, msg, sizeof(zone.lastError) - 1);
    zone.lastError[sizeof(zone.lastError) - 1] = '\0';
}

static void resetRx() {
    rxLen = 0;
    while (rs485Mux_available() > 0) {
        rs485Mux_read();
    }
}

static void readRx() {
    while (rs485Mux_available() > 0) {
        uint8_t b = rs485Mux_read();
        if (rxLen < sizeof(rxBuf)) {
            rxBuf[rxLen++] = b;
        } else {
            memmove(rxBuf, rxBuf + 1, sizeof(rxBuf) - 1);
            rxBuf[sizeof(rxBuf) - 1] = b;
        }
    }
}

static void rawToHex(const uint8_t* data, size_t len, char* out, size_t outLen) {
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 4 < outLen; ++i) {
        int written = snprintf(out + pos, outLen - pos, "%s%02X", i ? " " : "", data[i]);
        if (written <= 0) {
            break;
        }
        pos += static_cast<size_t>(written);
    }
    out[pos < outLen ? pos : outLen - 1] = '\0';
}

static bool findWriteAck(uint8_t address, MeterMode mode) {
    uint16_t value = (mode == MeterMode::Orp) ? 1 : 0;
    for (uint8_t start = 0; start + YIERYI_WRITE_ACK_LEN <= rxLen; ++start) {
        const uint8_t* frame = rxBuf + start;
        if (frame[0] != address || frame[1] != 0x06 || frame[2] != 0x00 || frame[3] != 0x05) {
            continue;
        }
        uint16_t frameValue = (static_cast<uint16_t>(frame[4]) << 8) | frame[5];
        uint16_t gotCrc = static_cast<uint16_t>(frame[6]) | (static_cast<uint16_t>(frame[7]) << 8);
        if (frameValue == value && gotCrc == crc16Modbus(frame, 6)) {
            return true;
        }
    }
    return false;
}

static bool parseReadResponse(ZoneState& zone, MeterMode mode) {
    for (uint8_t start = 0; start + YIERYI_READ_LEN <= rxLen; ++start) {
        const uint8_t* frame = rxBuf + start;
        if (frame[0] != zone.address || frame[1] != 0x03 || frame[2] != 0x00 || frame[3] != 0x08) {
            continue;
        }

        uint16_t gotCrc = static_cast<uint16_t>(frame[14]) | (static_cast<uint16_t>(frame[15]) << 8);
        uint16_t calcCrc = crc16Modbus(frame, 14);
        if (gotCrc != calcCrc) {
            setError(zone, "read crc mismatch");
            continue;
        }

        uint16_t ecRaw = (static_cast<uint16_t>(frame[4]) << 8) | frame[5];
        uint16_t phOrOrpRaw = (static_cast<uint16_t>(frame[6]) << 8) | frame[7];
        uint16_t humRaw = (static_cast<uint16_t>(frame[8]) << 8) | frame[9];
        uint16_t tempRawU = (static_cast<uint16_t>(frame[10]) << 8) | frame[11];
        int16_t signedTempRaw = static_cast<int16_t>(tempRawU);

        zone.ecUsCm = static_cast<float>(ecRaw);
        zone.tempC = static_cast<float>(signedTempRaw) / 10.0f;
        zone.humidityPct = humRaw;
        zone.tdsPpm = zone.ecUsCm * 0.5f;
        zone.commonValid = true;
        bool valueAccepted = false;
        if (mode == MeterMode::Orp) {
            // Bit 15 = sign indicator (1=positive, 0=negative), bits 14:0 = magnitude in mV
            bool positive = (phOrOrpRaw & 0x8000) != 0;
            int16_t parsedOrp = positive ? static_cast<int16_t>(phOrOrpRaw & 0x7FFF)
                                         : -static_cast<int16_t>(phOrOrpRaw & 0x7FFF);
            bool orpOk = true;
            if (zone.orpValid) {
                int16_t delta = parsedOrp > zone.orpMv ? parsedOrp - zone.orpMv
                                                       : zone.orpMv - parsedOrp;
                if (delta > YIERYI_ORP_MAX_DELTA_MV) {
                    setError(zone, "orp spike rejected");
                    orpOk = false;
                }
            }
            if (orpOk) {
                zone.orpMv    = parsedOrp;
                zone.orpValid = true;
                valueAccepted = true;
            }
        } else {
            float parsedPh = static_cast<float>(phOrOrpRaw) / 100.0f;
            if (parsedPh < YIERYI_PH_MIN || parsedPh > YIERYI_PH_MAX) {
                setError(zone, "ph out of range");
            } else {
                zone.ph       = parsedPh;
                zone.phValid  = true;
                valueAccepted = true;
            }
        }
        zone.lastSuccessMs = millis();
        if (valueAccepted) setError(zone, "ok");
        rawToHex(frame, YIERYI_READ_LEN, zone.rawHex, sizeof(zone.rawHex));
        return true;
    }
    return false;
}

static void sendModeCommand(ZoneState& zone, MeterMode mode) {
    uint8_t frame[8] = { zone.address, 0x06, 0x00, 0x05, 0x00, static_cast<uint8_t>(mode == MeterMode::Orp ? 0x01 : 0x00), 0x00, 0x00 };
    appendCrc(frame, 6);
    resetRx();
    rs485Mux_write(frame, sizeof(frame));
}

static void sendReadCommand(ZoneState& zone) {
    uint8_t frame[8] = { zone.address, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00 };
    appendCrc(frame, 6);
    resetRx();
    rs485Mux_write(frame, sizeof(frame));
}

static bool chooseNextJob(uint8_t& zoneOut, MeterMode& modeOut) {
    static uint8_t nextZone = 0;
    static bool nextOrp = false;
    static uint8_t jobsRemainingInRound = 0;

    if (jobsRemainingInRound == 0) {
        for (uint8_t i = 0; i < YIERYI_ZONE_COUNT; ++i) {
            if (zones[i].enabled) {
                jobsRemainingInRound += zones[i].readOrp ? 2 : 1;
            }
        }
        if (jobsRemainingInRound == 0) {
            activeJobEndsRound = true;
            return false;
        }
    }

    for (uint8_t tries = 0; tries < YIERYI_ZONE_COUNT * 2; ++tries) {
        ZoneState& zone = zones[nextZone];
        uint8_t candidateZone = nextZone;
        bool candidateOrp = nextOrp;

        if (zone.enabled) {
            if (!candidateOrp) {
                nextOrp = zone.readOrp;
                --jobsRemainingInRound;
                activeJobEndsRound = (jobsRemainingInRound == 0);
                zoneOut = candidateZone;
                modeOut = MeterMode::Ph;
                return true;
            }
            nextOrp = false;
            nextZone = (nextZone + 1) % YIERYI_ZONE_COUNT;
            --jobsRemainingInRound;
            activeJobEndsRound = (jobsRemainingInRound == 0);
            zoneOut = candidateZone;
            modeOut = MeterMode::Orp;
            return true;
        }

        nextOrp = false;
        nextZone = (nextZone + 1) % YIERYI_ZONE_COUNT;
    }
    return false;
}

static void loadConfig() {
    JsonDocument doc;
    if (!storeSd_readJsonFile(SD_CONFIG_PATH, doc)) {
        return;
    }

    JsonVariantConst wq = doc["water_quality"];
    if (wq.isNull()) {
        return;
    }

    uint32_t configuredPoll = wq["poll_interval_ms"] | pollIntervalMs;
    pollIntervalMs = max(YIERYI_MIN_POLL_MS, configuredPoll);
    bool defaultReadOrp = wq["read_orp"] | true;

    for (uint8_t i = 0; i < YIERYI_ZONE_COUNT; ++i) {
        JsonVariantConst zoneCfg = wq[zones[i].key];
        if (zoneCfg.isNull()) {
            zones[i].readOrp = defaultReadOrp;
            continue;
        }
        zones[i].enabled = zoneCfg["enabled"] | zones[i].enabled;
        zones[i].address = zoneCfg["address"] | zones[i].address;
        zones[i].readOrp = zoneCfg["read_orp"] | defaultReadOrp;
        if (!zones[i].enabled) {
            setError(zones[i], "disabled");
        }
        const char* phCal  = zoneCfg["ph_cal_date"]  | "";
        const char* orpCal = zoneCfg["orp_cal_date"] | "";
        const char* ecCal  = zoneCfg["ec_cal_date"]  | "";
        strncpy(zones[i].phCalDate,  phCal,  sizeof(zones[i].phCalDate)  - 1);
        strncpy(zones[i].orpCalDate, orpCal, sizeof(zones[i].orpCalDate) - 1);
        strncpy(zones[i].ecCalDate,  ecCal,  sizeof(zones[i].ecCalDate)  - 1);
    }
}

static bool saveCalDate(uint8_t zone, const char* paramKey, const char* date) {
    JsonDocument doc;
    storeSd_readJsonFile(SD_CONFIG_PATH, doc);
    JsonObject wq = doc["water_quality"].as<JsonObject>();
    if (wq.isNull()) wq = doc["water_quality"].to<JsonObject>();
    JsonObject zoneCfg = wq[zones[zone].key].as<JsonObject>();
    if (zoneCfg.isNull()) zoneCfg = wq[zones[zone].key].to<JsonObject>();
    zoneCfg[paramKey] = date;
    return storeSd_writeJsonFile(SD_CONFIG_PATH, doc);
}

bool sensorYieryi_begin() {
    loadConfig();
    nextPollMs = millis() + 1000UL;
    Serial.println("[YIERYI] Modbus driver ready (UART via rs485_mux)");
    return true;
}

void sensorYieryi_forcePoll() {
    nextPollMs = millis();
}

void sensorYieryi_loop() {
    unsigned long now = millis();
    readRx();

    switch (pollState) {
        case PollState::Idle: {
            if ((long)(now - nextPollMs) < 0) {
                return;
            }
            if (!chooseNextJob(activeZone, activeMode)) {
                nextPollMs = now + pollIntervalMs;
                return;
            }
            pollState = PollState::SendMode;
            break;
        }

        case PollState::SendMode:
            sendModeCommand(zones[activeZone], activeMode);
            stateDeadlineMs = now + YIERYI_RESPONSE_TIMEOUT_MS;
            pollState = PollState::WaitModeAck;
            break;

        case PollState::WaitModeAck:
            if (findWriteAck(zones[activeZone].address, activeMode) || (long)(now - stateDeadlineMs) >= 0) {
                if ((long)(now - stateDeadlineMs) >= 0) {
                    setError(zones[activeZone], "mode ack timeout");
                }
                stateDeadlineMs = now + YIERYI_MODE_SETTLE_MS;
                pollState = PollState::SettleMode;
            }
            break;

        case PollState::SettleMode:
            if ((long)(now - stateDeadlineMs) >= 0) {
                pollState = PollState::SendRead;
            }
            break;

        case PollState::SendRead:
            sendReadCommand(zones[activeZone]);
            stateDeadlineMs = now + YIERYI_RESPONSE_TIMEOUT_MS;
            pollState = PollState::WaitRead;
            break;

        case PollState::WaitRead:
            if (parseReadResponse(zones[activeZone], activeMode)) {
                pollState = PollState::Idle;
                nextPollMs = now + (activeJobEndsRound ? pollIntervalMs : 20UL);
            } else if ((long)(now - stateDeadlineMs) >= 0) {
                ++zones[activeZone].failCount;
                if (strcmp(zones[activeZone].lastError, "read crc mismatch") != 0) {
                    setError(zones[activeZone], "read timeout");
                }
                pollState = PollState::Idle;
                nextPollMs = now + (activeJobEndsRound ? pollIntervalMs : 20UL);
            }
            break;
    }
}

bool sensorYieryi_isEnabled(uint8_t zone) {
    return validZone(zone) && zones[zone].enabled;
}

bool sensorYieryi_isOnline(uint8_t zone) {
    if (!validZone(zone) || !zones[zone].enabled || zones[zone].lastSuccessMs == 0) {
        return false;
    }
    return millis() - zones[zone].lastSuccessMs <= YIERYI_STALE_MS;
}

bool sensorYieryi_hasPh(uint8_t zone) {
    return validZone(zone) && zones[zone].phValid && sensorYieryi_isOnline(zone);
}

bool sensorYieryi_hasOrp(uint8_t zone) {
    return validZone(zone) && zones[zone].orpValid && sensorYieryi_isOnline(zone);
}

bool sensorYieryi_hasEc(uint8_t zone) {
    return validZone(zone) && zones[zone].commonValid && sensorYieryi_isOnline(zone);
}

bool sensorYieryi_hasTemp(uint8_t zone) {
    return validZone(zone) && zones[zone].commonValid && sensorYieryi_isOnline(zone);
}

float sensorYieryi_getPh(uint8_t zone) {
    return validZone(zone) ? zones[zone].ph : NAN;
}

int16_t sensorYieryi_getOrpMv(uint8_t zone) {
    return validZone(zone) ? zones[zone].orpMv : 0;
}

float sensorYieryi_getEcUsCm(uint8_t zone) {
    return validZone(zone) ? zones[zone].ecUsCm : NAN;
}

float sensorYieryi_getTempC(uint8_t zone) {
    return validZone(zone) ? zones[zone].tempC : NAN;
}

uint16_t sensorYieryi_getHumidityPct(uint8_t zone) {
    return validZone(zone) ? zones[zone].humidityPct : 0;
}

float sensorYieryi_getTdsPpm(uint8_t zone) {
    return validZone(zone) ? zones[zone].tdsPpm : NAN;
}

const char* sensorYieryi_getPhCalDate(uint8_t zone) {
    return validZone(zone) ? zones[zone].phCalDate : "";
}
const char* sensorYieryi_getOrpCalDate(uint8_t zone) {
    return validZone(zone) ? zones[zone].orpCalDate : "";
}
const char* sensorYieryi_getEcCalDate(uint8_t zone) {
    return validZone(zone) ? zones[zone].ecCalDate : "";
}

bool sensorYieryi_setPhCalDate(uint8_t zone, const char* date) {
    if (!validZone(zone) || !date) return false;
    strncpy(zones[zone].phCalDate, date, sizeof(zones[zone].phCalDate) - 1);
    zones[zone].phCalDate[sizeof(zones[zone].phCalDate) - 1] = '\0';
    return saveCalDate(zone, "ph_cal_date", date);
}
bool sensorYieryi_setOrpCalDate(uint8_t zone, const char* date) {
    if (!validZone(zone) || !date) return false;
    strncpy(zones[zone].orpCalDate, date, sizeof(zones[zone].orpCalDate) - 1);
    zones[zone].orpCalDate[sizeof(zones[zone].orpCalDate) - 1] = '\0';
    return saveCalDate(zone, "orp_cal_date", date);
}
bool sensorYieryi_setEcCalDate(uint8_t zone, const char* date) {
    if (!validZone(zone) || !date) return false;
    strncpy(zones[zone].ecCalDate, date, sizeof(zones[zone].ecCalDate) - 1);
    zones[zone].ecCalDate[sizeof(zones[zone].ecCalDate) - 1] = '\0';
    return saveCalDate(zone, "ec_cal_date", date);
}

uint32_t sensorYieryi_getLastSuccessAgeMs(uint8_t zone) {
    if (!validZone(zone) || zones[zone].lastSuccessMs == 0) {
        return UINT32_MAX;
    }
    return millis() - zones[zone].lastSuccessMs;
}

uint16_t sensorYieryi_getFailCount(uint8_t zone) {
    return validZone(zone) ? zones[zone].failCount : 0;
}

const char* sensorYieryi_getLastError(uint8_t zone) {
    return validZone(zone) ? zones[zone].lastError : "invalid zone";
}

const char* sensorYieryi_getRawHex(uint8_t zone) {
    return validZone(zone) ? zones[zone].rawHex : "";
}

void sensorYieryi_printStatus(Print& out) {
    out.println("[YIERYI] status");
    for (uint8_t i = 0; i < YIERYI_ZONE_COUNT; ++i) {
        ZoneState& z = zones[i];
        out.printf("  %s addr=%u enabled=%u online=%u fail=%u err=%s raw=%s\n",
                   z.label, z.address, z.enabled ? 1 : 0, sensorYieryi_isOnline(i) ? 1 : 0,
                   z.failCount, z.lastError, z.rawHex);
        out.printf("    ph=%s%.2f orp=%s%d ec=%s%.0f tds=%s%.1f temp=%s%.1f hum=%u\n",
                   sensorYieryi_hasPh(i) ? "" : "stale/",
                   z.ph,
                   sensorYieryi_hasOrp(i) ? "" : "stale/",
                   z.orpMv,
                   sensorYieryi_hasEc(i) ? "" : "stale/",
                   z.ecUsCm,
                   sensorYieryi_hasEc(i) ? "" : "stale/",
                   z.tdsPpm,
                   sensorYieryi_hasTemp(i) ? "" : "stale/",
                   z.tempC,
                   z.humidityPct);
    }
}
