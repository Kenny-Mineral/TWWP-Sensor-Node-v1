#include "display_oled.h"
#include <Wire.h>
#include <Preferences.h>
#include "SSD1306Wire.h"
#include "OLEDDisplayUi.h"
#include "pins.h"
#include "config.h"
#include "sensor_flow.h"
#include "sensor_yieryi.h"
#include "sensor_tds_meter.h"
#include "sensor_leak.h"
#include "net_wifi.h"
#include "net_ap.h"
#include "net_mqtt.h"
#include "store_sd.h"
#include "sensor_voltage.h"
#include "time_rtc.h"
#include "session_flow.h"
#include "wq_config.h"
#include "tank_monitor.h"
#include <WiFi.h>

// ── Config ─────────────────────────────────────────────────────────────────
#define OLED_I2C_ADDR       0x3C
#define OLED_FPS            30
#define OLED_MS_PER_FRAME   5000
#define BTN_DEBOUNCE_MS     200
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
// Tank level is read from tank_monitor.cpp (Ch2 - Ch1 integration).
static void frameTank(OLEDDisplay* d, OLEDDisplayUiState*, int16_t x, int16_t y) {
    char buf[20];

    d->setTextAlignment(TEXT_ALIGN_LEFT);
    d->setFont(ArialMT_Plain_10);
    d->drawString(x, y + 14, "STORAGE TANK");

    // Vertical level bar — outline 16px wide × 36px tall
    d->drawRect(x + 4, y + 25, 16, 36);
    float tankL    = tankMonitor_getLevelL();
    float tankCap  = tankMonitor_getCapacityL();
    float tankFrac = (tankCap > 0.0f) ? (tankL / tankCap) : 0.0f;
    if (tankFrac > 1.0f) tankFrac = 1.0f;
    int fillH = (int)(tankFrac * 34.0f);
    fillH = constrain(fillH, 0, 34);
    if (fillH > 0) {
        d->fillRect(x + 5, y + 26 + (34 - fillH), 14, fillH);
    }

    d->setFont(ArialMT_Plain_16);
    snprintf(buf, sizeof(buf), "%d%%", (int)(tankFrac * 100.0f));
    d->drawString(x + 28, y + 26, buf);

    d->setFont(ArialMT_Plain_10);
    snprintf(buf, sizeof(buf), "%.1f/%.0fL", tankL, tankCap);
    d->drawString(x + 28, y + 43, buf);

    float pureRate = sensorFlow_getRateLpm(2);  // Ch2 = RO output into the tank
    float needed   = tankCap - tankL;
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

// Tank level now managed by tank_monitor.cpp; no simulation here.

static void handleButton() {
    uint32_t now = millis();
    if (digitalRead(PIN_OLED_BTN) == LOW &&
        (now - s_btnLastMs > BTN_DEBOUNCE_MS)) {
        s_btnLastMs = now;
        s_ui.nextFrame();
    }
}

static void drawUploadMode() {
    char buf[32];
    uint32_t expires = netAp_getExpiresS();
    uint32_t mins = expires / 60;
    uint32_t secs = expires % 60;

    s_disp.clear();
    s_disp.setTextAlignment(TEXT_ALIGN_LEFT);
    s_disp.setFont(ArialMT_Plain_16);
    s_disp.drawString(0, 0, "UPLOAD MODE");
    s_disp.setFont(ArialMT_Plain_10);
    snprintf(buf, sizeof(buf), "SSID: %s", netAp_getSsid());
    s_disp.drawString(0, 20, buf);
    s_disp.drawString(0, 32, "IP: 192.168.4.1");
    snprintf(buf, sizeof(buf), "T: %lum %02lus", static_cast<unsigned long>(mins),
             static_cast<unsigned long>(secs));
    s_disp.drawString(0, 44, buf);
    snprintf(buf, sizeof(buf), "Clients: %u", static_cast<unsigned>(netAp_getClientCount()));
    s_disp.drawString(0, 56, buf);
    s_disp.display();
}

// ── Calibration mode screen ───────────────────────────────────────────────
// Returns true (and draws the cal screen) if any calibration is in progress.
// Priority: flow ch1 > flow ch2 > flow ch3 > TDS zone 0 > TDS zone 1.
static bool drawCalMode() {
    const char* fc1 = sensorFlow_getCalState(1);
    const char* fc2 = sensorFlow_getCalState(2);
    const char* fc3 = sensorFlow_getCalState(3);
    const char* tc0 = sensorTdsMeter_getCalState(0);
    const char* tc1 = sensorTdsMeter_getCalState(1);

    bool flowCh1Active = strcmp(fc1, "idle") != 0;
    bool flowCh2Active = strcmp(fc2, "idle") != 0;
    bool flowCh3Active = strcmp(fc3, "idle") != 0;
    bool tdsCh0Active  = strcmp(tc0, "idle") != 0;
    bool tdsCh1Active  = strcmp(tc1, "idle") != 0;

    if (!flowCh1Active && !flowCh2Active && !flowCh3Active && !tdsCh0Active && !tdsCh1Active) return false;

    char title[22], line1[22], line2[22], line3[22], line4[22];

    if (flowCh1Active || flowCh2Active || flowCh3Active) {
        uint8_t ch = flowCh1Active ? 1 : (flowCh2Active ? 2 : 3);
        const char* state = flowCh1Active ? fc1 : (flowCh2Active ? fc2 : fc3);
        bool done      = strcmp(state, "done") == 0;
        bool timedOut  = strcmp(state, "timed_out") == 0;
        bool tooFew    = strcmp(state, "too_few_pulses") == 0;

        snprintf(title, sizeof(title), "FLOW CAL  CH%d", ch);
        if (timedOut) {
            strlcpy(line1, "!! NO FLOW !!", sizeof(line1));
            strlcpy(line2, "Auto-aborted", sizeof(line2));
            strlcpy(line3, "Open tap then", sizeof(line3));
            strlcpy(line4, "press START again", sizeof(line4));
        } else if (tooFew) {
            strlcpy(line1, "TOO FEW PULSES", sizeof(line1));
            snprintf(line2, sizeof(line2), "Got:%llu Need:%lu",
                     (unsigned long long)sensorFlow_getCalPulsesSinceStart(ch),
                     (unsigned long)FLOW_CAL_MIN_PULSES);
            strlcpy(line3, "Let more water", sizeof(line3));
            strlcpy(line4, "flow then COMMIT", sizeof(line4));
        } else if (done) {
            strlcpy(line1, "REVIEW RESULT", sizeof(line1));
            snprintf(line2, sizeof(line2), "New K: %.0f", sensorFlow_getCalSuggestedK(ch));
            snprintf(line3, sizeof(line3), "Cur K: %.0f", sensorFlow_getKFactor(ch));
            strlcpy(line4, "Accept/Abort in HA", sizeof(line4));
        } else {
            int secsLeft = sensorFlow_getCalSecsUntilTimeout(ch);
            if (secsLeft >= 0 && secsLeft <= 30) {
                // Countdown: no flow detected and running out of time
                snprintf(line1, sizeof(line1), "WAITING FOR FLOW");
                snprintf(line2, sizeof(line2), "Pulses:%llu",
                         (unsigned long long)sensorFlow_getCalPulsesSinceStart(ch));
                snprintf(line3, sizeof(line3), "%.3f L/min", sensorFlow_getRateLpm(ch));
                snprintf(line4, sizeof(line4), "Abort in %ds", secsLeft);
            } else {
                strlcpy(line1, "FILLING...", sizeof(line1));
                snprintf(line2, sizeof(line2), "Pulses:%llu",
                         (unsigned long long)sensorFlow_getCalPulsesSinceStart(ch));
                snprintf(line3, sizeof(line3), "%.3f L/min", sensorFlow_getRateLpm(ch));
                snprintf(line4, sizeof(line4), "RefVol:%.2fL", sensorFlow_getCalRefVol(ch));
            }
        }
    } else {
        uint8_t zone = tdsCh0Active ? 0 : 1;
        const char* state = tdsCh0Active ? tc0 : tc1;
        const char* zoneName = (zone == 0) ? "PRE-RO" : "POST-RO";
        bool done = strcmp(state, "done") == 0;

        snprintf(title, sizeof(title), "TDS CAL %s", zoneName);
        if (done) {
            strlcpy(line1, "REVIEW RESULT", sizeof(line1));
            snprintf(line2, sizeof(line2), "New F:%.4f", sensorTdsMeter_getCalSuggestedFactor(zone));
            snprintf(line3, sizeof(line3), "Cur F:%.4f", sensorTdsMeter_getEcCalFactor(zone));
            strlcpy(line4, "Accept/Abort in HA", sizeof(line4));
        } else {
            strlcpy(line1, "STIR IN SOLUTION", sizeof(line1));
            snprintf(line2, sizeof(line2), "Raw:%.0f uS", sensorTdsMeter_getRawEc(zone));
            snprintf(line3, sizeof(line3), "Ref:%.0f uS", sensorTdsMeter_getCalRefEc(zone));
            strlcpy(line4, "", sizeof(line4));
        }
    }

    s_disp.clear();
    drawHeader(&s_disp, nullptr);

    s_disp.setTextAlignment(TEXT_ALIGN_LEFT);
    s_disp.setFont(ArialMT_Plain_10);
    s_disp.drawString(0, 14, title);
    s_disp.drawString(0, 25, line1);
    s_disp.drawString(0, 36, line2);
    s_disp.drawString(0, 47, line3);
    s_disp.drawString(0, 56, line4);
    s_disp.display();
    return true;
}

// ── Public interface ───────────────────────────────────────────────────────
bool displayOled_begin() {
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
    handleButton();
    if (netAp_isActive()) {
        drawUploadMode();
        return;
    }
    if (drawCalMode()) {
        return;
    }
    s_ui.update();
}
