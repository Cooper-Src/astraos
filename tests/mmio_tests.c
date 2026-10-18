#include "mmio.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

enum {
    TEST_DESCRIPTOR_SIZE = 48u,
    TEST_DESCRIPTOR_COUNT = 3u,
    TEST_MAP_SIZE = TEST_DESCRIPTOR_SIZE * TEST_DESCRIPTOR_COUNT
};

static uint8_t g_map[TEST_MAP_SIZE] = {0};
static int g_failures = 0;

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            (void)fprintf(stderr, "%s:%d: CHECK failed: %s\n", \
                __FILE__, __LINE__, #expression); \
            g_failures++; \
        } \
    } while (0)

static void write_u32_le(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value & UINT32_C(0xff));
    bytes[1] = (uint8_t)((value >> 8u) & UINT32_C(0xff));
    bytes[2] = (uint8_t)((value >> 16u) & UINT32_C(0xff));
    bytes[3] = (uint8_t)((value >> 24u) & UINT32_C(0xff));
}

static void write_u64_le(uint8_t *bytes, uint64_t value) {
    for (size_t i = 0u; i < 8u; i++) {
        bytes[i] = (uint8_t)(value & UINT64_C(0xff));
        value >>= 8u;
    }
}

static void set_descriptor(
    size_t index,
    uint32_t type,
    uint64_t base,
    uint64_t page_count
) {
    uint8_t *descriptor = &g_map[index * TEST_DESCRIPTOR_SIZE];

    for (size_t i = 0u; i < TEST_DESCRIPTOR_SIZE; i++) {
        descriptor[i] = 0u;
    }
    write_u32_le(&descriptor[0], type);
    write_u64_le(&descriptor[8], base);
    write_u64_le(&descriptor[24], page_count);
}

static AstraBootInfo make_boot_info(void) {
    AstraBootInfo info = {0};

    for (size_t i = 0u; i < sizeof g_map; i++) {
        g_map[i] = 0u;
    }
    set_descriptor(0u, 7u, UINT64_C(0x40000000), 4u);
    set_descriptor(1u, 11u, UINT64_C(0x09000000), 1u);
    set_descriptor(2u, 11u, UINT64_C(0x10000000), 2u);

    info.magic = ASTRA_BOOT_INFO_MAGIC;
    info.version = ASTRA_BOOT_INFO_VERSION;
    info.memory_map = g_map;
    info.memory_map_size = sizeof g_map;
    info.memory_descriptor_size = TEST_DESCRIPTOR_SIZE;
    return info;
}

static void test_valid_and_boundary_ranges(void) {
    AstraBootInfo info = make_boot_info();

    CHECK(astra_mmio_range_is_valid(&info, UINT64_C(0x09000000), 72u));
    CHECK(astra_mmio_range_is_valid(&info, UINT64_C(0x09000ffc), 4u));
    CHECK(astra_mmio_range_is_valid(&info, UINT64_C(0x10001000), 4096u));

    /* Some firmware marks platform-described device windows as reserved. */
    set_descriptor(1u, 0u, UINT64_C(0x09000000), 1u);
    CHECK(astra_mmio_range_is_valid(&info, UINT64_C(0x09000000), 72u));
    CHECK(!astra_mmio_range_is_valid(&info, UINT64_C(0x09001000), 4u));
    CHECK(!astra_mmio_range_is_valid(&info, UINT64_C(0x09000fc0), 72u));
}

static void test_unlisted_device_window_is_allowed_without_conflicts(void) {
    AstraBootInfo info = make_boot_info();

    /* The device tree names this UART, but the UEFI map omits its address. */
    set_descriptor(1u, 7u, UINT64_C(0x20000000), 1u);
    CHECK(astra_mmio_range_is_valid(&info, UINT64_C(0x09000000), 72u));
}

static void test_ram_and_malformed_ranges_are_rejected(void) {
    AstraBootInfo info = make_boot_info();

    CHECK(!astra_mmio_range_is_valid(&info, UINT64_C(0x40000000), 4u));
    CHECK(!astra_mmio_range_is_valid(&info, UINT64_C(0x09000000), 0u));
    CHECK(!astra_mmio_range_is_valid(&info, UINT64_MAX - 3u, 8u));
    CHECK(!astra_mmio_range_is_valid(&info, 0u, 4u));

    info.memory_map_size--;
    CHECK(!astra_mmio_range_is_valid(&info, UINT64_C(0x09000000), 4u));
    info.memory_map_size = sizeof g_map;
    info.memory_descriptor_size = 32u;
    CHECK(!astra_mmio_range_is_valid(&info, UINT64_C(0x09000000), 4u));
    info.memory_descriptor_size = TEST_DESCRIPTOR_SIZE;
    info.magic = 0u;
    CHECK(!astra_mmio_range_is_valid(&info, UINT64_C(0x09000000), 4u));
}

static void test_overlapping_non_mmio_descriptor_is_rejected(void) {
    AstraBootInfo info = make_boot_info();

    set_descriptor(2u, 7u, UINT64_C(0x09000000), 1u);
    CHECK(!astra_mmio_range_is_valid(&info, UINT64_C(0x09000000), 72u));
}

int main(void) {
    test_valid_and_boundary_ranges();
    test_unlisted_device_window_is_allowed_without_conflicts();
    test_ram_and_malformed_ranges_are_rejected();
    test_overlapping_non_mmio_descriptor_is_rejected();

    if (g_failures != 0) {
        (void)fprintf(stderr, "%d AstraOS MMIO test(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    (void)puts("All AstraOS MMIO tests passed.");
    return EXIT_SUCCESS;
}
