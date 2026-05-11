#pragma once

#include <stddef.h>
#include <stdint.h>

// RS485 protocol multiplexer — owns UART1.
//
// Routes incoming bytes to the right consumer:
//   - Bytes starting with '$' are accumulated as ASCII lines and dispatched
//     to sensorTdsMeter_onFrame() when complete.
//   - All other bytes are queued in the Modbus FIFO for sensor_yieryi.
//
// Must be initialised (rs485Mux_begin) before any driver that uses RS485.
// Must be called first in loop() so the Modbus FIFO is populated before
// sensorYieryi_loop() reads from it.

void    rs485Mux_begin();
void    rs485Mux_loop();
int     rs485Mux_available();           // bytes waiting in Modbus FIFO
uint8_t rs485Mux_read();               // pop one byte from Modbus FIFO
void    rs485Mux_write(const uint8_t* data, size_t len);  // write + flush
