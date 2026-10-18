#include "memory.h"

enum {
    EFI_MEMORY_DESCRIPTOR_MIN_SIZE = 40u,
    EFI_MEMORY_TYPE_OFFSET = 0u,
    EFI_MEMORY_PHYSICAL_START_OFFSET = 8u,
    EFI_MEMORY_NUMBER_OF_PAGES_OFFSET = 24u,
    EFI_CONVENTIONAL_MEMORY = 7u
};

static bool bitmap_test(const uint8_t *bitmap, size_t page_index) {
    size_t byte_index = page_index / 8u;
    uint8_t mask = (uint8_t)(1u << (page_index % 8u));

    return (bitmap[byte_index] & mask) != 0;
}

static void bitmap_set(uint8_t *bitmap, size_t page_index) {
    size_t byte_index = page_index / 8u;
    uint8_t mask = (uint8_t)(1u << (page_index % 8u));

    bitmap[byte_index] = (uint8_t)(bitmap[byte_index] | mask);
}

static void bitmap_clear(uint8_t *bitmap, size_t page_index) {
    size_t byte_index = page_index / 8u;
    uint8_t mask = (uint8_t)(1u << (page_index % 8u));

    bitmap[byte_index] = (uint8_t)(bitmap[byte_index] & (uint8_t)~mask);
}

/* UEFI descriptors are little-endian on AstraOS's supported AArch64 target. */
static uint32_t read_u32_le(const uint8_t *bytes) {
    return (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8)
        | ((uint32_t)bytes[2] << 16)
        | ((uint32_t)bytes[3] << 24);
}

static uint64_t read_u64_le(const uint8_t *bytes) {
    uint64_t value = 0;

    for (size_t i = 0; i < 8u; i++) {
        value |= (uint64_t)bytes[i] << (i * 8u);
    }
    return value;
}

static bool add_u64(uint64_t left, uint64_t right, uint64_t *sum_out) {
    if (sum_out == NULL || left > UINT64_MAX - right) {
        return false;
    }
    *sum_out = left + right;
    return true;
}

/*
 * Validates one UEFI descriptor's half-open physical range.
 * Empty descriptors carry no address span; non-empty spans must be page-aligned
 * and have a representable end address.
 */
static bool memory_descriptor_range(
    const uint8_t *descriptor,
    uint64_t *start_out,
    uint64_t *end_out,
    bool *empty_out
) {
    uint64_t physical_start = 0u;
    uint64_t page_count = 0u;

    if (descriptor == NULL || start_out == NULL || end_out == NULL ||
        empty_out == NULL) {
        return false;
    }

    physical_start = read_u64_le(
        &descriptor[EFI_MEMORY_PHYSICAL_START_OFFSET]
    );
    page_count = read_u64_le(
        &descriptor[EFI_MEMORY_NUMBER_OF_PAGES_OFFSET]
    );
    *start_out = physical_start;
    *end_out = physical_start;
    *empty_out = page_count == 0u;

    if (page_count == 0u) {
        return true;
    }
    if ((physical_start % ASTRA_MEMORY_PAGE_SIZE) != 0u ||
        page_count > (UINT64_MAX - physical_start) / ASTRA_MEMORY_PAGE_SIZE) {
        return false;
    }

    *end_out = physical_start + page_count * ASTRA_MEMORY_PAGE_SIZE;
    return true;
}

/*
 * UEFI descriptors must not claim overlapping physical spans, even when one
 * descriptor is reserved/MMIO and lies above the allocator's managed range.
 * The map-size cap bounds this pairwise validation to a few thousand entries.
 */
static bool memory_map_is_nonoverlapping(
    const uint8_t *map,
    size_t map_size,
    size_t descriptor_size
) {
    if (map == NULL || descriptor_size < (size_t)EFI_MEMORY_DESCRIPTOR_MIN_SIZE ||
        descriptor_size > map_size || map_size % descriptor_size != 0u) {
        return false;
    }

    for (size_t offset = 0u; offset < map_size; offset += descriptor_size) {
        const uint8_t *left = &map[offset];
        uint64_t left_start = 0u;
        uint64_t left_end = 0u;
        bool left_empty = false;

        if (!memory_descriptor_range(
            left, &left_start, &left_end, &left_empty
        )) {
            return false;
        }
        if (left_empty) {
            continue;
        }

        for (size_t other_offset = offset + descriptor_size;
            other_offset < map_size;
            other_offset += descriptor_size) {
            const uint8_t *right = &map[other_offset];
            uint64_t right_start = 0u;
            uint64_t right_end = 0u;
            bool right_empty = false;

            if (!memory_descriptor_range(
                right, &right_start, &right_end, &right_empty
            )) {
                return false;
            }
            if (right_empty) {
                continue;
            }
            if (left_start < right_end && right_start < left_end) {
                return false;
            }
        }
    }

    return true;
}

static void clear_allocator(AstraMemory *memory) {
    for (size_t i = 0; i < ASTRA_MEMORY_BITMAP_BYTES; i++) {
        memory->managed_bitmap[i] = 0;
        memory->free_bitmap[i] = 0;
    }
    memory->stats = (AstraMemoryStats){0};
    memory->initialized = false;
}

AstraMemoryResult astra_memory_init(AstraMemory *memory, const AstraBootInfo *boot_info) {
    const uint8_t *map = NULL;
    size_t map_size = 0;
    size_t descriptor_size = 0;
    size_t descriptor_offset = 0;

    if (memory == NULL) {
        return ASTRA_MEMORY_INVALID_ARGUMENT;
    }
    clear_allocator(memory);

    if (boot_info == NULL) {
        return ASTRA_MEMORY_INVALID_ARGUMENT;
    }
    if (boot_info->magic != ASTRA_BOOT_INFO_MAGIC ||
        boot_info->version != ASTRA_BOOT_INFO_VERSION) {
        return ASTRA_MEMORY_INVALID_BOOT_INFO;
    }
    if (boot_info->memory_map == NULL ||
        boot_info->memory_map_size == 0 ||
        boot_info->memory_map_size > ASTRA_BOOT_MEMORY_MAP_CAP_BYTES ||
        boot_info->memory_map_size > (uint64_t)SIZE_MAX ||
        boot_info->memory_descriptor_size < (uint64_t)EFI_MEMORY_DESCRIPTOR_MIN_SIZE ||
        boot_info->memory_descriptor_size > (uint64_t)SIZE_MAX ||
        boot_info->memory_descriptor_size > boot_info->memory_map_size ||
        boot_info->memory_map_size % boot_info->memory_descriptor_size != 0) {
        return ASTRA_MEMORY_INVALID_MAP;
    }

    map = (const uint8_t *)boot_info->memory_map;
    map_size = (size_t)boot_info->memory_map_size;
    descriptor_size = (size_t)boot_info->memory_descriptor_size;
    if (!memory_map_is_nonoverlapping(map, map_size, descriptor_size)) {
        return ASTRA_MEMORY_INVALID_MAP;
    }
    memory->stats.descriptor_count =
        boot_info->memory_map_size / boot_info->memory_descriptor_size;

    while (descriptor_offset < map_size) {
        const uint8_t *descriptor = &map[descriptor_offset];
        uint32_t type = read_u32_le(&descriptor[EFI_MEMORY_TYPE_OFFSET]);

        if (type == EFI_CONVENTIONAL_MEMORY) {
            uint64_t physical_start =
                read_u64_le(&descriptor[EFI_MEMORY_PHYSICAL_START_OFFSET]);
            uint64_t page_count =
                read_u64_le(&descriptor[EFI_MEMORY_NUMBER_OF_PAGES_OFFSET]);
            uint64_t first_page = 0;
            uint64_t managed_count = 0;
            uint64_t ignored_count = 0;
            uint64_t new_managed_pages = 0;
            uint64_t new_free_pages = 0;
            uint64_t new_ignored_pages = 0;

            if (page_count != 0) {
                if ((physical_start % ASTRA_MEMORY_PAGE_SIZE) != 0 ||
                    page_count > (UINT64_MAX - physical_start) / ASTRA_MEMORY_PAGE_SIZE) {
                    return ASTRA_MEMORY_INVALID_MAP;
                }

                first_page = physical_start / ASTRA_MEMORY_PAGE_SIZE;
                if (first_page >= ASTRA_MEMORY_MAX_PAGES) {
                    ignored_count = page_count;
                } else {
                    uint64_t pages_until_limit = ASTRA_MEMORY_MAX_PAGES - first_page;
                    managed_count = page_count < pages_until_limit
                        ? page_count
                        : pages_until_limit;
                    ignored_count = page_count - managed_count;
                }

                if (!add_u64(memory->stats.managed_pages, managed_count,
                        &new_managed_pages) ||
                    !add_u64(memory->stats.free_pages, managed_count, &new_free_pages) ||
                    !add_u64(memory->stats.ignored_pages, ignored_count, &new_ignored_pages)) {
                    return ASTRA_MEMORY_INVALID_MAP;
                }

                for (uint64_t i = 0; i < managed_count; i++) {
                    size_t page_index = (size_t)(first_page + i);
                    if (bitmap_test(memory->managed_bitmap, page_index)) {
                        return ASTRA_MEMORY_INVALID_MAP;
                    }
                }
                for (uint64_t i = 0; i < managed_count; i++) {
                    size_t page_index = (size_t)(first_page + i);
                    bitmap_set(memory->managed_bitmap, page_index);
                    bitmap_set(memory->free_bitmap, page_index);
                }

                memory->stats.managed_pages = new_managed_pages;
                memory->stats.free_pages = new_free_pages;
                memory->stats.ignored_pages = new_ignored_pages;
            }
        }

        descriptor_offset += descriptor_size;
    }

    /* Physical address zero is never returned as an allocated page. */
    if (bitmap_test(memory->managed_bitmap, 0u) &&
        bitmap_test(memory->free_bitmap, 0u)) {
        if (memory->stats.free_pages == 0) {
            return ASTRA_MEMORY_INVALID_MAP;
        }
        bitmap_clear(memory->free_bitmap, 0u);
        memory->stats.free_pages--;
    }

    memory->initialized = true;
    return ASTRA_MEMORY_OK;
}

AstraMemoryResult astra_memory_alloc_page(AstraMemory *memory, uint64_t *address_out) {
    if (memory == NULL || address_out == NULL) {
        return ASTRA_MEMORY_INVALID_ARGUMENT;
    }
    if (!memory->initialized) {
        return ASTRA_MEMORY_NOT_INITIALIZED;
    }

    for (size_t page_index = 1u; page_index < (size_t)ASTRA_MEMORY_MAX_PAGES;
        page_index++) {
        if (bitmap_test(memory->free_bitmap, page_index)) {
            if (memory->stats.free_pages == 0) {
                return ASTRA_MEMORY_INVALID_MAP;
            }
            bitmap_clear(memory->free_bitmap, page_index);
            memory->stats.free_pages--;
            *address_out = (uint64_t)page_index * ASTRA_MEMORY_PAGE_SIZE;
            return ASTRA_MEMORY_OK;
        }
    }
    return ASTRA_MEMORY_OUT_OF_MEMORY;
}

AstraMemoryResult astra_memory_free_page(AstraMemory *memory, uint64_t address) {
    size_t page_index = 0;

    if (memory == NULL || address == 0 ||
        (address % ASTRA_MEMORY_PAGE_SIZE) != 0) {
        return ASTRA_MEMORY_INVALID_ARGUMENT;
    }
    if (!memory->initialized) {
        return ASTRA_MEMORY_NOT_INITIALIZED;
    }
    if (address >= ASTRA_MEMORY_ADDRESS_LIMIT) {
        return ASTRA_MEMORY_ADDRESS_OUT_OF_RANGE;
    }

    page_index = (size_t)(address / ASTRA_MEMORY_PAGE_SIZE);
    if (!bitmap_test(memory->managed_bitmap, page_index)) {
        return ASTRA_MEMORY_NOT_MANAGED;
    }
    if (bitmap_test(memory->free_bitmap, page_index)) {
        return ASTRA_MEMORY_DOUBLE_FREE;
    }
    if (memory->stats.free_pages >= memory->stats.managed_pages) {
        return ASTRA_MEMORY_INVALID_MAP;
    }

    bitmap_set(memory->free_bitmap, page_index);
    memory->stats.free_pages++;
    return ASTRA_MEMORY_OK;
}

AstraMemoryResult astra_memory_get_stats(
    const AstraMemory *memory,
    AstraMemoryStats *stats_out
) {
    if (memory == NULL || stats_out == NULL) {
        return ASTRA_MEMORY_INVALID_ARGUMENT;
    }
    if (!memory->initialized) {
        return ASTRA_MEMORY_NOT_INITIALIZED;
    }
    *stats_out = memory->stats;
    return ASTRA_MEMORY_OK;
}

const char *astra_memory_result_name(AstraMemoryResult result) {
    switch (result) {
        case ASTRA_MEMORY_OK:
            return "success";
        case ASTRA_MEMORY_INVALID_ARGUMENT:
            return "invalid argument";
        case ASTRA_MEMORY_INVALID_BOOT_INFO:
            return "invalid boot information";
        case ASTRA_MEMORY_INVALID_MAP:
            return "invalid or overlapping memory map";
        case ASTRA_MEMORY_NOT_INITIALIZED:
            return "allocator not initialized";
        case ASTRA_MEMORY_OUT_OF_MEMORY:
            return "out of physical pages";
        case ASTRA_MEMORY_ADDRESS_OUT_OF_RANGE:
            return "physical address is above the 4 GiB limit";
        case ASTRA_MEMORY_NOT_MANAGED:
            return "physical page is not managed";
        case ASTRA_MEMORY_DOUBLE_FREE:
            return "physical page is already free";
    }
    return "unknown memory allocator error";
}
