#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "HardwareSerial.h"

typedef unsigned long ulong;
typedef uint8_t       byte;

// GPIO constants
#define OUTPUT  0x01
#define INPUT   0x00
#define HIGH    0x01
#define LOW     0x00

// Minimal Print stub used by store_sd.h and similar headers
struct Print {
    void println(const char*) {}
    void print(const char*) {}
    template<typename T> void println(T) {}
    template<typename T> void print(T) {}
    void printf(const char*, ...) {}
};

// Controllable fake clock — tests call setMillis() / advanceMillis()
static unsigned long g_millis = 0;
inline unsigned long millis()                    { return g_millis; }
inline void          setMillis(unsigned long ms) { g_millis = ms; }
inline void          advanceMillis(unsigned long ms) { g_millis += ms; }

struct _FakeSerial {
    void begin(unsigned long) {}
    void println(const char*) {}
    void print(const char*) {}
    void printf(const char* fmt, ...) {
        (void)fmt; /* discard */
    }
    template<typename T> void println(T) {}
    template<typename T> void print(T) {}
};
static _FakeSerial Serial;
