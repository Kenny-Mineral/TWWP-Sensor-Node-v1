#include "net_wifi.h"

#include <WiFi.h>
#include <WiFiManager.h>

#include "config.h"
#include "status_led.h"
#include "watchdog.h"

static WiFiManager wifiManager;

static String makeApName() {
    uint64_t mac = ESP.getEfuseMac();
    uint32_t id = (uint32_t)(mac & 0xFFFFFF);
    char buf[16];
    snprintf(buf, sizeof(buf), "%06X", id);
    return String("TWWP-Setup-") + String(buf);
}

bool netWifi_begin() {
    Serial.println("[WiFi] init");

    String hostname = String("twwp-") + String(NODE_ID);
    WiFi.setHostname(hostname.c_str());

    // Clean slate before autoConnect — avoids stale mode state on ESP32-S3
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_STA);
    delay(200);

    String apName = makeApName();
    statusLed_setState(LedState::WIFI_CONNECTING);

    wifiManager.setConfigPortalTimeout(180);
    wifiManager.setConnectTimeout(15);
    wifiManager.setSaveConnectTimeout(10);
    wifiManager.setWiFiAPChannel(1);

    wifiManager.setAPCallback([](WiFiManager* wm) {
        Serial.println();
        Serial.println("========================================");
        Serial.println("[WiFi] SETUP PORTAL OPEN");
        Serial.print("[WiFi]   Connect to WiFi:  ");
        Serial.println(wm->getConfigPortalSSID());
        Serial.println("[WiFi]   Password:        wateriswet");
        Serial.println("[WiFi]   Then open:       http://192.168.4.1");
        Serial.println("[WiFi]   Portal closes in 180 seconds");
        Serial.println("========================================");
        Serial.println();
    });

    wifiManager.setSaveConfigCallback([]() {
        Serial.println("[WiFi] credentials saved — connecting...");
    });

    bool ok = wifiManager.autoConnect(apName.c_str(), "wateriswet");

    if (!ok) {
        Serial.println("[WiFi] provisioning failed or timed out");
        statusLed_setState(LedState::ERROR);
        return false;
    }

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
    if (now - lastReconnect < 5000) return;
    lastReconnect = now;

    Serial.println("[WiFi] lost connection, attempting reconnect");
    statusLed_setState(LedState::WIFI_CONNECTING);

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

void netWifi_reconnect() {
    Serial.println("[WiFi] reconnect requested");
    WiFi.disconnect(false);  // drop connection, keep credentials — netWifi_loop() will reconnect
}

void netWifi_resetCredentials() {
    Serial.println("[WiFi] clearing credentials and rebooting");
    wifiManager.resetSettings();
    WiFi.disconnect(true, true);
    delay(500);
    ESP.restart();
}
