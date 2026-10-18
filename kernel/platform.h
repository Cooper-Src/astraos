#ifndef ASTRAOS_PLATFORM_H
#define ASTRAOS_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_info.h"

/* Resolve the FDT stdout path (including aliases) to a validated PL011 MMIO
 * range. This metadata-only function does not access device registers. */
bool astra_platform_find_pl011_uart_from_fdt(
    const AstraBootInfo *boot_info,
    uint64_t *base_out
);

/* Prefer validated FDT discovery, then fall back to a validated ACPI SPCR
 * describing an ARM PL011 UART. base_out is unchanged on failure. */
bool astra_platform_find_pl011_uart(const AstraBootInfo *boot_info, uint64_t *base_out);

#endif
