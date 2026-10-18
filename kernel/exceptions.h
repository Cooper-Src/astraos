#ifndef ASTRAOS_EXCEPTIONS_H
#define ASTRAOS_EXCEPTIONS_H

#include <stdint.h>

/*
 * Reports a fatal AArch64 exception and halts. The vector slot is 0-15 in the
 * architectural VBAR table; current_el uses the architectural CurrentEL value.
 */
_Noreturn void astra_exception_report(
    uint64_t vector_slot,
    uint64_t esr,
    uint64_t elr,
    uint64_t far,
    uint64_t stack_pointer,
    uint64_t current_el
);

#endif
