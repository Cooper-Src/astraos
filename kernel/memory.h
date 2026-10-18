#ifndef ASTRAOS_MEMORY_H
#define ASTRAOS_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_info.h"

/* The early allocator only manages page frames below 4 GiB. */
#define ASTRA_MEMORY_PAGE_SIZE UINT64_C(4096)
#define ASTRA_MEMORY_ADDRESS_LIMIT UINT64_C(0x100000000)
#define ASTRA_MEMORY_MAX_PAGES (ASTRA_MEMORY_ADDRESS_LIMIT / ASTRA_MEMORY_PAGE_SIZE)
#define ASTRA_MEMORY_BITMAP_BYTES ((size_t)(ASTRA_MEMORY_MAX_PAGES / UINT64_C(8)))

typedef enum AstraMemoryResult {
    ASTRA_MEMORY_OK = 0,
    ASTRA_MEMORY_INVALID_ARGUMENT,
    ASTRA_MEMORY_INVALID_BOOT_INFO,
    ASTRA_MEMORY_INVALID_MAP,
    ASTRA_MEMORY_NOT_INITIALIZED,
    ASTRA_MEMORY_OUT_OF_MEMORY,
    ASTRA_MEMORY_ADDRESS_OUT_OF_RANGE,
    ASTRA_MEMORY_NOT_MANAGED,
    ASTRA_MEMORY_DOUBLE_FREE
} AstraMemoryResult;

typedef struct AstraMemoryStats {
    uint64_t descriptor_count;
    uint64_t managed_pages;
    uint64_t free_pages;
    uint64_t ignored_pages;
} AstraMemoryStats;

/*
 * Caller-owned allocator state. A bit in managed_bitmap means the page came
 * from a valid EFI_CONVENTIONAL_MEMORY descriptor; a bit in free_bitmap means
 * it is currently available for allocation. The page-zero frame is reserved.
 */
typedef struct AstraMemory {
    uint8_t managed_bitmap[ASTRA_MEMORY_BITMAP_BYTES];
    uint8_t free_bitmap[ASTRA_MEMORY_BITMAP_BYTES];
    AstraMemoryStats stats;
    bool initialized;
} AstraMemory;

/* Parses the UEFI memory-map byte span and initializes caller-owned storage. */
AstraMemoryResult astra_memory_init(AstraMemory *memory, const AstraBootInfo *boot_info);

/* Allocates or frees one 4 KiB physical page. Free accepts only managed pages. */
AstraMemoryResult astra_memory_alloc_page(AstraMemory *memory, uint64_t *address_out);
AstraMemoryResult astra_memory_free_page(AstraMemory *memory, uint64_t address);

/* Copies allocator counters to caller-owned storage. */
AstraMemoryResult astra_memory_get_stats(
    const AstraMemory *memory,
    AstraMemoryStats *stats_out
);

const char *astra_memory_result_name(AstraMemoryResult result);

#endif
