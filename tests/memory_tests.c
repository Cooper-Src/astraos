#include "memory.h"

#include <stdio.h>
#include <stdint.h>

enum {
    TEST_DESCRIPTOR_BYTES = 48u,
    TEST_DESCRIPTOR_COUNT = 3u,
    TEST_MAP_BYTES = TEST_DESCRIPTOR_BYTES * TEST_DESCRIPTOR_COUNT
};

static uint8_t g_test_map[TEST_MAP_BYTES];
static AstraMemory g_memory;
static int g_failures = 0;

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            (void)fprintf(stderr, "%s:%d: CHECK failed: %s\n", \
                __FILE__, __LINE__, #expression); \
            g_failures++; \
        } \
    } while (0)

static void zero_test_map(void) {
    for (size_t i = 0; i < sizeof g_test_map; i++) {
        g_test_map[i] = 0;
    }
}

static void write_u32_le(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value & UINT32_C(0xff));
    bytes[1] = (uint8_t)((value >> 8) & UINT32_C(0xff));
    bytes[2] = (uint8_t)((value >> 16) & UINT32_C(0xff));
    bytes[3] = (uint8_t)((value >> 24) & UINT32_C(0xff));
}

static void write_u64_le(uint8_t *bytes, uint64_t value) {
    for (size_t i = 0; i < 8u; i++) {
        bytes[i] = (uint8_t)(value & UINT64_C(0xff));
        value >>= 8;
    }
}

static void set_descriptor(
    size_t index,
    size_t descriptor_size,
    uint32_t type,
    uint64_t physical_start,
    uint64_t page_count
) {
    uint8_t *descriptor = &g_test_map[index * descriptor_size];

    for (size_t i = 0; i < descriptor_size; i++) {
        descriptor[i] = 0;
    }
    write_u32_le(&descriptor[0], type);
    write_u64_le(&descriptor[8], physical_start);
    write_u64_le(&descriptor[24], page_count);
}

static AstraBootInfo make_boot_info(size_t map_size, size_t descriptor_size) {
    AstraBootInfo info = {0};

    info.magic = ASTRA_BOOT_INFO_MAGIC;
    info.version = ASTRA_BOOT_INFO_VERSION;
    info.memory_map = g_test_map;
    info.memory_map_size = (uint64_t)map_size;
    info.memory_descriptor_size = (uint64_t)descriptor_size;
    info.memory_descriptor_version = 1;
    info.kernel_base = UINT64_C(0x40200000);
    info.kernel_image_size = UINT64_C(0x10000);
    info.kernel_allocation_size = UINT64_C(0x200000);
    return info;
}

static void test_allocate_free_and_stats(void) {
    AstraBootInfo info = {0};
    AstraMemoryStats stats = {0};
    uint64_t addresses[8] = {0};
    uint64_t address = UINT64_C(0xdeadbeef);

    zero_test_map();
    set_descriptor(0u, TEST_DESCRIPTOR_BYTES, 2u, 0u, 1024u);
    set_descriptor(1u, TEST_DESCRIPTOR_BYTES, 7u, UINT64_C(0x40000000), 8u);
    set_descriptor(2u, TEST_DESCRIPTOR_BYTES, 7u, UINT64_C(0x100000000), 2u);
    info = make_boot_info(TEST_MAP_BYTES, TEST_DESCRIPTOR_BYTES);

    CHECK(astra_memory_init(&g_memory, &info) == ASTRA_MEMORY_OK);
    CHECK(astra_memory_get_stats(&g_memory, &stats) == ASTRA_MEMORY_OK);
    CHECK(stats.descriptor_count == 3u);
    CHECK(stats.managed_pages == 8u);
    CHECK(stats.free_pages == 8u);
    CHECK(stats.ignored_pages == 2u);

    for (size_t i = 0; i < 8u; i++) {
        CHECK(astra_memory_alloc_page(&g_memory, &addresses[i]) == ASTRA_MEMORY_OK);
        CHECK(addresses[i] == UINT64_C(0x40000000) +
            ((uint64_t)i * ASTRA_MEMORY_PAGE_SIZE));
    }
    CHECK(astra_memory_alloc_page(&g_memory, &address) == ASTRA_MEMORY_OUT_OF_MEMORY);
    CHECK(address == UINT64_C(0xdeadbeef));

    CHECK(astra_memory_free_page(&g_memory, addresses[0]) == ASTRA_MEMORY_OK);
    CHECK(astra_memory_free_page(&g_memory, addresses[0]) == ASTRA_MEMORY_DOUBLE_FREE);
    CHECK(astra_memory_free_page(&g_memory, addresses[1] + 1u) ==
        ASTRA_MEMORY_INVALID_ARGUMENT);
    CHECK(astra_memory_free_page(&g_memory, UINT64_C(0x50000000)) ==
        ASTRA_MEMORY_NOT_MANAGED);
    CHECK(astra_memory_free_page(&g_memory, UINT64_C(0x100000000)) ==
        ASTRA_MEMORY_ADDRESS_OUT_OF_RANGE);
    CHECK(astra_memory_free_page(&g_memory, 0u) == ASTRA_MEMORY_INVALID_ARGUMENT);

    for (size_t i = 1u; i < 8u; i++) {
        CHECK(astra_memory_free_page(&g_memory, addresses[i]) == ASTRA_MEMORY_OK);
    }
    CHECK(astra_memory_get_stats(&g_memory, &stats) == ASTRA_MEMORY_OK);
    CHECK(stats.free_pages == stats.managed_pages);
}

static void test_high_memory_is_reported_but_not_allocated(void) {
    AstraBootInfo info = {0};
    AstraMemoryStats stats = {0};
    uint64_t address = 123u;

    zero_test_map();
    set_descriptor(0u, TEST_DESCRIPTOR_BYTES, 7u, UINT64_C(0x100000000), 12u);
    info = make_boot_info(TEST_DESCRIPTOR_BYTES, TEST_DESCRIPTOR_BYTES);

    CHECK(astra_memory_init(&g_memory, &info) == ASTRA_MEMORY_OK);
    CHECK(astra_memory_get_stats(&g_memory, &stats) == ASTRA_MEMORY_OK);
    CHECK(stats.managed_pages == 0u);
    CHECK(stats.free_pages == 0u);
    CHECK(stats.ignored_pages == 12u);
    CHECK(astra_memory_alloc_page(&g_memory, &address) == ASTRA_MEMORY_OUT_OF_MEMORY);
    CHECK(address == 123u);
}

static void test_invalid_maps_are_rejected(void) {
    AstraBootInfo info = {0};

    zero_test_map();
    set_descriptor(0u, 40u, 7u, UINT64_C(0x40000000), 2u);
    set_descriptor(1u, 40u, 7u, UINT64_C(0x40001000), 2u);
    info = make_boot_info(80u, 40u);
    CHECK(astra_memory_init(&g_memory, &info) == ASTRA_MEMORY_INVALID_MAP);
    CHECK(astra_memory_get_stats(&g_memory, &(AstraMemoryStats){0}) ==
        ASTRA_MEMORY_NOT_INITIALIZED);

    info = make_boot_info(80u, 32u);
    CHECK(astra_memory_init(&g_memory, &info) == ASTRA_MEMORY_INVALID_MAP);

    info = make_boot_info(41u, 40u);
    CHECK(astra_memory_init(&g_memory, &info) == ASTRA_MEMORY_INVALID_MAP);

    info = make_boot_info(TEST_MAP_BYTES, TEST_DESCRIPTOR_BYTES * 2u);
    CHECK(astra_memory_init(&g_memory, &info) == ASTRA_MEMORY_INVALID_MAP);

    info = make_boot_info(TEST_MAP_BYTES, TEST_DESCRIPTOR_BYTES);
    info.memory_map_size = ASTRA_BOOT_MEMORY_MAP_CAP_BYTES + UINT64_C(1);
    CHECK(astra_memory_init(&g_memory, &info) == ASTRA_MEMORY_INVALID_MAP);

    info = make_boot_info(TEST_MAP_BYTES, TEST_DESCRIPTOR_BYTES);
    info.magic = 0;
    CHECK(astra_memory_init(&g_memory, &info) == ASTRA_MEMORY_INVALID_BOOT_INFO);
}

static void test_overlaps_between_different_descriptor_types_are_rejected(void) {
    AstraBootInfo info = {0};

    zero_test_map();
    set_descriptor(
        0u, TEST_DESCRIPTOR_BYTES, 7u, UINT64_C(0x40000000), 4u
    );
    set_descriptor(
        1u, TEST_DESCRIPTOR_BYTES, 0u, UINT64_C(0x40002000), 1u
    );
    info = make_boot_info(2u * TEST_DESCRIPTOR_BYTES, TEST_DESCRIPTOR_BYTES);
    CHECK(astra_memory_init(&g_memory, &info) == ASTRA_MEMORY_INVALID_MAP);
    CHECK(!g_memory.initialized);

    zero_test_map();
    set_descriptor(
        0u, TEST_DESCRIPTOR_BYTES, 7u, UINT64_C(0x100000000), 4u
    );
    set_descriptor(
        1u, TEST_DESCRIPTOR_BYTES, 0u, UINT64_C(0x100002000), 1u
    );
    info = make_boot_info(2u * TEST_DESCRIPTOR_BYTES, TEST_DESCRIPTOR_BYTES);
    CHECK(astra_memory_init(&g_memory, &info) == ASTRA_MEMORY_INVALID_MAP);
    CHECK(!g_memory.initialized);
}

static void test_invalid_arguments_and_descriptor_overflow(void) {
    AstraBootInfo info = {0};

    CHECK(astra_memory_init(NULL, NULL) == ASTRA_MEMORY_INVALID_ARGUMENT);
    CHECK(astra_memory_init(&g_memory, NULL) == ASTRA_MEMORY_INVALID_ARGUMENT);
    CHECK(astra_memory_alloc_page(&g_memory, NULL) == ASTRA_MEMORY_INVALID_ARGUMENT);
    CHECK(astra_memory_get_stats(&g_memory, NULL) == ASTRA_MEMORY_INVALID_ARGUMENT);

    zero_test_map();
    set_descriptor(0u, TEST_DESCRIPTOR_BYTES, 7u, UINT64_C(0xfffffffffffff000), 2u);
    info = make_boot_info(TEST_DESCRIPTOR_BYTES, TEST_DESCRIPTOR_BYTES);
    CHECK(astra_memory_init(&g_memory, &info) == ASTRA_MEMORY_INVALID_MAP);
}

int main(void) {
    test_allocate_free_and_stats();
    test_high_memory_is_reported_but_not_allocated();
    test_invalid_maps_are_rejected();
    test_overlaps_between_different_descriptor_types_are_rejected();
    test_invalid_arguments_and_descriptor_overflow();

    if (g_failures != 0) {
        (void)fprintf(stderr, "%d AstraOS memory test(s) failed\n", g_failures);
        return 1;
    }
    (void)puts("All AstraOS memory tests passed.");
    return 0;
}
