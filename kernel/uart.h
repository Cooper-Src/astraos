#ifndef ASTRAOS_UART_H
#define ASTRAOS_UART_H

#include <stddef.h>
#include <stdint.h>

/* QEMU virt PL011 UART at 0x09000000; not a generic physical-device driver. */
void astra_uart_init(void);
void astra_uart_write(const char *data, size_t length);
void astra_uart_puts(const char *text);
void astra_uart_put_byte(uint8_t value);
uint8_t astra_uart_get_byte(void);
void astra_uart_put_u32(uint32_t value);
void astra_uart_put_u64(uint64_t value);
void astra_uart_put_hex(uint64_t value);

#endif
