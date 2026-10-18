#include "acpi.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

enum {
    TEST_DESCRIPTOR_SIZE = 48u,
    TEST_REGION_SIZE = 4096u,
    TEST_RSDP_OFFSET = 0x80u,
    TEST_XSDT_OFFSET = 0x100u,
    TEST_SPCR_OFFSET = 0x200u,
    TEST_XSDT_SIZE = 44u,
    TEST_SPCR_SIZE = 80u
};

static _Alignas(TEST_REGION_SIZE) uint8_t g_acpi_region[TEST_REGION_SIZE];
static uint8_t g_memory_map[TEST_DESCRIPTOR_SIZE];
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

static void set_checksum(uint8_t *bytes, size_t length, size_t checksum_offset) {
    uint8_t sum = 0u;

    bytes[checksum_offset] = 0u;
    for (size_t i = 0u; i < length; i++) {
        sum = (uint8_t)(sum + bytes[i]);
    }
    bytes[checksum_offset] = (uint8_t)(0u - (uint32_t)sum);
}

static void make_fixture(AstraBootInfo *boot_info_out) {
    uint8_t *rsdp = &g_acpi_region[TEST_RSDP_OFFSET];
    uint8_t *xsdt = &g_acpi_region[TEST_XSDT_OFFSET];
    uint8_t *spcr = &g_acpi_region[TEST_SPCR_OFFSET];
    uint64_t region_base = (uint64_t)(uintptr_t)g_acpi_region;

    for (size_t i = 0u; i < sizeof g_acpi_region; i++) {
        g_acpi_region[i] = 0u;
    }
    for (size_t i = 0u; i < sizeof g_memory_map; i++) {
        g_memory_map[i] = 0u;
    }

    /* Valid ACPI 2.0+ RSDP, with checksum coverage for both structures. */
    rsdp[0] = (uint8_t)'R';
    rsdp[1] = (uint8_t)'S';
    rsdp[2] = (uint8_t)'D';
    rsdp[3] = (uint8_t)' ';
    rsdp[4] = (uint8_t)'P';
    rsdp[5] = (uint8_t)'T';
    rsdp[6] = (uint8_t)'R';
    rsdp[7] = 0u;
    rsdp[15] = 2u;
    write_u32_le(&rsdp[20], 36u);
    write_u64_le(&rsdp[24], region_base + (uint64_t)TEST_XSDT_OFFSET);
    set_checksum(rsdp, 20u, 8u);
    set_checksum(rsdp, 36u, 32u);

    /* XSDT containing exactly one SPCR pointer. */
    xsdt[0] = (uint8_t)'X';
    xsdt[1] = (uint8_t)'S';
    xsdt[2] = (uint8_t)'D';
    xsdt[3] = (uint8_t)'T';
    write_u32_le(&xsdt[4], TEST_XSDT_SIZE);
    xsdt[8] = 1u;
    write_u64_le(&xsdt[36], region_base + (uint64_t)TEST_SPCR_OFFSET);
    set_checksum(xsdt, TEST_XSDT_SIZE, 9u);

    /* ACPI SPCR describing a PL011 at QEMU virt's documented test address. */
    spcr[0] = (uint8_t)'S';
    spcr[1] = (uint8_t)'P';
    spcr[2] = (uint8_t)'C';
    spcr[3] = (uint8_t)'R';
    write_u32_le(&spcr[4], TEST_SPCR_SIZE);
    spcr[8] = 2u;
    spcr[36] = 3u;
    spcr[40] = 0u;
    spcr[41] = 32u;
    spcr[42] = 0u;
    spcr[43] = 3u;
    write_u64_le(&spcr[44], UINT64_C(0x09000000));
    set_checksum(spcr, TEST_SPCR_SIZE, 9u);

    /* A single ACPI-reclaim descriptor makes only this page table-readable. */
    write_u32_le(&g_memory_map[0], 9u);
    write_u64_le(&g_memory_map[8], region_base);
    write_u64_le(&g_memory_map[24], 1u);

    *boot_info_out = (AstraBootInfo){0};
    boot_info_out->magic = ASTRA_BOOT_INFO_MAGIC;
    boot_info_out->version = ASTRA_BOOT_INFO_VERSION;
    boot_info_out->memory_map = g_memory_map;
    boot_info_out->memory_map_size = sizeof g_memory_map;
    boot_info_out->memory_descriptor_size = TEST_DESCRIPTOR_SIZE;
    boot_info_out->acpi_rsdp_base = region_base + (uint64_t)TEST_RSDP_OFFSET;
}

static void test_valid_spcr(void) {
    AstraBootInfo boot_info = {0};
    AstraAcpiSpcr spcr = {.base_address = UINT64_C(0xdeadbeef)};

    make_fixture(&boot_info);
    CHECK(astra_acpi_find_pl011_uart(&boot_info, &spcr));
    CHECK(spcr.base_address == UINT64_C(0x09000000));
}

static void test_invalid_rsdp_checksum_is_rejected(void) {
    AstraBootInfo boot_info = {0};
    AstraAcpiSpcr spcr = {.base_address = UINT64_C(0xdeadbeef)};

    make_fixture(&boot_info);
    g_acpi_region[TEST_RSDP_OFFSET + 8u] ^= 1u;
    CHECK(!astra_acpi_find_pl011_uart(&boot_info, &spcr));
    CHECK(spcr.base_address == UINT64_C(0xdeadbeef));
}

static void test_invalid_spcr_checksum_is_rejected(void) {
    AstraBootInfo boot_info = {0};
    AstraAcpiSpcr spcr = {.base_address = UINT64_C(0xdeadbeef)};

    make_fixture(&boot_info);
    g_acpi_region[TEST_SPCR_OFFSET + 9u] ^= 1u;
    CHECK(!astra_acpi_find_pl011_uart(&boot_info, &spcr));
    CHECK(spcr.base_address == UINT64_C(0xdeadbeef));
}

static void test_unsupported_uart_and_outside_pointer_are_rejected(void) {
    AstraBootInfo boot_info = {0};
    AstraAcpiSpcr spcr = {.base_address = UINT64_C(0xdeadbeef)};
    uint64_t saved_address = 0u;
    uint8_t *table = &g_acpi_region[TEST_SPCR_OFFSET];

    make_fixture(&boot_info);
    table[36] = 0x0eu;
    set_checksum(table, TEST_SPCR_SIZE, 9u);
    CHECK(!astra_acpi_find_pl011_uart(&boot_info, &spcr));
    CHECK(spcr.base_address == UINT64_C(0xdeadbeef));

    make_fixture(&boot_info);
    saved_address = boot_info.acpi_rsdp_base;
    boot_info.acpi_rsdp_base = saved_address + (uint64_t)TEST_REGION_SIZE;
    CHECK(!astra_acpi_find_pl011_uart(&boot_info, &spcr));
    CHECK(spcr.base_address == UINT64_C(0xdeadbeef));
}

static void test_table_length_is_bounded(void) {
    AstraBootInfo boot_info = {0};
    AstraAcpiSpcr spcr = {.base_address = UINT64_C(0xdeadbeef)};

    make_fixture(&boot_info);
    write_u32_le(&g_acpi_region[TEST_XSDT_OFFSET + 4u], UINT32_MAX);
    set_checksum(&g_acpi_region[TEST_XSDT_OFFSET], TEST_XSDT_SIZE, 9u);
    CHECK(!astra_acpi_find_pl011_uart(&boot_info, &spcr));
    CHECK(spcr.base_address == UINT64_C(0xdeadbeef));
}

int main(void) {
    test_valid_spcr();
    test_invalid_rsdp_checksum_is_rejected();
    test_invalid_spcr_checksum_is_rejected();
    test_unsupported_uart_and_outside_pointer_are_rejected();
    test_table_length_is_bounded();

    if (g_failures != 0) {
        (void)fprintf(stderr, "%d AstraOS ACPI test(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    (void)puts("All AstraOS ACPI tests passed.");
    return EXIT_SUCCESS;
}
