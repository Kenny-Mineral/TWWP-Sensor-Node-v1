#include "net_wifi.h"

#include <WiFi.h>
#include <WiFiManager.h>

#include "config.h"
#include "status_led.h"
#include "watchdog.h"

static WiFiManager wifiManager;

static String makeApName() {
    // Use lower 24 bits of efuse MAC as a short chip id
    uint64_t mac = ESP.getEfuseMac();
    uint32_t id = (uint32_t)(mac & 0xFFFFFF);
    char buf[16];
    snprintf(buf, sizeof(buf), "%06X", id);
    return String("TWWP-Setup-") + String(buf);
}

bool netWifi_begin() {
    Serial.println("[WiFi] init");

    // Hostname: twwp-<NODE_ID>
    String hostname = String("twwp-") + String(NODE_ID);
    WiFi.setHostname(hostname.c_str());

    // Use WiFiManager to autoconnect or start captive portal
    String apName = makeApName();

    statusLed_setState(LedState::WIFI_CONNECTING);

    // Cap portal time so watchdog (30s) is never exceeded
    wifiManager.setTimeout(25);

    // Try autoConnect (will use saved creds if present, otherwise starts AP)
    bool ok = wifiManager.autoConnect(apName.c_str(), "wateriswet");

    if (!ok) {
        Serial.println("[WiFi] provisioning failed or timed out");
        statusLed_setState(LedState::ERROR);
        return false;
    }

    // Connected
    Serial.print("[WiFi] connected, IP=");
    Serial.println(WiFi.localIP().toString());
    statusLed_setState(LedState::ONLINE);
    return true;
}

void netWifi_loop() {
    static unsigned long lastReconnect = 0;

    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    unsigned long now = millis();
    if (now - lastReconnect < 5000) return; // throttle attempts
    lastReconnect = now;

    Serial.println("[WiFi] lost connection, attempting reconnect");
    statusLed_setState(LedState::WIFI_CONNECTING);

    // Non-blocking reconnect attempt; wait a short time while feeding watchdog
    WiFi.reconnect();
    unsigned long start = millis();
    while (millis() - start < 5000) {
        if (WiFi.status() == WL_CONNECTED) break;
        watchdog_feed();
        delay(200);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("[WiFi] reconnected, IP=");
        Serial.println(WiFi.localIP().toString());
        statusLed_setState(LedState::ONLINE);
    } else {
        Serial.println("[WiFi] reconnect attempt failed");
        statusLed_setState(LedState::WIFI_CONNECTING);
    }
}

bool netWifi_isConnected() {
    return (WiFi.status() == WL_CONNECTED);
}

void netWifi_resetCredentials() {
    Serial.println("[WiFi] resetting stored credentials (WiFiManager NVS)");
    // Clear saved WiFi settings stored by WiFiManager
    wifiManager.resetSettings();
    // Also attempt to clear saved WiFi on the stack
    WiFi.disconnect(true, true);
    delay(200);
    Serial.println("[WiFi] rebooting to apply reset");
    ESP.restart();
}
