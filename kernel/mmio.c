#include "mmio.h"

#include <stddef.h>
#include <stdint.h>

enum {
    MMIO_EFI_DESCRIPTOR_MIN_SIZE = 40u,
    MMIO_EFI_TYPE_OFFSET = 0u,
    MMIO_EFI_PHYSICAL_START_OFFSET = 8u,
    MMIO_EFI_PAGE_COUNT_OFFSET = 24u,
    MMIO_EFI_RESERVED_MEMORY = 0u,
    MMIO_EFI_MEMORY_MAPPED_IO = 11u,
    MMIO_EFI_PAGE_SIZE = 4096u
};

static uint32_t mmio_read_u32_le(const uint8_t *bytes) {
    return (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8u)
        | ((uint32_t)bytes[2] << 16u)
        | ((uint32_t)bytes[3] << 24u);
}

static uint64_t mmio_read_u64_le(const uint8_t *bytes) {
    uint64_t value = 0u;

    for (size_t i = 0u; i < 8u; i++) {
        value |= (uint64_t)bytes[i] << (i * 8u);
    }
    return value;
}

bool astra_mmio_range_is_valid(
    const AstraBootInfo *boot_info,
    uint64_t address,
    uint64_t size
) {
    const uint8_t *map = NULL;
    size_t map_size = 0u;
    size_t descriptor_size = 0u;
    uint64_t range_end = 0u;
    bool found = false;

    if (boot_info == NULL ||
        boot_info->magic != ASTRA_BOOT_INFO_MAGIC ||
        boot_info->version != ASTRA_BOOT_INFO_VERSION ||
        boot_info->memory_map == NULL ||
        boot_info->memory_map_size == 0u ||
        boot_info->memory_map_size > ASTRA_BOOT_MEMORY_MAP_CAP_BYTES ||
        boot_info->memory_map_size > (uint64_t)SIZE_MAX ||
        boot_info->memory_descriptor_size < (uint64_t)MMIO_EFI_DESCRIPTOR_MIN_SIZE ||
        boot_info->memory_descriptor_size > (uint64_t)SIZE_MAX ||
        boot_info->memory_descriptor_size > boot_info->memory_map_size ||
        boot_info->memory_map_size % boot_info->memory_descriptor_size != 0u ||
        address == 0u ||
        size == 0u ||
        address > UINT64_MAX - size ||
        address > (uint64_t)UINTPTR_MAX ||
        (size - 1u) > (uint64_t)UINTPTR_MAX - address) {
        return false;
    }

    range_end = address + size;
    map = (const uint8_t *)boot_info->memory_map;
    map_size = (size_t)boot_info->memory_map_size;
    descriptor_size = (size_t)boot_info->memory_descriptor_size;

    for (size_t offset = 0u; offset < map_size; offset += descriptor_size) {
        const uint8_t *descriptor = &map[offset];
        uint32_t type = mmio_read_u32_le(&descriptor[MMIO_EFI_TYPE_OFFSET]);
        uint64_t base = mmio_read_u64_le(
            &descriptor[MMIO_EFI_PHYSICAL_START_OFFSET]
        );
        uint64_t pages = mmio_read_u64_le(
            &descriptor[MMIO_EFI_PAGE_COUNT_OFFSET]
        );
        uint64_t span = 0u;
        uint64_t descriptor_end = 0u;
        bool overlaps = false;

        if (pages == 0u) {
            continue;
        }
        if ((base % (uint64_t)MMIO_EFI_PAGE_SIZE) != 0u ||
            pages > (UINT64_MAX - base) / (uint64_t)MMIO_EFI_PAGE_SIZE) {
            return false;
        }

        span = pages * (uint64_t)MMIO_EFI_PAGE_SIZE;
        descriptor_end = base + span;
        overlaps = address < descriptor_end && range_end > base;
        if (!overlaps) {
            continue;
        }

        if ((type != (uint32_t)MMIO_EFI_MEMORY_MAPPED_IO &&
             type != (uint32_t)MMIO_EFI_RESERVED_MEMORY) ||
            found ||
            address < base ||
            range_end > descriptor_end) {
            return false;
        }
        found = true;
    }

    return found;
}
