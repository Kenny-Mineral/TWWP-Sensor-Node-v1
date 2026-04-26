#include "time_rtc.h"

#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <RTClib.h>

#include "config.h"
#include "pins.h"

static RTC_DS3231 rtc;
static WiFiUDP ntpUdp;
static NTPClient ntpClient(ntpUdp, "pool.ntp.org", 0, 60000UL);
static bool rtcReady = false;
static bool ntpStarted = false;
static bool ntpSynced = false;
static unsigned long nextSyncAttemptMs = 0;

static bool isValidDateTime(const DateTime& dt) {
    return dt.year() >= 2024 &&
           dt.month() >= 1 && dt.month() <= 12 &&
           dt.day() >= 1 && dt.day() <= 31 &&
           dt.hour() <= 23 &&
           dt.minute() <= 59 &&
           dt.second() <= 59;
}

bool timeRtc_begin() {
    rtcReady = rtc.begin(&Wire);
    if (!rtcReady) {
        Serial.println("[RTC] DS3231 not found — scanning I2C bus:");
        Wire.setTimeOut(10);
        for (uint8_t addr = 1; addr < 127; addr++) {
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0) {
                Serial.printf("[RTC]   device at 0x%02X\r\n", addr);
            }
        }
        return false;
    }

    nextSyncAttemptMs = 0;

    if (rtc.lostPower()) {
        Serial.println("[RTC] DS3231 lost power — setting time to compile time");
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    return true;
}

void timeRtc_loop() {
    if (!rtcReady) {
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    if (!ntpStarted) {
        ntpClient.begin();
        ntpStarted = true;
        nextSyncAttemptMs = 0;
    }

    if (!ntpSynced) {
        if (millis() < nextSyncAttemptMs) {
            return;
        }

        if (!ntpClient.forceUpdate()) {
            Serial.println("[RTC] NTP sync failed");
            nextSyncAttemptMs = millis() + 30000UL;
            return;
        }

        uint32_t ntpUnix = ntpClient.getEpochTime();
        uint32_t rtcUnix = rtc.now().unixtime();
        long drift = (long)((int64_t)ntpUnix - (int64_t)rtcUnix);

        if (labs(drift) > 2) {
            Serial.print("[RTC] adjusting DS3231 from ");
            Serial.print(rtcUnix);
            Serial.print(" to ");
            Serial.print(ntpUnix);
            Serial.print(" (drift ");
            Serial.print(drift);
            Serial.println(" s)");
            rtc.adjust(DateTime(ntpUnix));
        } else {
            Serial.print("[RTC] NTP sync ok (drift ");
            Serial.print(drift);
            Serial.println(" s)");
        }

        ntpSynced = true;
        nextSyncAttemptMs = millis() + 60000UL;
        return;
    }

    if (ntpClient.update()) {
        uint32_t ntpUnix = ntpClient.getEpochTime();
        uint32_t rtcUnix = rtc.now().unixtime();
        long drift = (long)((int64_t)ntpUnix - (int64_t)rtcUnix);

        if (labs(drift) > 2) {
            Serial.print("[RTC] correcting RTC drift: ");
            Serial.print(rtcUnix);
            Serial.print(" -> ");
            Serial.print(ntpUnix);
            Serial.print(" (");
            Serial.print(drift);
            Serial.println(" s)");
            rtc.adjust(DateTime(ntpUnix));
        }
    }
}

String timeRtc_getISOTimestamp() {
    if (!rtcReady) {
        return "1970-01-01T00:00:00Z";
    }

    DateTime now = rtc.now();
    if (!isValidDateTime(now)) {
        return "1970-01-01T00:00:00Z";
    }

    char buf[21];
    snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02uZ",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    return String(buf);
}

String timeRtc_getDateString() {
    if (!rtcReady) {
        return "1970-01-01";
    }

    DateTime now = rtc.now();
    if (!isValidDateTime(now)) {
        return "unsynced";
    }

    char buf[11];
    snprintf(buf, sizeof(buf), "%04u-%02u-%02u",
             now.year(), now.month(), now.day());
    return String(buf);
}

uint32_t timeRtc_getUnixTime() {
    if (!rtcReady) {
        return 0;
    }

    DateTime now = rtc.now();
    if (!isValidDateTime(now)) {
        return 0;
    }

    return now.unixtime();
}

bool timeRtc_isSynced() {
    return ntpSynced;
}
