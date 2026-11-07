#include "input.h"
#include "uart.h"

#include <stdio.h>
#include <stdlib.h>

static int g_failures = 0;
static uint8_t g_serial_byte = (uint8_t)'q';

uint8_t astra_uart_get_byte(void) {
    return g_serial_byte;
}

/* The host unit test does not provide a real serial console. */
void astra_uart_puts(const char *text) {
    (void)text;
}

void astra_uart_put_hex(uint64_t value) {
    (void)value;
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

static void test_serial_input_fallback(void) {
    uint8_t byte = 0u;
    AstraInputEvent event = {0};

    CHECK(astra_input_initialize(NULL, true));
    CHECK(!astra_input_pointer_available());
    CHECK(astra_input_read_byte(&byte));
    CHECK(byte == g_serial_byte);
    CHECK(astra_input_read_event(&event));
    CHECK(event.type == ASTRA_INPUT_EVENT_KEY);
    CHECK(event.key == g_serial_byte);
    CHECK(!event.absolute_position);

    CHECK(!astra_input_initialize(NULL, false));
    byte = UINT8_C(0xaa);
    CHECK(!astra_input_read_byte(&byte));
    CHECK(byte == UINT8_C(0xaa));
}

int main(void) {
    test_ascii_key_translation();
    test_serial_input_fallback();

    if (g_failures != 0) {
        (void)fprintf(stderr, "%d AstraOS input test(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }

    (void)puts("All AstraOS input tests passed.");
    return EXIT_SUCCESS;
}
