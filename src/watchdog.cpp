#include "watchdog.h"

#include <esp_task_wdt.h>

#include "config.h"
#include "store_sd.h"

static bool watchdogReady = false;

bool watchdog_begin() {
    const esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms = WATCHDOG_TIMEOUT_S * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_err_t err = esp_task_wdt_init(&wdt_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        Serial.print("[WDT] init failed: ");
        Serial.println(static_cast<int>(err));
        return false;
    }

    err = esp_task_wdt_add(NULL);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        Serial.print("[WDT] add failed: ");
        Serial.println(static_cast<int>(err));
        return false;
    }

    watchdogReady = true;
    return true;
}

void watchdog_feed() {
    if (watchdogReady) {
        esp_task_wdt_reset();
    }
}

void watchdog_logCrash(const char* reason) {
    if (reason == nullptr) {
        reason = "unknown";
    }

    Serial.print("[WDT] crash: ");
    Serial.println(reason);

    char buf[192];
    snprintf(buf, sizeof(buf), "watchdog:%s", reason);
    storeSd_logEvent(buf);
}
