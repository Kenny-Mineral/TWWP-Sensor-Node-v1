#include "status_led.h"

#include <FastLED.h>

#include "config.h"
#include "pins.h"

static CRGB ledPixels[1];
static LedState currentState = LedState::OFF;
static unsigned long lastFrameMs = 0;
static bool ledReady = false;

static void showColor(const CRGB& color) {
    ledPixels[0] = color;
    FastLED.show();
}

static CRGB pulseColor(const CRGB& color, uint8_t minBrightness, uint8_t maxBrightness, uint16_t periodMs) {
    uint16_t phase = static_cast<uint16_t>(millis() % periodMs);
    uint8_t level = (phase < periodMs / 2)
                        ? map(phase, 0, periodMs / 2, minBrightness, maxBrightness)
                        : map(phase, periodMs / 2, periodMs, maxBrightness, minBrightness);
    CRGB out = color;
    out.nscale8_video(level);
    return out;
}

bool statusLed_begin() {
    FastLED.addLeds<WS2812, PIN_STATUS_LED, GRB>(ledPixels, 1);
    FastLED.setBrightness(96);
    FastLED.clear(true);
    ledReady = true;
    currentState = LedState::BOOTING;
    return true;
}

void statusLed_loop() {
    if (!ledReady) {
        return;
    }

    unsigned long now = millis();
    if (now - lastFrameMs < 20) {
        return;
    }
    lastFrameMs = now;

    switch (currentState) {
        case LedState::OFF:
            showColor(CRGB::Black);
            break;
        case LedState::BOOTING:
            showColor(pulseColor(CRGB::Blue, 8, 96, 1800));
            break;
        case LedState::WIFI_CONNECTING:
            showColor((now / 350) % 2 ? CRGB(255, 180, 0) : CRGB::Black);
            break;
        case LedState::MQTT_CONNECTING:
            showColor((now / 250) % 2 ? CRGB(255, 96, 0) : CRGB::Black);
            break;
        case LedState::ONLINE:
            showColor(CRGB::Green);
            break;
        case LedState::LEAK_DETECTED:
            showColor(CRGB::Red);
            break;
        case LedState::ERROR:
            showColor((now / 125) % 2 ? CRGB::Red : CRGB::Black);
            break;
    }
}

void statusLed_setState(LedState state) {
    currentState = state;
    if (ledReady) {
        statusLed_loop();
    }
}
