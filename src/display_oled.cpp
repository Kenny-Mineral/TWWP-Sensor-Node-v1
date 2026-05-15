#include "display_oled.h"
#include <Wire.h>
#include <Preferences.h>
#include "SSD1306Wire.h"
#include "OLEDDisplayUi.h"
#include "pins.h"
#include "sensor_flow.h"
#include "sensor_yieryi.h"
#include "sensor_tds_meter.h"
#include "sensor_leak.h"
#include "net_wifi.h"
#include "net_mqtt.h"
#include "store_sd.h"
#include "sensor_voltage.h"
#include "time_rtc.h"
#include "session_flow.h"
#include "wq_config.h"
#include <WiFi.h>

// ── Config ─────────────────────────────────────────────────────────────────
#define OLED_I2C_ADDR       0x3C
#define OLED_FPS            30
#define OLED_MS_PER_FRAME   5000
#define BTN_DEBOUNCE_MS     200
#define TANK_CAPACITY_L     20.0f
// 4:1 waste:pure RO ratio → 20% of feed becomes stored pure water
#define TANK_RO_RECOVERY    0.20f
#define TANK_NVS_SAVE_MS    60000UL
#define HEADER_SEP_Y        12

// ── XBM branding bitmap (48×32) ─────────────────────────────────────────────
// Two stylised cups clinking — spark at centre top, cup outlines, stems, bases
// XBM format: 1=black pixel, 0=white; LSB of each byte = leftmost pixel in group
static const uint8_t TWWP_LOGO_BITS[] PROGMEM = {
    0x00,0x00,0x00,0x00,0x00,0x00,  // row  0
    0x00,0x00,0x00,0x00,0x00,0x00,  // row  1
    0x00,0x00,0x00,0x01,0x00,0x00,  // row  2  spark centre (col 24)
    0x00,0x00,0x80,0x03,0x00,0x00,  // row  3  spark spread (cols 23-25)
    0x00,0x00,0xC0,0x06,0x00,0x00,  // row  4  spark wide   (cols 22,23,25,26)
    0x00,0x00,0x80,0x03,0x00,0x00,  // row  5  spark spread
    0x00,0x00,0x00,0x00,0x00,0x00,  // row  6
    0xF8,0xFF,0x0F,0xE0,0xFF,0x3F,  // row  7  left rim cols 3-19, right rim cols 29-45
    0x08,0x00,0x08,0x20,0x00,0x20,  // row  8  cup sides
    0x08,0x00,0x08,0x20,0x00,0x20,  // row  9
    0x08,0x00,0x08,0x20,0x00,0x20,  // row 10
    0x08,0x00,0x08,0x20,0x00,0x20,  // row 11
    0x08,0x00,0x08,0x20,0x00,0x20,  // row 12
    0x08,0x00,0x08,0x20,0x00,0x20,  // row 13
    0x08,0x00,0x08,0x20,0x00,0x20,  // row 14
    0x08,0x00,0x08,0x20,0x00,0x20,  // row 15
    0x08,0x00,0x08,0x20,0x00,0x20,  // row 16
    0x08,0x00,0x08,0x20,0x00,0x20,  // row 17
    0x08,0x00,0x08,0x20,0x00,0x20,  // row 18
    0x08,0x00,0x08,0x20,0x00,0x20,  // row 19
    0x08,0x00,0x08,0x20,0x00,0x20,  // row 20
    0x08,0x00,0x08,0x20,0x00,0x20,  // row 21
    0x08,0x00,0x08,0x20,0x00,0x20,  // row 22
    0xF8,0xFF,0x0F,0xE0,0xFF,0x3F,  // row 23  cup bottoms
    0x00,0x00,0x00,0x00,0x00,0x00,  // row 24
    0x00,0x0C,0x00,0x00,0x30,0x00,  // row 25  stems (cols 10-11, 36-37)
    0x00,0x0C,0x00,0x00,0x30,0x00,  // row 26
    0x00,0x0C,0x00,0x00,0x30,0x00,  // row 27
    0xC0,0xFF,0x00,0x00,0xFF,0x03,  // row 28  bases (cols 6-15, 32-41)
    0x00,0x00,0x00,0x00,0x00,0x00,  // row 29
    0x00,0x00,0x00,0x00,0x00,0x00,  // row 30
    0x00,0x00,0x00,0x00,0x00,0x00,  // row 31
};
static const int TWWP_LOGO_W = 48;
static const int TWWP_LOGO_H = 32;

// ── Private state ──────────────────────────────────────────────────────────
static SSD1306Wire   s_disp(OLED_I2C_ADDR, PIN_OLED_SDA, PIN_OLED_SCL,
                             GEOMETRY_128_64, I2C_ONE);
static OLEDDisplayUi s_ui(&s_disp);
static bool          s_ready       = false;
static uint32_t      s_btnLastMs   = 0;
static float         s_tankLiters  = TANK_CAPACITY_L;
static uint32_t      s_tankLastMs  = 0;
static uint32_t      s_tankSaveMs  = 0;
static Preferences   s_prefs;

// Invisible 8×8 symbol — disables the ThingPulse frame indicator dots
static const uint8_t s_emptySymbol[] PROGMEM = { 0,0,0,0, 0,0,0,0 };

// ── Format helpers (snprintf into caller's stack buffer, no heap) ──────────
static void fmtPh(char* b, size_t n, uint8_t z) {
    if (!sensorYieryi_isOnline(z)) { snprintf(b, n, "pH:---"); return; }
    snprintf(b, n, "pH:%.1f", sensorYieryi_getPh(z));
}
static void fmtOrp(char* b, size_t n, uint8_t z) {
    if (!sensorYieryi_isOnline(z)) { snprintf(b, n, "ORP:---"); return; }
    snprintf(b, n, "%+dmV", (int)sensorYieryi_getOrpMv(z));
}
static void fmtTemp(char* b, size_t n, uint8_t z) {
    if (!sensorYieryi_isOnline(z)) { snprintf(b, n, "T:---"); return; }
    snprintf(b, n, "T:%.1fC", sensorYieryi_getTempC(z));
}
static void fmtEc(char* b, size_t n, uint8_t z) {
    if (!sensorYieryi_isOnline(z)) { snprintf(b, n, "EC:---"); return; }
    snprintf(b, n, "EC:%.0f", sensorYieryi_getEcUsCm(z));
}
static void fmtRej(char* b, size_t n) {
    if (!sensorYieryi_isOnline(YIERYI_ZONE_PRE_RO) ||
        !sensorYieryi_isOnline(YIERYI_ZONE_POST_RO)) {
        snprintf(b, n, "Rej:---"); return;
    }
    float pre  = sensorYieryi_getTdsPpm(YIERYI_ZONE_PRE_RO);
    float post = sensorYieryi_getTdsPpm(YIERYI_ZONE_POST_RO);
    if (pre < 1.0f) { snprintf(b, n, "Rej:---"); return; }
    float r = constrain((1.0f - post / pre) * 100.0f, 0.0f, 100.0f);
    snprintf(b, n, "Rej:%.0f%%", r);
}

// ── Overlay: persistent header (always visible, top 12px) ─────────────────
static void drawHeader(OLEDDisplay* d, OLEDDisplayUiState*) {
    char buf[20];
    d->setFont(ArialMT_Plain_10);

    d->setTextAlignment(TEXT_ALIGN_LEFT);
    String ts = timeRtc_getISOTimestamp();
    float sesVol = sessionFlow_getCurrentVolumeOut();
    snprintf(buf, sizeof(buf), "%c%c:%c%c %.1fL",
             ts[11], ts[12], ts[14], ts[15], sesVol);
    d->drawString(0, 0, buf);

    d->setTextAlignment(TEXT_ALIGN_CENTER);
    if (sensorLeak_isWet() && (millis() % 1000 < 500)) {
        d->drawString(64, 0, "!LEAK!");
    } else {
        d->drawString(64, 0, "TWWP");
    }

    d->setTextAlignment(TEXT_ALIGN_RIGHT);
    snprintf(buf, sizeof(buf), "%s%s%s",
             netWifi_isConnected() ? "W" : "!",
             netMqtt_isConnected() ? "M" : "!",
             storeSd_bufferCount() ? "B" : "");
    d->drawString(128, 0, buf);

    d->drawHorizontalLine(0, HEADER_SEP_Y, 128);
}

// ── WQ Summary frame helper — one row: name left, ppm centre, status right ─
static void drawWqRow(OLEDDisplay* d, int16_t x, int16_t rowY,
                      const char* name, bool online, float tds, const char* status) {
    char buf[12];
    d->setFont(ArialMT_Plain_10);
    d->setTextAlignment(TEXT_ALIGN_LEFT);
    d->drawString(x, rowY, name);
    d->setTextAlignment(TEXT_ALIGN_CENTER);
    if (online) snprintf(buf, sizeof(buf), "%dppm", (int)tds);
    else        strlcpy(buf, "---", sizeof(buf));
    d->drawString(x + 64, rowY, buf);
    d->setTextAlignment(TEXT_ALIGN_RIGHT);
    d->drawString(x + 128, rowY, online ? status : "---");
}

// ── Zone slide helper ──────────────────────────────────────────────────────
// Layout (y offsets from frame top, which = 0 at display top):
//   y+14  zone title (10pt)
//   y+24  hero TDS value (24pt)
//   y+50  "ppm" unit (10pt, baseline of hero)
//   x+75 right col: pH y+14, ORP y+26, Temp y+38, EC/Rej y+50 (all 10pt)
static void drawZoneFrame(OLEDDisplay* d, int16_t x, int16_t y,
                           uint8_t zone, const char* title, bool showRej) {
    char buf[20];

    d->setTextAlignment(TEXT_ALIGN_LEFT);
    d->setFont(ArialMT_Plain_10);
    d->drawString(x, y + 14, title);

    d->setFont(ArialMT_Plain_24);
    if (!sensorYieryi_isOnline(zone)) {
        d->drawString(x, y + 24, "---");
    } else {
        snprintf(buf, sizeof(buf), "%d", (int)sensorYieryi_getTdsPpm(zone));
        d->drawString(x, y + 24, buf);
    }

    d->setFont(ArialMT_Plain_10);
    d->drawString(x + 50, y + 50, "ppm");

    fmtPh(buf, sizeof(buf), zone);
    d->drawString(x + 75, y + 14, buf);
    fmtOrp(buf, sizeof(buf), zone);
    d->drawString(x + 75, y + 26, buf);
    fmtTemp(buf, sizeof(buf), zone);
    d->drawString(x + 75, y + 38, buf);
    if (showRej) {
        fmtRej(buf, sizeof(buf));
    } else {
        fmtEc(buf, sizeof(buf), zone);
    }
    d->drawString(x + 75, y + 50, buf);
}

// ── Frame 0: WQ Summary — all three positions on one slide ────────────────
static void frameWqSummary(OLEDDisplay* d, OLEDDisplayUiState*, int16_t x, int16_t y) {
    bool preOn  = sensorTdsMeter_isOnline(TDS_ZONE_PRE_RO);
    float preTds = preOn ? sensorTdsMeter_getTds(TDS_ZONE_PRE_RO) : 0.0f;
    drawWqRow(d, x, y + 14, wqConfig_getPreRoName(),  preOn,  preTds,
              wqConfig_evalPreRo(preTds));

    bool postOn  = sensorTdsMeter_isOnline(TDS_ZONE_POST_RO);
    float postTds = postOn ? sensorTdsMeter_getTds(TDS_ZONE_POST_RO) : 0.0f;
    drawWqRow(d, x, y + 31, wqConfig_getPostRoName(), postOn, postTds,
              wqConfig_evalPostRo(postTds));

    bool remOn  = sensorYieryi_isOnline(YIERYI_ZONE_REMIN);
    float remTds = remOn ? sensorYieryi_getTdsPpm(YIERYI_ZONE_REMIN) : 0.0f;
    drawWqRow(d, x, y + 48, wqConfig_getReminName(),  remOn,  remTds,
              wqConfig_evalRemin(remTds));
}

// ── Frame 1: Remineralised ─────────────────────────────────────────────────
static void frameRemin(OLEDDisplay* d, OLEDDisplayUiState*, int16_t x, int16_t y) {
    drawZoneFrame(d, x, y, YIERYI_ZONE_REMIN, "REMIN", false);
}

// ── Frame 3: Flow & Waste ──────────────────────────────────────────────────
static void frameFlow(OLEDDisplay* d, OLEDDisplayUiState*, int16_t x, int16_t y) {
    char buf[20];
    float f1 = sensorFlow_getRateLpm(1);  // purified out
    float f2 = sensorFlow_getRateLpm(2);  // RO feed in

    d->setTextAlignment(TEXT_ALIGN_LEFT);
    d->setFont(ArialMT_Plain_10);
    d->drawString(x, y + 14, "FLOW & WASTE");

    d->setFont(ArialMT_Plain_16);
    snprintf(buf, sizeof(buf), "OUT:%.2fL/m", f1);
    d->drawString(x, y + 26, buf);
    snprintf(buf, sizeof(buf), "IN: %.2fL/m", f2);
    d->drawString(x, y + 44, buf);

    d->setFont(ArialMT_Plain_10);
    if (f1 > 0.01f) {
        snprintf(buf, sizeof(buf), "Ratio %.1f:1", f2 / f1);
    } else {
        snprintf(buf, sizeof(buf), "Ratio ---");
    }
    d->drawString(x, y + 54, buf);
}

// ── Frame 4: Storage tank ──────────────────────────────────────────────────
// Tank level is calculated in updateTankLevel() from flow differential.
// Override with displayOled_setTankLiters() when a real level sensor is wired.
static void frameTank(OLEDDisplay* d, OLEDDisplayUiState*, int16_t x, int16_t y) {
    char buf[20];

    d->setTextAlignment(TEXT_ALIGN_LEFT);
    d->setFont(ArialMT_Plain_10);
    d->drawString(x, y + 14, "STORAGE TANK");

    // Vertical level bar — outline 16px wide × 36px tall
    d->drawRect(x + 4, y + 25, 16, 36);
    int fillH = (int)((s_tankLiters / TANK_CAPACITY_L) * 34.0f);
    fillH = constrain(fillH, 0, 34);
    if (fillH > 0) {
        d->fillRect(x + 5, y + 26 + (34 - fillH), 14, fillH);
    }

    d->setFont(ArialMT_Plain_16);
    snprintf(buf, sizeof(buf), "%d%%", (int)((s_tankLiters / TANK_CAPACITY_L) * 100.0f));
    d->drawString(x + 28, y + 26, buf);

    d->setFont(ArialMT_Plain_10);
    snprintf(buf, sizeof(buf), "%.1f/%.0fL", s_tankLiters, TANK_CAPACITY_L);
    d->drawString(x + 28, y + 43, buf);

    float pureRate = sensorFlow_getRateLpm(2) * TANK_RO_RECOVERY;
    float needed   = TANK_CAPACITY_L - s_tankLiters;
    if (needed < 0.1f) {
        snprintf(buf, sizeof(buf), "Full!");
    } else if (pureRate > 0.01f) {
        snprintf(buf, sizeof(buf), "ETA:%dm", (int)(needed / pureRate));
    } else {
        snprintf(buf, sizeof(buf), "Idle");
    }
    d->drawString(x + 28, y + 54, buf);
}

// ── Frame 5: System health — battery, WiFi, uptime, offline buffer ─────────
static void frameSystemHealth(OLEDDisplay* d, OLEDDisplayUiState*, int16_t x, int16_t y) {
    char buf[22];

    d->setTextAlignment(TEXT_ALIGN_LEFT);
    d->setFont(ArialMT_Plain_10);
    d->drawString(x, y + 14, "SYS HEALTH");

    // Battery — voltage, %, charge state
    float v   = sensorVoltage_getVoltageV();
    float pct = sensorVoltage_getPercentPct();
    const char* st = sensorVoltage_getState();
    const char* stAbbr = (st[0] == 'C') ? "CHG" : (st[0] == 'D') ? "DIS" : "OK";
    snprintf(buf, sizeof(buf), "Bat:%.1fV %d%% %s", v, (int)pct, stAbbr);
    d->drawString(x, y + 26, buf);

    // WiFi — RSSI and signal %
    if (netWifi_isConnected()) {
        int rssi = WiFi.RSSI();
        int sig  = constrain(2 * (rssi + 100), 0, 100);
        snprintf(buf, sizeof(buf), "WiFi:%ddBm %d%%", rssi, sig);
    } else {
        snprintf(buf, sizeof(buf), "WiFi: OFFLINE");
    }
    d->drawString(x, y + 37, buf);

    // Uptime + offline buffer count
    uint32_t upSec = millis() / 1000;
    uint32_t days  = upSec / 86400;
    uint32_t hrs   = (upSec % 86400) / 3600;
    uint32_t mins  = (upSec % 3600) / 60;
    if (days > 0)
        snprintf(buf, sizeof(buf), "Up:%dd%dh  Buf:%d", (int)days, (int)hrs, storeSd_bufferCount());
    else
        snprintf(buf, sizeof(buf), "Up:%dh%dm  Buf:%d", (int)hrs, (int)mins, storeSd_bufferCount());
    d->drawString(x, y + 48, buf);
}

// ── Frame 6: Branding ──────────────────────────────────────────────────────
static void frameBranding(OLEDDisplay* d, OLEDDisplayUiState*, int16_t x, int16_t y) {
    d->drawXbm(x + (128 - TWWP_LOGO_W) / 2, y + 14,
               TWWP_LOGO_W, TWWP_LOGO_H, TWWP_LOGO_BITS);
    d->setTextAlignment(TEXT_ALIGN_CENTER);
    d->setFont(ArialMT_Plain_10);
    d->drawString(x + 64, y + 50, "Wholey Water Project");
}

// ── Frame / overlay registry ───────────────────────────────────────────────
static FrameCallback   s_frames[]   = { frameWqSummary, frameRemin,
                                         frameFlow, frameTank, frameSystemHealth,
                                         frameBranding };
static OverlayCallback s_overlays[] = { drawHeader };

// ── Internal updates ───────────────────────────────────────────────────────
static void updateTankLevel() {
    uint32_t now = millis();
    if (s_tankLastMs == 0) { s_tankLastMs = now; return; }

    float dt = (float)(now - s_tankLastMs) / 60000.0f;
    s_tankLastMs = now;
    if (dt > 1.0f) dt = 1.0f;  // cap jump after a pause

    s_tankLiters += sensorFlow_getRateLpm(2) * dt * TANK_RO_RECOVERY;
    s_tankLiters -= sensorFlow_getRateLpm(1) * dt;
    s_tankLiters  = constrain(s_tankLiters, 0.0f, TANK_CAPACITY_L);
}

static void saveTankIfDue() {
    uint32_t now = millis();
    if (now - s_tankSaveMs < TANK_NVS_SAVE_MS) return;
    s_tankSaveMs = now;
    s_prefs.begin("oled", false);
    s_prefs.putFloat("tank_l", s_tankLiters);
    s_prefs.end();
}

static void handleButton() {
    uint32_t now = millis();
    if (digitalRead(PIN_OLED_BTN) == LOW &&
        (now - s_btnLastMs > BTN_DEBOUNCE_MS)) {
        s_btnLastMs = now;
        s_ui.nextFrame();
    }
}

// ── Public interface ───────────────────────────────────────────────────────
bool displayOled_begin() {
    s_prefs.begin("oled", true);
    s_tankLiters = s_prefs.getFloat("tank_l", TANK_CAPACITY_L);
    s_prefs.end();
    s_tankLiters = constrain(s_tankLiters, 0.0f, TANK_CAPACITY_L);

    pinMode(PIN_OLED_BTN, INPUT_PULLUP);

    // Wire (main bus, GPIO9/GPIO3) already started by DS3231 driver — just probe
    Wire.beginTransmission(OLED_I2C_ADDR);
    if (Wire.endTransmission() != 0) {
        // Scan to report what is actually on the bus
        bool anyFound = false;
        for (uint8_t addr = 1; addr < 127; addr++) {
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0) {
                Serial.printf("[OLED] I2C device at 0x%02X (not the display)\n", addr);
                anyFound = true;
            }
        }
        if (!anyFound) Serial.println("[OLED] No I2C devices found — check wiring");
        else Serial.printf("[OLED] SSD1306 not found at 0x%02X — check address jumper\n", OLED_I2C_ADDR);
        return false;
    }

    s_ui.setTargetFPS(OLED_FPS);
    s_ui.setTimePerFrame(OLED_MS_PER_FRAME);
    s_ui.setFrameAnimation(SLIDE_LEFT);
    s_ui.setActiveSymbol(s_emptySymbol);    // hide page-indicator dots
    s_ui.setInactiveSymbol(s_emptySymbol);
    s_ui.setFrames(s_frames, 6);
    s_ui.setOverlays(s_overlays, 1);
    s_ui.init();
    s_disp.flipScreenVertically();

    s_ready = true;
    Serial.println("[OLED] ready");
    return true;
}

void displayOled_loop() {
    if (!s_ready) return;
    updateTankLevel();
    handleButton();
    s_ui.update();
    saveTankIfDue();
}

void displayOled_setTankLiters(float l) {
    s_tankLiters = constrain(l, 0.0f, TANK_CAPACITY_L);
}

float displayOled_getTankLiters() {
    return s_tankLiters;
}
