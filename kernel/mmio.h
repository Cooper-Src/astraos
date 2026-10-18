#ifndef ASTRAOS_MMIO_H
#define ASTRAOS_MMIO_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_info.h"

/*
 * Checks that the complete byte span is contained in one EFI_MEMORY_MAPPED_IO
 * descriptor and does not overlap any descriptor of another type.
 * This examines metadata only; it does not access the requested MMIO address.
 */
bool astra_mmio_range_is_valid(
    const AstraBootInfo *boot_info,
    uint64_t address,
    uint64_t size
);

#endif
