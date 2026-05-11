#include "rs485_mux.h"

#include <Arduino.h>
#include <HardwareSerial.h>
#include <driver/uart.h>
#include <string.h>

#include "pins.h"
#include "sensor_tds_meter.h"

static HardwareSerial rs485Serial(1);

static const uint32_t RS485_BAUD = 9600;

// ── Modbus FIFO ──────────────────────────────────────────────────────────────
// Binary bytes that do not belong to a $WM ASCII frame are queued here for
// sensor_yieryi to consume via rs485Mux_available() / rs485Mux_read().

static uint8_t modbusFifo[64];
static uint8_t mbHead  = 0;
static uint8_t mbTail  = 0;
static uint8_t mbCount = 0;

static void mbPush(uint8_t b) {
    if (mbCount < sizeof(modbusFifo)) {
        modbusFifo[mbTail] = b;
        mbTail = (mbTail + 1) % (uint8_t)sizeof(modbusFifo);
        mbCount++;
    }
    // silently drop on overflow — Modbus CRC will catch the truncated frame
}

// ── ASCII frame accumulator ──────────────────────────────────────────────────
// A frame starts when '$' is seen at the beginning of a new sequence.
// It ends when '\n' arrives, the buffer fills, or 200 ms elapses (timeout guard
// against a corrupted partial frame locking out the Modbus FIFO).

static char          lineBuf[128];
static uint8_t       lineLen     = 0;
static bool          lineStarted = false;
static unsigned long lineStartMs = 0;

// ── Public API ───────────────────────────────────────────────────────────────

void rs485Mux_begin() {
    rs485Serial.begin(RS485_BAUD, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
    rs485Serial.setPins(-1, -1, -1, PIN_RS485_EN);
    rs485Serial.setMode(UART_MODE_RS485_HALF_DUPLEX);
    Serial.println("[MUX] RS485 UART1 ready 9600 8N1");
}

void rs485Mux_loop() {
    while (rs485Serial.available() > 0) {
        uint8_t b = (uint8_t)rs485Serial.read();

        if (lineStarted) {
            if (lineLen < (uint8_t)(sizeof(lineBuf) - 1)) {
                lineBuf[lineLen++] = (char)b;
            }

            bool eol     = (b == '\n');
            bool full    = (lineLen >= (uint8_t)(sizeof(lineBuf) - 1));
            bool timeout = ((millis() - lineStartMs) > 200UL);

            if (eol || full || timeout) {
                lineBuf[lineLen] = '\0';
                if (eol && lineLen >= 5 &&
                    lineBuf[0] == '$' && lineBuf[1] == 'W' &&
                    lineBuf[2] == 'M' && lineBuf[3] == ',') {
                    sensorTdsMeter_onFrame(lineBuf);
                }
                lineStarted = false;
                lineLen     = 0;
            }
        } else {
            if (b == '$') {
                lineStarted  = true;
                lineStartMs  = millis();
                lineBuf[0]   = '$';
                lineLen      = 1;
            } else {
                mbPush(b);
            }
        }
    }
}

int rs485Mux_available() {
    return (int)mbCount;
}

uint8_t rs485Mux_read() {
    if (mbCount == 0) return 0;
    uint8_t b = modbusFifo[mbHead];
    mbHead = (mbHead + 1) % (uint8_t)sizeof(modbusFifo);
    mbCount--;
    return b;
}

void rs485Mux_write(const uint8_t* data, size_t len) {
    rs485Serial.write(data, len);
    rs485Serial.flush();
}
