#include "uart.h"

enum {
    UART_DATA = 0,
    UART_FLAGS = 6,
    UART_INTEGER_BAUD = 9,
    UART_FRACTIONAL_BAUD = 10,
    UART_LINE_CONTROL = 11,
    UART_CONTROL = 12,
    UART_INTERRUPT_MASK = 14,
    UART_INTERRUPT_CLEAR = 17,
    UART_FLAG_RX_EMPTY = 1u << 4,
    UART_FLAG_TX_FULL = 1u << 5,
    UART_LINE_8_BITS_FIFO = (3u << 5) | (1u << 4),
    UART_CONTROL_ENABLE = (1u << 0) | (1u << 8) | (1u << 9)
};

static volatile uint32_t *const g_uart =
    (volatile uint32_t *)(uintptr_t)UINT32_C(0x09000000);

void astra_uart_init(void) {
    g_uart[UART_CONTROL] = 0;
    g_uart[UART_INTERRUPT_MASK] = 0;
    g_uart[UART_INTERRUPT_CLEAR] = 0x7ffu;
    g_uart[UART_INTEGER_BAUD] = 13u;
    g_uart[UART_FRACTIONAL_BAUD] = 1u;
    g_uart[UART_LINE_CONTROL] = UART_LINE_8_BITS_FIFO;
    g_uart[UART_CONTROL] = UART_CONTROL_ENABLE;
}

void astra_uart_put_byte(uint8_t value) {
    while ((g_uart[UART_FLAGS] & UART_FLAG_TX_FULL) != 0) {
    }
    g_uart[UART_DATA] = (uint32_t)value;
}

void astra_uart_write(const char *data, size_t length) {
    if (data == NULL && length != 0) {
        return;
    }
    for (size_t i = 0; i < length; i++) {
        if (data[i] == '\n') {
            astra_uart_put_byte((uint8_t)'\r');
        }
        astra_uart_put_byte((uint8_t)data[i]);
    }
}

void astra_uart_puts(const char *text) {
    size_t length = 0;

    if (text == NULL) {
        return;
    }
    while (text[length] != '\0') {
        length++;
    }
    astra_uart_write(text, length);
}

uint8_t astra_uart_get_byte(void) {
    while ((g_uart[UART_FLAGS] & UART_FLAG_RX_EMPTY) != 0) {
    }
    return (uint8_t)(g_uart[UART_DATA] & 0xffu);
}

void astra_uart_put_u64(uint64_t value) {
    char digits[20];
    size_t count = 0;

    if (value == 0) {
        astra_uart_put_byte((uint8_t)'0');
        return;
    }
    while (value > 0 && count < sizeof digits) {
        digits[count] = (char)('0' + (value % UINT64_C(10)));
        count++;
        value /= UINT64_C(10);
    }
    while (count > 0) {
        count--;
        astra_uart_put_byte((uint8_t)digits[count]);
    }
}

void astra_uart_put_u32(uint32_t value) {
    astra_uart_put_u64((uint64_t)value);
}

void astra_uart_put_hex(uint64_t value) {
    static const char hex_digits[] = "0123456789abcdef";

    astra_uart_puts("0x");
    for (size_t i = 0; i < 16u; i++) {
        size_t shift = 60u - (i * 4u);
        size_t digit = (size_t)((value >> shift) & UINT64_C(0xf));
        astra_uart_put_byte((uint8_t)hex_digits[digit]);
    }
}
