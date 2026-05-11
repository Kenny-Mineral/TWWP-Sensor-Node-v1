#pragma once
#include <stdint.h>
#include <stddef.h>
#include "driver/uart.h"

#define SERIAL_8N1 0x800001cUL

class HardwareSerial {
public:
    explicit HardwareSerial(int) {}
    void   begin(unsigned long, uint32_t, int8_t, int8_t) {}
    void   setPins(int8_t, int8_t, int8_t, int8_t) {}
    void   setMode(uart_mode_t) {}
    int    available() { return 0; }
    int    read()      { return -1; }
    size_t write(const uint8_t*, size_t n) { return n; }
    void   flush() {}
};
