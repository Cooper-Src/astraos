#include "uart.h"

#include "acpi.h"
#include "framebuffer.h"
#include "platform.h"

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

static volatile uint32_t *g_uart = NULL;

/*
 * Translate an MMIO virtual address without dereferencing it. The AT
 * instruction reports a translation/permission fault through PAR_ELx instead
 * of raising a Data Abort, letting us fall back to the framebuffer console
 * when firmware has not mapped the device into AstraOS's current address space.
 */
static bool uart_address_is_writable(uint64_t address) {
#if defined(__aarch64__)
    uint64_t current_el = 0u;
    uint64_t control = 0u;
    uint64_t par = 0u;

    if (address == 0u || address > (uint64_t)UINTPTR_MAX) {
        return false;
    }

    __asm__ volatile("mrs %0, CurrentEL" : "=r"(current_el));
    if (current_el == UINT64_C(0x4)) {
        __asm__ volatile("mrs %0, SCTLR_EL1" : "=r"(control));
        if ((control & UINT64_C(1)) == 0u) {
            return false;
        }
        __asm__ volatile(
            "at s1e1w, %1\n\t"
            "isb\n\t"
            "mrs %0, PAR_EL1"
            : "=r"(par)
            : "r"(address)
            : "memory"
        );
    } else if (current_el == UINT64_C(0x8)) {
        __asm__ volatile("mrs %0, SCTLR_EL2" : "=r"(control));
        if ((control & UINT64_C(1)) == 0u) {
            return false;
        }
        __asm__ volatile(
            "at s1e2w, %1\n\t"
            "isb\n\t"
            "mrs %0, PAR_EL2"
            : "=r"(par)
            : "r"(address)
            : "memory"
        );
    } else {
        return false;
    }
    return (par & UINT64_C(1)) == 0u;
#else
    (void)address;
    return false;
#endif
}

/* Check both ends so a register block crossing a page boundary is rejected
 * unless both pages are writable in the current translation regime. */
static bool uart_register_range_is_writable(uint64_t base) {
    const uint64_t register_span =
        ((uint64_t)UART_INTERRUPT_CLEAR + UINT64_C(1)) * sizeof(uint32_t);
    uint64_t last_register = 0u;

    if (base == 0u || base > UINT64_MAX - register_span) {
        return false;
    }
    last_register = base + register_span - sizeof(uint32_t);
    return uart_address_is_writable(base) &&
        uart_address_is_writable(last_register);
}

/* FDT-described QEMU/board hardware uses the configured AstraOS console rate. */
static bool uart_init_from_fdt(uint64_t uart_base) {
    volatile uint32_t *uart = (volatile uint32_t *)(uintptr_t)uart_base;

    if (!uart_register_range_is_writable(uart_base)) {
        return false;
    }
    uart[UART_CONTROL] = 0u;
    uart[UART_INTERRUPT_MASK] = 0u;
    uart[UART_INTERRUPT_CLEAR] = 0x7ffu;
    uart[UART_INTEGER_BAUD] = 13u;
    uart[UART_FRACTIONAL_BAUD] = 1u;
    uart[UART_LINE_CONTROL] = UART_LINE_8_BITS_FIFO;
    uart[UART_CONTROL] = UART_CONTROL_ENABLE;
    g_uart = uart;
    return true;
}

/* ACPI SPCR advertises the firmware console. Preserve its baud divisor instead
 * of assuming that every platform clocks a PL011 at QEMU's 24 MHz rate. */
static bool uart_init_from_acpi(const AstraBootInfo *boot_info) {
    AstraAcpiSpcr spcr = {0};
    volatile uint32_t *uart = NULL;
    uint32_t integer_baud = 0u;
    uint32_t fractional_baud = 0u;
    uint32_t line_control = 0u;

    if (!astra_acpi_find_pl011_uart(boot_info, &spcr) ||
        !uart_register_range_is_writable(spcr.base_address)) {
        return false;
    }

    uart = (volatile uint32_t *)(uintptr_t)spcr.base_address;
    integer_baud = uart[UART_INTEGER_BAUD];
    fractional_baud = uart[UART_FRACTIONAL_BAUD];
    line_control = uart[UART_LINE_CONTROL];
    if (integer_baud == 0u || integer_baud > 0xffffu ||
        fractional_baud > 0x3fu || line_control == 0u) {
        return false;
    }

    uart[UART_CONTROL] = 0u;
    uart[UART_INTERRUPT_MASK] = 0u;
    uart[UART_INTERRUPT_CLEAR] = 0x7ffu;
    uart[UART_INTEGER_BAUD] = integer_baud;
    uart[UART_FRACTIONAL_BAUD] = fractional_baud;
    uart[UART_LINE_CONTROL] = line_control;
    uart[UART_CONTROL] = UART_CONTROL_ENABLE;
    g_uart = uart;
    return true;
}

/* Failure leaves the framebuffer available and never probes an assumed address. */
bool astra_uart_init(const AstraBootInfo *boot_info) {
    uint64_t uart_base = 0u;

    g_uart = NULL;
    if (astra_platform_find_pl011_uart_from_fdt(boot_info, &uart_base)) {
        return uart_init_from_fdt(uart_base);
    }
    return uart_init_from_acpi(boot_info);
}

void astra_uart_put_byte(uint8_t value) {
    /* Draw first so a stalled serial device cannot hide the current character. */
    astra_framebuffer_put_byte(value);
    if (g_uart == NULL) {
        return;
    }
    while ((g_uart[UART_FLAGS] & UART_FLAG_TX_FULL) != 0u) {
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
    if (g_uart == NULL) {
        for (;;) {
            __asm__ volatile("wfe");
        }
    }
    while ((g_uart[UART_FLAGS] & UART_FLAG_RX_EMPTY) != 0u) {
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
