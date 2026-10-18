#include "exceptions.h"

#include "uart.h"

#include <stdint.h>

enum {
    ESR_EXCEPTION_CLASS_SHIFT = 26u
};

/*
 * Prints exception state through the initialized serial/framebuffer console.
 * This function never returns because the current exception has no recovery path.
 */
_Noreturn void astra_exception_report(
    uint64_t vector_slot,
    uint64_t esr,
    uint64_t elr,
    uint64_t far,
    uint64_t stack_pointer,
    uint64_t current_el
) {
    const uint64_t exception_class =
        (esr >> ESR_EXCEPTION_CLASS_SHIFT) & UINT64_C(0x3f);

    astra_uart_puts("\nAstraOS fatal exception\n");
    astra_uart_puts("Vector slot: ");
    astra_uart_put_u64(vector_slot);
    astra_uart_puts("\nCurrentEL: ");
    astra_uart_put_hex(current_el);
    astra_uart_puts("\nException class: ");
    astra_uart_put_hex(exception_class);
    astra_uart_puts("\nESR_ELx: ");
    astra_uart_put_hex(esr);
    astra_uart_puts("\nELR_ELx: ");
    astra_uart_put_hex(elr);
    astra_uart_puts("\nFAR_ELx: ");
    astra_uart_put_hex(far);
    astra_uart_puts("\nSP_ELx: ");
    astra_uart_put_hex(stack_pointer);
    astra_uart_puts("\nKernel stopped to prevent an uncontrolled exception return.\n");

    for (;;) {
        __asm__ volatile("wfe");
    }
}
