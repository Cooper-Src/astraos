#include "input.h"
#include "uart.h"

#include <stdio.h>
#include <stdlib.h>

static int g_failures = 0;
static uint8_t g_serial_byte = (uint8_t)'q';

uint8_t astra_uart_get_byte(void) {
    return g_serial_byte;
}

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            (void)fprintf(stderr, "%s:%d: CHECK failed: %s\n", \
                __FILE__, __LINE__, #condition); \
            g_failures++; \
        } \
    } while (0)

static void test_ascii_key_translation(void) {
    uint8_t byte = 0u;

    CHECK(astra_input_translate_key((uint16_t)'a', &byte));
    CHECK(byte == (uint8_t)'a');

    CHECK(astra_input_translate_key((uint16_t)'Z', &byte));
    CHECK(byte == (uint8_t)'Z');

    CHECK(astra_input_translate_key((uint16_t)'\r', &byte));
    CHECK(byte == (uint8_t)'\r');

    CHECK(astra_input_translate_key(UINT16_C(0x08), &byte));
    CHECK(byte == UINT8_C(0x08));

    byte = UINT8_C(0xaa);
    CHECK(!astra_input_translate_key(UINT16_C(0), &byte));
    CHECK(byte == UINT8_C(0xaa));
    CHECK(!astra_input_translate_key(UINT16_C(0x20ac), &byte));
    CHECK(byte == UINT8_C(0xaa));
    CHECK(!astra_input_translate_key((uint16_t)'x', NULL));
}

static void test_firmware_function_key_translation(void) {
    AstraFirmwareInputKey key = {0};
    uint8_t byte = 0u;

    key.scan_code = UINT16_C(0x000b);
    CHECK(astra_input_translate_firmware_key(&key, &byte));
    CHECK(byte == (uint8_t)ASTRA_INPUT_KEY_F1);

    key.scan_code = UINT16_C(0x000c);
    CHECK(astra_input_translate_firmware_key(&key, &byte));
    CHECK(byte == (uint8_t)ASTRA_INPUT_KEY_F2);

    key.scan_code = UINT16_C(0x000d);
    CHECK(astra_input_translate_firmware_key(&key, &byte));
    CHECK(byte == (uint8_t)ASTRA_INPUT_KEY_F3);

    key.scan_code = UINT16_C(0x000d);
    key.unicode_char = (uint16_t)'x';
    CHECK(astra_input_translate_firmware_key(&key, &byte));
    CHECK(byte == (uint8_t)'x');

    byte = UINT8_C(0xaa);
    key.scan_code = UINT16_C(0xffff);
    key.unicode_char = 0u;
    CHECK(!astra_input_translate_firmware_key(&key, &byte));
    CHECK(byte == UINT8_C(0xaa));
    CHECK(!astra_input_translate_firmware_key(NULL, &byte));
    CHECK(!astra_input_translate_firmware_key(&key, NULL));
}

static void test_serial_input_fallback(void) {
    uint8_t byte = 0u;

    CHECK(astra_input_initialize(NULL, true));
    CHECK(astra_input_read_byte(&byte));
    CHECK(byte == g_serial_byte);

    CHECK(!astra_input_initialize(NULL, false));
    byte = UINT8_C(0xaa);
    CHECK(!astra_input_read_byte(&byte));
    CHECK(byte == UINT8_C(0xaa));
}

int main(void) {
    test_ascii_key_translation();
    test_firmware_function_key_translation();
    test_serial_input_fallback();

    if (g_failures != 0) {
        (void)fprintf(stderr, "%d AstraOS input test(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }

    (void)puts("All AstraOS input tests passed.");
    return EXIT_SUCCESS;
}
