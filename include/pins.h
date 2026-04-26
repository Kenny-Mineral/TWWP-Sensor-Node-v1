#pragma once

// M0 — active
#define PIN_LEAK_DO      6   // MH-RD leak detector, INPUT_PULLUP, LOW=wet
#define PIN_I2C_SDA      9   // DS1307 RTC + microSD combo module
#define PIN_I2C_SCL      3   // DS1307 RTC + microSD combo module (strapping pin, fine as driven clock)
#define PIN_SD_MOSI     11   // SPI2
#define PIN_SD_SCK      12
#define PIN_SD_MISO     13
#define PIN_SD_CS       14
#define PIN_STATUS_LED  48   // WS2812 onboard RGB

// M1 — Hall flow sensors (stub for now)
#define PIN_FLOW_1       4
#define PIN_FLOW_2       5

// M2 — Pressure + temp (stub)
#define PIN_PRESSURE     7   // Analog ADC, 0–5V via 2:1 divider → 0–2.5V
#define PIN_TEMP_1WIRE  10   // DS18B20 OneWire

// M3 — Solenoid (stub)
#define PIN_SOLENOID     8   // N-MOSFET gate driver

// M5 — RS485 water quality sensor (blocked, stub)
#define PIN_RS485_TX    17   // UART1
#define PIN_RS485_RX    18
#define PIN_RS485_EN    21   // Hardware auto DE/RE via UART_MODE_RS485_HALF_DUPLEX

// Boot button — used as reset-credentials gesture (hold >5s)
#define PIN_RESET_CREDS  0
