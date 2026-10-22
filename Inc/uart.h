#ifndef UART_H
#define UART_H

#include <stdbool.h>
#include <stdint.h>

void uart_init(void);

bool uart_write_byte(uint8_t value);
bool uart_write_text(const char *text);
bool uart_write_u32(uint32_t value);
bool uart_write_i32(int32_t value);
bool uart_write_hex32(uint32_t value);

#endif