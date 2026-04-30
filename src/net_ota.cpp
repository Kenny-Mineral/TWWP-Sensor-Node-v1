#include "net_ota.h"

#include <Preferences.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ctype.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <mbedtls/md5.h>
#include <stdarg.h>

#include "config.h"
#include "store_sd.h"
#include "time_rtc.h"
#include "watchdog.h"

namespace {

static const char* OTA_NVS_NAMESPACE = "ota";
static const char* OTA_KEY_BOOT_PENDING = "boot_pend";
static const char* OTA_KEY_BOOT_TS = "boot_ts";

static const uint8_t OTA_BOOT_NONE = 0;
static const uint8_t OTA_BOOT_ARMED = 1;
static const uint8_t OTA_BOOT_SEEN = 2;
static const uint8_t OTA_BOOT_ROLLED_BACK = 3;

static const size_t OTA_URL_MAX_LEN = 384;
static const size_t OTA_ERROR_MAX_LEN = 160;
static const size_t OTA_MD5_HEX_LEN = 32;
static const size_t OTA_DOWNLOAD_CHUNK_SIZE = 1024;

static OtaState otaState = OtaState::IDLE;
static uint8_t otaProgressPct = 0;
static char otaError[OTA_ERROR_MAX_LEN] = {0};
static char otaUrl[OTA_URL_MAX_LEN] = {0};
static char otaExpectedMd5[OTA_MD5_HEX_LEN + 1] = {0};
static bool otaRollbackTriggered = false;
static bool otaAwaitingValidation = false;
static unsigned long otaValidationStartMs = 0;

struct ParsedUrl {
    String host;
    uint16_t port;
    String path;
};

static void setState(OtaState state) {
    otaState = state;
}

static void clearError() {
    otaError[0] = '\0';
}

static void setError(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(otaError, sizeof(otaError), fmt, args);
    va_end(args);
    otaError[sizeof(otaError) - 1] = '\0';
    setState(OtaState::FAILED);
}

static void clearUrl() {
    otaUrl[0] = '\0';
}

static bool prefsBegin(Preferences& prefs, bool readOnly) {
    if (prefs.begin(OTA_NVS_NAMESPACE, readOnly)) {
        return true;
    }
    storeSd_logEvent("[OTA] failed to open ota preferences");
    return false;
}

static uint8_t readBootPendingFlag() {
    Preferences prefs;
    if (!prefsBegin(prefs, true)) {
        return OTA_BOOT_NONE;
    }
    uint8_t value = prefs.getUChar(OTA_KEY_BOOT_PENDING, OTA_BOOT_NONE);
    prefs.end();
    return value;
}

static uint32_t readBootTimestamp() {
    Preferences prefs;
    if (!prefsBegin(prefs, true)) {
        return 0;
    }
    uint32_t value = prefs.getULong(OTA_KEY_BOOT_TS, 0UL);
    prefs.end();
    return value;
}

static bool writeBootFlags(uint8_t pending, uint32_t bootTs) {
    Preferences prefs;
    if (!prefsBegin(prefs, false)) {
        return false;
    }

    bool ok = prefs.putUChar(OTA_KEY_BOOT_PENDING, pending) > 0;
    ok = (prefs.putULong(OTA_KEY_BOOT_TS, bootTs) > 0) && ok;
    prefs.end();
    return ok;
}

static bool clearBootFlags() {
    Preferences prefs;
    if (!prefsBegin(prefs, false)) {
        return false;
    }
    bool okPending = prefs.putUChar(OTA_KEY_BOOT_PENDING, OTA_BOOT_NONE) > 0;
    bool okTs = prefs.putULong(OTA_KEY_BOOT_TS, 0UL) > 0;
    prefs.end();
    return okPending && okTs;
}

static bool runningPartitionPendingVerify() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running == nullptr) {
        return false;
    }

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
        return false;
    }
    return state == ESP_OTA_IMG_PENDING_VERIFY;
}

static bool markRunningAppValid() {
    if (!runningPartitionPendingVerify()) {
        clearBootFlags();
        otaAwaitingValidation = false;
        return true;
    }

    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        setError("esp_ota_mark_app_valid failed (%d)", (int)err);
        storeSd_logEvent("[OTA] failed to mark app valid");
        return false;
    }

    clearBootFlags();
    otaAwaitingValidation = false;
    storeSd_logEvent("[OTA] new firmware marked valid");
    return true;
}

static bool normaliseMd5(const char* source, char* outHex, size_t outSize) {
    if (outSize < OTA_MD5_HEX_LEN + 1 || source == nullptr || source[0] == '\0') {
        return false;
    }

    size_t len = strlen(source);
    if (len != OTA_MD5_HEX_LEN) {
        return false;
    }

    for (size_t i = 0; i < OTA_MD5_HEX_LEN; ++i) {
        if (!isxdigit(static_cast<unsigned char>(source[i]))) {
            return false;
        }
        outHex[i] = static_cast<char>(tolower(static_cast<unsigned char>(source[i])));
    }
    outHex[OTA_MD5_HEX_LEN] = '\0';
    return true;
}

static void digestToHex(const uint8_t* digest, char* outHex, size_t outSize) {
    if (outSize < OTA_MD5_HEX_LEN + 1) {
        return;
    }

    for (size_t i = 0; i < 16; ++i) {
        snprintf(outHex + (i * 2), outSize - (i * 2), "%02x", digest[i]);
    }
    outHex[OTA_MD5_HEX_LEN] = '\0';
}

static bool parseHttpsUrl(const char* url, ParsedUrl& parsed) {
    if (url == nullptr) {
        return false;
    }

    String fullUrl(url);
    if (!fullUrl.startsWith("https://")) {
        return false;
    }

    String remainder = fullUrl.substring(8);
    int slashPos = remainder.indexOf('/');
    String hostPort = (slashPos >= 0) ? remainder.substring(0, slashPos) : remainder;
    parsed.path = (slashPos >= 0) ? remainder.substring(slashPos) : String("/");

    if (hostPort.isEmpty()) {
        return false;
    }

    int colonPos = hostPort.indexOf(':');
    if (colonPos >= 0) {
        parsed.host = hostPort.substring(0, colonPos);
        long port = hostPort.substring(colonPos + 1).toInt();
        if (parsed.host.isEmpty() || port <= 0 || port > 65535) {
            return false;
        }
        parsed.port = static_cast<uint16_t>(port);
    } else {
        parsed.host = hostPort;
        parsed.port = 443;
    }

    return !parsed.host.isEmpty() && !parsed.path.isEmpty();
}

static bool readHttpHeaders(WiFiClientSecure& client, int& statusCode, int32_t& contentLength) {
    statusCode = -1;
    contentLength = -1;
    unsigned long startedMs = millis();

    while (true) {
        watchdog_feed();

        if (millis() - startedMs > OTA_HTTP_TIMEOUT_MS) {
            setError("HTTP header timeout");
            return false;
        }

        char line[256];
        size_t len = client.readBytesUntil('\n', line, sizeof(line) - 1);
        if (len == 0) {
            if (!client.connected() && client.available() == 0) {
                setError("HTTP connection closed during headers");
                return false;
            }
            continue;
        }

        line[len] = '\0';
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
            line[--len] = '\0';
        }

        if (len == 0) {
            break;
        }

        if (strncmp(line, "HTTP/1.", 7) == 0) {
            char* firstSpace = strchr(line, ' ');
            if (firstSpace != nullptr) {
                statusCode = atoi(firstSpace + 1);
            }
        } else if (strncmp(line, "Content-Length:", 15) == 0) {
            contentLength = static_cast<int32_t>(atol(line + 15));
        }
    }

    if (statusCode != 200) {
        setError("HTTP status %d", statusCode);
        return false;
    }

    if (contentLength <= 0) {
        setError("missing Content-Length");
        return false;
    }

    return true;
}

static bool downloadAndStageFirmware(const char* url, const char* md5Expected) {
    ParsedUrl parsed;
    if (!parseHttpsUrl(url, parsed)) {
        setError("OTA URL must be https://host/path");
        return false;
    }

    WiFiClientSecure client;
    client.setCACert(MQTT_CA_CERT);
    client.setHandshakeTimeout(5);
    client.setTimeout(2);

    watchdog_feed();
    if (!client.connect(parsed.host.c_str(), parsed.port, 5000)) {
        char tlsMsg[96] = {0};
        client.lastError(tlsMsg, sizeof(tlsMsg));
        setError("TLS connect failed: %s", tlsMsg[0] ? tlsMsg : "unknown");
        return false;
    }

    String request = String("GET ") + parsed.path + " HTTP/1.1\r\n" +
                     "Host: " + parsed.host + "\r\n" +
                     "User-Agent: TWWP-OTA/1.0\r\n" +
                     "Connection: close\r\n\r\n";
    client.print(request);

    int statusCode = -1;
    int32_t contentLength = -1;
    if (!readHttpHeaders(client, statusCode, contentLength)) {
        client.stop();
        return false;
    }

    if (!Update.begin(static_cast<size_t>(contentLength))) {
        setError("Update.begin failed (%u)", (unsigned)Update.getError());
        client.stop();
        return false;
    }

    if (md5Expected != nullptr && md5Expected[0] != '\0') {
        if (!Update.setMD5(md5Expected)) {
            setError("Update.setMD5 rejected expected hash");
            Update.abort();
            client.stop();
            return false;
        }
    }

    mbedtls_md5_context md5Context;
    mbedtls_md5_init(&md5Context);
    bool md5Started = false;
    if (md5Expected != nullptr && md5Expected[0] != '\0') {
        if (mbedtls_md5_starts(&md5Context) != 0) {
            setError("failed to start MD5");
            Update.abort();
            client.stop();
            mbedtls_md5_free(&md5Context);
            return false;
        }
        md5Started = true;
    }

    uint8_t buffer[OTA_DOWNLOAD_CHUNK_SIZE];
    size_t bytesWritten = 0;
    unsigned long lastDataMs = millis();

    while (bytesWritten < static_cast<size_t>(contentLength)) {
        watchdog_feed();

        if (WiFi.status() != WL_CONNECTED) {
            setError("WiFi disconnected during OTA");
            Update.abort();
            client.stop();
            if (md5Started) {
                mbedtls_md5_free(&md5Context);
            }
            return false;
        }

        if (millis() - lastDataMs > OTA_HTTP_TIMEOUT_MS) {
            setError("OTA download timeout");
            Update.abort();
            client.stop();
            if (md5Started) {
                mbedtls_md5_free(&md5Context);
            }
            return false;
        }

        int availableBytes = client.available();
        if (availableBytes <= 0) {
            if (!client.connected() && bytesWritten < static_cast<size_t>(contentLength)) {
                setError("HTTP body ended early (%u/%u bytes)",
                         (unsigned)bytesWritten,
                         (unsigned)contentLength);
                Update.abort();
                client.stop();
                if (md5Started) {
                    mbedtls_md5_free(&md5Context);
                }
                return false;
            }
            yield();
            continue;
        }

        size_t remaining = static_cast<size_t>(contentLength) - bytesWritten;
        size_t toRead = remaining;
        if (toRead > OTA_DOWNLOAD_CHUNK_SIZE) {
            toRead = OTA_DOWNLOAD_CHUNK_SIZE;
        }
        if (toRead > static_cast<size_t>(availableBytes)) {
            toRead = static_cast<size_t>(availableBytes);
        }

        int bytesRead = client.readBytes(reinterpret_cast<char*>(buffer), toRead);
        if (bytesRead <= 0) {
            yield();
            continue;
        }

        lastDataMs = millis();

        if (md5Started && mbedtls_md5_update(&md5Context, buffer, bytesRead) != 0) {
            setError("failed to update MD5");
            Update.abort();
            client.stop();
            mbedtls_md5_free(&md5Context);
            return false;
        }

        size_t writtenNow = Update.write(buffer, static_cast<size_t>(bytesRead));
        if (writtenNow != static_cast<size_t>(bytesRead)) {
            setError("Update.write failed (%u)", (unsigned)Update.getError());
            Update.abort();
            client.stop();
            if (md5Started) {
                mbedtls_md5_free(&md5Context);
            }
            return false;
        }

        bytesWritten += writtenNow;
        otaProgressPct = static_cast<uint8_t>((bytesWritten * 100U) / static_cast<size_t>(contentLength));
    }

    client.stop();
    setState(OtaState::VERIFYING);

    if (md5Started) {
        uint8_t digest[16] = {0};
        char actualMd5[OTA_MD5_HEX_LEN + 1] = {0};
        if (mbedtls_md5_finish(&md5Context, digest) != 0) {
            setError("failed to finish MD5");
            Update.abort();
            mbedtls_md5_free(&md5Context);
            return false;
        }
        mbedtls_md5_free(&md5Context);
        digestToHex(digest, actualMd5, sizeof(actualMd5));
        if (strcmp(actualMd5, md5Expected) != 0) {
            setError("MD5 mismatch expected=%s got=%s", md5Expected, actualMd5);
            Update.abort();
            return false;
        }
    }

    setState(OtaState::APPLYING);
    if (!Update.end()) {
        setError("Update.end failed (%u)", (unsigned)Update.getError());
        return false;
    }

    if (!Update.isFinished()) {
        setError("update image incomplete");
        return false;
    }

    return true;
}

} // namespace

bool netOta_begin() {
    otaRollbackTriggered = false;
    otaAwaitingValidation = false;
    otaValidationStartMs = 0;
    otaProgressPct = 0;
    clearError();
    clearUrl();
    otaExpectedMd5[0] = '\0';
    setState(OtaState::IDLE);

    uint8_t pending = readBootPendingFlag();
    if (pending == OTA_BOOT_ROLLED_BACK) {
        otaRollbackTriggered = true;
        clearBootFlags();
        storeSd_logEvent("[OTA] booted after rollback to previous partition");
        return true;
    }

    if (pending == OTA_BOOT_SEEN) {
        uint32_t bootTs = readBootTimestamp();
        char logMsg[128];
        snprintf(logMsg, sizeof(logMsg),
                 "[OTA] rollback triggered after failed boot validation (boot_ts=%lu)",
                 static_cast<unsigned long>(bootTs));
        storeSd_logEvent(logMsg);
        netOta_rollback();
        return false;
    }

    if (pending == OTA_BOOT_ARMED || runningPartitionPendingVerify()) {
        if (pending == OTA_BOOT_NONE) {
            writeBootFlags(OTA_BOOT_SEEN, timeRtc_isSynced() ? timeRtc_getUnixTime() : 0UL);
        } else {
            writeBootFlags(OTA_BOOT_SEEN, readBootTimestamp());
        }
        otaAwaitingValidation = true;
        otaValidationStartMs = millis();
        storeSd_logEvent("[OTA] validation window started for new firmware");
    } else {
        clearBootFlags();
    }

    ArduinoOTA.setHostname("twwp-" NODE_ID);
    ArduinoOTA.onStart([]() {
        clearError();
        clearUrl();
        otaProgressPct = 0;
        setState(OtaState::DOWNLOADING);
        storeSd_logEvent("[OTA] ArduinoOTA upload started");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        if (total == 0U) {
            otaProgressPct = 0;
            return;
        }
        otaProgressPct = static_cast<uint8_t>((progress * 100U) / total);
    });
    ArduinoOTA.onEnd([]() {
        otaProgressPct = 100;
        setState(OtaState::SUCCESS);
        storeSd_logEvent("[OTA] ArduinoOTA upload complete");
    });
    ArduinoOTA.onError([](ota_error_t error) {
        setError("ArduinoOTA error %u", (unsigned)error);
        char logMsg[96];
        snprintf(logMsg, sizeof(logMsg), "[OTA] ArduinoOTA failed: %u", (unsigned)error);
        storeSd_logEvent(logMsg);
    });
    ArduinoOTA.begin();
    storeSd_logEvent("[OTA] ArduinoOTA ready");

    return true;
}

void netOta_loop() {
    if (otaState == OtaState::IDLE) {
        ArduinoOTA.handle();
    }

    if (!otaAwaitingValidation) {
        return;
    }

    if (millis() - otaValidationStartMs < OTA_ROLLBACK_TIMEOUT_MS) {
        return;
    }

    markRunningAppValid();
}

bool netOta_beginUpdate(const char* url, const char* md5Expected) {
    if (otaState != OtaState::IDLE) {
        setError("OTA busy");
        return false;
    }

    if (otaAwaitingValidation) {
        setError("current firmware still in validation window");
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        setError("WiFi not connected");
        return false;
    }

    if (url == nullptr || url[0] == '\0') {
        setError("OTA URL is empty");
        return false;
    }

    if (strlen(url) >= sizeof(otaUrl)) {
        setError("OTA URL too long");
        return false;
    }

    if (md5Expected != nullptr && md5Expected[0] != '\0') {
        if (!normaliseMd5(md5Expected, otaExpectedMd5, sizeof(otaExpectedMd5))) {
            setError("invalid OTA MD5 (need 32 hex chars)");
            return false;
        }
    } else {
        otaExpectedMd5[0] = '\0';
    }

    strlcpy(otaUrl, url, sizeof(otaUrl));
    otaProgressPct = 0;
    clearError();
    setState(OtaState::DOWNLOADING);
    storeSd_logEvent("[OTA] HTTPS OTA download started");

    if (!downloadAndStageFirmware(otaUrl, otaExpectedMd5[0] ? otaExpectedMd5 : nullptr)) {
        char logMsg[220];
        snprintf(logMsg, sizeof(logMsg), "[OTA] update failed: %s", otaError[0] ? otaError : "unknown");
        storeSd_logEvent(logMsg);
        return false;
    }

    uint32_t bootTs = timeRtc_isSynced() ? timeRtc_getUnixTime() : 0UL;
    if (!writeBootFlags(OTA_BOOT_ARMED, bootTs)) {
        Update.abort();
        setError("failed to store OTA rollback flags");
        storeSd_logEvent("[OTA] failed to store rollback flags");
        return false;
    }

    otaProgressPct = 100;
    setState(OtaState::SUCCESS);
    storeSd_logEvent("[OTA] update applied successfully, rebooting");
    ESP.restart();
    return true;
}

OtaState netOta_getState() {
    return otaState;
}

uint8_t netOta_getProgressPct() {
    return otaProgressPct;
}

const char* netOta_getError() {
    return otaError[0] ? otaError : nullptr;
}

const char* netOta_getUrl() {
    return otaUrl[0] ? otaUrl : nullptr;
}

void netOta_rollback() {
    const esp_partition_t* rollbackPartition = esp_ota_get_next_update_partition(nullptr);
    if (rollbackPartition == nullptr) {
        setError("no rollback partition available");
        storeSd_logEvent("[OTA] rollback failed: no target partition");
        return;
    }

    if (!writeBootFlags(OTA_BOOT_ROLLED_BACK, 0UL)) {
        storeSd_logEvent("[OTA] rollback warning: failed to persist rollback marker");
    }

    esp_err_t err = esp_ota_set_boot_partition(rollbackPartition);
    if (err != ESP_OK) {
        setError("esp_ota_set_boot_partition failed (%d)", (int)err);
        storeSd_logEvent("[OTA] rollback failed: set_boot_partition error");
        return;
    }

    storeSd_logEvent("[OTA] rolling back to previous partition");
    ESP.restart();
}

bool netOta_isRollbackPending() {
    return otaRollbackTriggered;
}
