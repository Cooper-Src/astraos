#include "platform.h"

#include <stdio.h>
#include <stdlib.h>

enum { TEST_FDT_BYTE_COUNT = 428u };

static const char g_fixture_hex[] =
    "d00dfeed000001ac0000003800000168000000280000001100000010000000000000004400000130"
    "00000000000000000000000000000000000000010000000000000003000000040000000000000002"
    "00000003000000040000000f0000000200000003000000130000001b41737472614f532043492066"
    "6978747572650000000000030000001d0000002161737472616f732c63692d666978747572650071"
    "656d752c766972740000000000000001616c696173657300000000030000000f000000382f706c30"
    "313140393030303030300000000000020000000163686f73656e000000000003000000110000002c"
    "73657269616c303a3131353230306e38000000000000000200000001706c30313140393030303030"
    "3000000000000003000000180000002161726d2c706c3031310061726d2c7072696d6563656c6c00"
    "00000003000000100000004000000000090000000000000000001000000000020000000200000009"
    "23616464726573732d63656c6c73002373697a652d63656c6c73006d6f64656c00636f6d70617469"
    "626c65007374646f75742d706174680073657269616c300072656700";
static uint8_t g_fixture[TEST_FDT_BYTE_COUNT] = {0};
static int g_failures = 0;

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            (void)fprintf(stderr, "%s:%d: CHECK failed: %s\\n", \
                __FILE__, __LINE__, #expression); \
            g_failures++; \
        } \
    } while (0)

static int hex_nibble(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return (character - 'a') + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return (character - 'A') + 10;
    }
    return -1;
}

static bool decode_fixture(void) {
    if ((sizeof g_fixture_hex - 1u) != (sizeof g_fixture * 2u)) {
        return false;
    }
    for (size_t i = 0u; i < sizeof g_fixture; i++) {
        size_t offset = i * 2u;
        int high = hex_nibble(g_fixture_hex[offset]);
        int low = hex_nibble(g_fixture_hex[offset + 1u]);
        if (high < 0 || low < 0) {
            return false;
        }
        g_fixture[i] = (uint8_t)(((uint32_t)high << 4u) | (uint32_t)low);
    }
    return true;
}

static size_t find_bytes(
    const uint8_t *bytes,
    size_t length,
    const uint8_t *needle,
    size_t needle_length
) {
    if (bytes == NULL || needle == NULL || needle_length == 0u ||
        needle_length > length) {
        return SIZE_MAX;
    }
    for (size_t start = 0u; start <= length - needle_length; start++) {
        bool matches = true;
        for (size_t i = 0u; i < needle_length; i++) {
            if (bytes[start + i] != needle[i]) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return start;
        }
    }
    return SIZE_MAX;
}

static AstraBootInfo make_boot_info(void) {
    AstraBootInfo info = {0};
    info.magic = ASTRA_BOOT_INFO_MAGIC;
    info.version = ASTRA_BOOT_INFO_VERSION;
    info.device_tree_base = (uint64_t)(uintptr_t)g_fixture;
    info.device_tree_size = (uint64_t)sizeof g_fixture;
    return info;
}

static void test_resolves_stdout_alias_and_pl011_register_range(void) {
    AstraBootInfo info = make_boot_info();
    uint64_t base = UINT64_C(0xdeadbeef);

    CHECK(astra_platform_find_pl011_uart(&info, &base));
    CHECK(base == UINT64_C(0x09000000));
}

static void test_rejects_invalid_arguments_and_malformed_fdt(void) {
    AstraBootInfo info = make_boot_info();
    uint64_t base = UINT64_C(0xdeadbeef);
    uint8_t saved_magic = g_fixture[0];

    CHECK(!astra_platform_find_pl011_uart(NULL, &base));
    CHECK(base == UINT64_C(0xdeadbeef));

    info.device_tree_size = 0u;
    CHECK(!astra_platform_find_pl011_uart(&info, &base));
    CHECK(base == UINT64_C(0xdeadbeef));

    info = make_boot_info();
    info.device_tree_base = UINT64_MAX;
    CHECK(!astra_platform_find_pl011_uart(&info, &base));
    CHECK(base == UINT64_C(0xdeadbeef));

    info = make_boot_info();
    g_fixture[0] = 0u;
    CHECK(!astra_platform_find_pl011_uart(&info, &base));
    CHECK(base == UINT64_C(0xdeadbeef));
    g_fixture[0] = saved_magic;
}

static void test_rejects_non_pl011_serial_and_preserves_output(void) {
    static const uint8_t compatible[] = "arm,pl011";
    AstraBootInfo info = make_boot_info();
    uint64_t base = UINT64_C(0xdeadbeef);
    size_t offset = find_bytes(
        g_fixture, sizeof g_fixture, compatible, sizeof compatible - 1u
    );
    uint8_t saved_character = 0u;

    CHECK(offset != SIZE_MAX);
    if (offset == SIZE_MAX) {
        return;
    }
    saved_character = g_fixture[offset];
    g_fixture[offset] = (uint8_t)'x';
    CHECK(!astra_platform_find_pl011_uart(&info, &base));
    CHECK(base == UINT64_C(0xdeadbeef));
    g_fixture[offset] = saved_character;
}

int main(void) {
    if (!decode_fixture()) {
        (void)fprintf(stderr, "invalid embedded FDT fixture\\n");
        return EXIT_FAILURE;
    }
    test_resolves_stdout_alias_and_pl011_register_range();
    test_rejects_invalid_arguments_and_malformed_fdt();
    test_rejects_non_pl011_serial_and_preserves_output();

    if (g_failures != 0) {
        (void)fprintf(stderr, "%d AstraOS platform test(s) failed\\n", g_failures);
        return EXIT_FAILURE;
    }
    (void)puts("All AstraOS platform tests passed.");
    return EXIT_SUCCESS;
}
