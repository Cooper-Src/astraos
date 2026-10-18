#ifndef ASTRAOS_ACPI_H
#define ASTRAOS_ACPI_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_info.h"

typedef struct AstraAcpiSpcr {
    uint64_t base_address;
} AstraAcpiSpcr;

/* Finds a checksummed ACPI SPCR table for a memory-mapped ARM PL011 UART.
 * Reads only ACPI-reclaim/NVS memory ranges in the boot memory map.
 * Returns false on absent, malformed, unsupported, or inaccessible tables;
 * spcr_out is unchanged on failure. */
bool astra_acpi_find_pl011_uart(
    const AstraBootInfo *boot_info,
    AstraAcpiSpcr *spcr_out
);

#endif
