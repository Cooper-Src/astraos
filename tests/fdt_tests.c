#include "fdt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    TEST_FDT_TOTAL_SIZE = 102u,
    TEST_FDT_STRUCTURE_OFFSET = 56u,
    TEST_FDT_STRUCTURE_SIZE = 40u,
    TEST_FDT_STRINGS_OFFSET = 96u,
    TEST_FDT_STRINGS_SIZE = 6u,
    TEST_FDT_RESERVE_OFFSET = 40u
};

static int g_failures = 0;

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            (void)fprintf(stderr, "%s:%d: CHECK failed: %s\n", \
                __FILE__, __LINE__, #expression); \
            g_failures++; \
        } \
    } while (0)

static void write_be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)((value >> 24) & UINT32_C(0xff));
    bytes[1] = (uint8_t)((value >> 16) & UINT32_C(0xff));
    bytes[2] = (uint8_t)((value >> 8) & UINT32_C(0xff));
    bytes[3] = (uint8_t)(value & UINT32_C(0xff));
}

static size_t make_valid_blob(uint8_t *blob, size_t capacity) {
    static const uint8_t model[] = "Astra Test";
    static const uint8_t property_name[] = "model";
    size_t total = (size_t)TEST_FDT_TOTAL_SIZE;

    if (blob == NULL || capacity < total) {
        return 0u;
    }
    (void)memset(blob, 0, capacity);
    write_be32(&blob[0], ASTRA_FDT_MAGIC);
    write_be32(&blob[4], (uint32_t)TEST_FDT_TOTAL_SIZE);
    write_be32(&blob[8], (uint32_t)TEST_FDT_STRUCTURE_OFFSET);
    write_be32(&blob[12], (uint32_t)TEST_FDT_STRINGS_OFFSET);
    write_be32(&blob[16], (uint32_t)TEST_FDT_RESERVE_OFFSET);
    write_be32(&blob[20], 17u);
    write_be32(&blob[24], 16u);
    write_be32(&blob[28], 0u);
    write_be32(&blob[32], (uint32_t)TEST_FDT_STRINGS_SIZE);
    write_be32(&blob[36], (uint32_t)TEST_FDT_STRUCTURE_SIZE);

    write_be32(&blob[56], 1u);
    blob[60] = 0u;
    write_be32(&blob[64], 3u);
    write_be32(&blob[68], (uint32_t)sizeof model);
    write_be32(&blob[72], 0u);
    (void)memcpy(&blob[76], model, sizeof model);
    write_be32(&blob[88], 2u);
    write_be32(&blob[92], 9u);
    (void)memcpy(&blob[96], property_name, sizeof property_name);
    return total;
}

static void test_valid_header_and_root_property(void) {
    uint8_t blob[256] = {0};
    AstraFdtView view = {0};
    const uint8_t *value = NULL;
    size_t value_length = 0;
    size_t blob_size = make_valid_blob(blob, sizeof blob);

    CHECK(blob_size == (size_t)TEST_FDT_TOTAL_SIZE);
    CHECK(astra_fdt_open(blob, blob_size, &view) == ASTRA_FDT_OK);
    CHECK(view.total_size == blob_size);
    CHECK(view.structure_offset == (size_t)TEST_FDT_STRUCTURE_OFFSET);
    CHECK(view.structure_size == (size_t)TEST_FDT_STRUCTURE_SIZE);
    CHECK(view.strings_offset == (size_t)TEST_FDT_STRINGS_OFFSET);
    CHECK(view.reserve_map_size == 16u);
    CHECK(astra_fdt_root_property(&view, "model", 5u, &value, &value_length) == ASTRA_FDT_OK);
    CHECK(value != NULL);
    CHECK(value_length == sizeof "Astra Test");
    CHECK(memcmp(value, "Astra Test", sizeof "Astra Test") == 0);
    CHECK(astra_fdt_root_property(&view, "compatible", 10u, &value, &value_length) ==
        ASTRA_FDT_PROPERTY_NOT_FOUND);
    CHECK(value == NULL);
    CHECK(value_length == 0u);
}

static void test_short_and_invalid_headers(void) {
    uint8_t blob[256] = {0};
    AstraFdtView view = {0};
    size_t blob_size = make_valid_blob(blob, sizeof blob);

    CHECK(astra_fdt_open(blob, (size_t)ASTRA_FDT_HEADER_SIZE - 1u, &view) ==
        ASTRA_FDT_TRUNCATED);

    blob[0] = 0u;
    CHECK(astra_fdt_open(blob, blob_size, &view) == ASTRA_FDT_BAD_MAGIC);

    blob_size = make_valid_blob(blob, sizeof blob);
    CHECK(astra_fdt_open(blob, blob_size - 1u, &view) == ASTRA_FDT_TRUNCATED);

    write_be32(&blob[4], (uint32_t)ASTRA_FDT_MAX_SIZE + 1u);
    CHECK(astra_fdt_open(blob, sizeof blob, &view) == ASTRA_FDT_TOO_LARGE);

    blob_size = make_valid_blob(blob, sizeof blob);
    write_be32(&blob[20], 16u);
    CHECK(astra_fdt_open(blob, blob_size, &view) == ASTRA_FDT_UNSUPPORTED_VERSION);
}

static void test_block_ranges_and_reserve_map(void) {
    uint8_t blob[256] = {0};
    AstraFdtView view = {0};
    size_t blob_size = make_valid_blob(blob, sizeof blob);

    write_be32(&blob[8], UINT32_MAX);
    CHECK(astra_fdt_open(blob, blob_size, &view) == ASTRA_FDT_INVALID_BLOCK);

    blob_size = make_valid_blob(blob, sizeof blob);
    write_be32(&blob[12], (uint32_t)TEST_FDT_STRUCTURE_OFFSET + 4u);
    CHECK(astra_fdt_open(blob, blob_size, &view) == ASTRA_FDT_INVALID_BLOCK);

    blob_size = make_valid_blob(blob, sizeof blob);
    write_be32(&blob[16], 41u);
    CHECK(astra_fdt_open(blob, blob_size, &view) == ASTRA_FDT_INVALID_BLOCK);

    blob_size = make_valid_blob(blob, sizeof blob);
    write_be32(&blob[40], 1u);
    write_be32(&blob[44], 1u);
    CHECK(astra_fdt_open(blob, blob_size, &view) == ASTRA_FDT_INVALID_RESERVE_MAP);
}

static void test_invalid_structure_and_arguments(void) {
    uint8_t blob[256] = {0};
    AstraFdtView view = {0};
    const uint8_t *value = NULL;
    size_t value_length = 0;
    size_t blob_size = make_valid_blob(blob, sizeof blob);

    CHECK(astra_fdt_open(NULL, blob_size, &view) == ASTRA_FDT_INVALID_ARGUMENT);
    CHECK(astra_fdt_open(blob, blob_size, NULL) == ASTRA_FDT_INVALID_ARGUMENT);
    CHECK(astra_fdt_open(blob, blob_size, &view) == ASTRA_FDT_OK);

    write_be32(&blob[64], UINT32_C(0xfffffff0));
    CHECK(astra_fdt_root_property(&view, "model", 5u, &value, &value_length) ==
        ASTRA_FDT_INVALID_STRUCTURE);

    CHECK(astra_fdt_root_property(&view, NULL, 5u, &value, &value_length) ==
        ASTRA_FDT_INVALID_ARGUMENT);
    CHECK(astra_fdt_root_property(&view, "model", 5u, NULL, &value_length) ==
        ASTRA_FDT_INVALID_ARGUMENT);
}

int main(void) {
    test_valid_header_and_root_property();
    test_short_and_invalid_headers();
    test_block_ranges_and_reserve_map();
    test_invalid_structure_and_arguments();

    if (g_failures != 0) {
        (void)fprintf(stderr, "%d AstraOS FDT test(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    (void)puts("All AstraOS FDT tests passed.");
    return EXIT_SUCCESS;
}
