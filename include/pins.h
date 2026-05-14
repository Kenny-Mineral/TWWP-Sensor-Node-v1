#pragma once

// M0 — active
#define PIN_LEAK_DO      6   // MH-RD leak detector, INPUT_PULLUP, LOW=wet
#define PIN_I2C_SDA      9   // DS3231 RTC + microSD combo module + ADS1115 @ 0x48 (M2.5)
#define PIN_I2C_SCL      3   // DS3231 RTC + microSD combo module + ADS1115 @ 0x48 (M2.5) — strapping pin, fine as driven clock
#define PIN_SD_MOSI     11   // SPI2
#define PIN_SD_SCK      12
#define PIN_SD_MISO     13
#define PIN_SD_CS       14
#define PIN_STATUS_LED  48   // WS2812 onboard RGB

// M1 — Hall flow sensors
#define PIN_FLOW_1       4   // USN-HS06PE — purified output, INPUT_PULLUP, interrupt FALLING
#define PIN_FLOW_2       5   // USN-HS06PS — raw input,      INPUT_PULLUP, interrupt FALLING

// M2 — Pressure (stub)
#define PIN_PRESSURE     7   // Analog ADC, 0–5V via 2:1 divider → 0–2.5V

// Display — SSD1306 OLED on main Wire bus (shared with DS3231, no address conflict)
#define PIN_OLED_SDA     PIN_I2C_SDA   // GPIO9
#define PIN_OLED_SCL     PIN_I2C_SCL   // GPIO3
#define PIN_OLED_BTN    10   // Tactile button INPUT_PULLUP, LOW=pressed — cycles display frame

// M3 — Valve relay
#define PIN_VALVE        8   // Relay output — active-low (LOW=open). Current load: 12V LED / future ball valve.

// M5 — RS485 water quality sensor (blocked, stub)
#define PIN_RS485_TX    17   // UART1
#define PIN_RS485_RX    18
#define PIN_RS485_EN    21   // Hardware auto DE/RE via UART_MODE_RS485_HALF_DUPLEX

// Boot button — used as reset-credentials gesture (hold >5s)
#define PIN_RESET_CREDS  0
