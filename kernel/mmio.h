#ifndef ASTRAOS_MMIO_H
#define ASTRAOS_MMIO_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_info.h"

/*
 * Rejects overlap with RAM or any conflicting memory-map descriptor. If one
 * descriptor overlaps the requested span, it must be EFI_MEMORY_MAPPED_IO or
 * EfiReservedMemoryType and contain the complete span. A missing descriptor is
 * permitted only because callers first validate a specific FDT/ACPI device.
 * This examines metadata only; it does not access the requested MMIO address.
 */
bool astra_mmio_range_is_valid(
    const AstraBootInfo *boot_info,
    uint64_t address,
    uint64_t size
);

#endif
