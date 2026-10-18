#ifndef ASTRAOS_UART_H
#define ASTRAOS_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_info.h"

/* Resolve a PL011 from a validated FDT without touching device registers.
 * base_out is unchanged when discovery fails. */
bool astra_platform_find_pl011_uart(const AstraBootInfo *boot_info, uint64_t *base_out);

/* Initializes the discovered PL011. Returns false without touching UART MMIO
 * when no supported device is found. */
bool astra_uart_init(const AstraBootInfo *boot_info);
void astra_uart_write(const char *data, size_t length);
void astra_uart_puts(const char *text);
void astra_uart_put_byte(uint8_t value);
uint8_t astra_uart_get_byte(void);
void astra_uart_put_u32(uint32_t value);
void astra_uart_put_u64(uint64_t value);
void astra_uart_put_hex(uint64_t value);

#endif
