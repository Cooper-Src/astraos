.section .text.exceptions, "ax"
.balign 2048

.global astra_exception_vectors
.type astra_exception_vectors, %object
astra_exception_vectors:
.macro ASTRA_VECTOR slot
    mov x0, #\slot
    b astra_exception_entry
    .space 120
.endm

/* Current EL using SP_EL0: synchronous, IRQ, FIQ, SError. */
ASTRA_VECTOR 0
ASTRA_VECTOR 1
ASTRA_VECTOR 2
ASTRA_VECTOR 3

/* Current EL using SP_ELx: synchronous, IRQ, FIQ, SError. */
ASTRA_VECTOR 4
ASTRA_VECTOR 5
ASTRA_VECTOR 6
ASTRA_VECTOR 7

/* Lower AArch64 EL: synchronous, IRQ, FIQ, SError. */
ASTRA_VECTOR 8
ASTRA_VECTOR 9
ASTRA_VECTOR 10
ASTRA_VECTOR 11

/* Lower AArch32 EL: synchronous, IRQ, FIQ, SError. */
ASTRA_VECTOR 12
ASTRA_VECTOR 13
ASTRA_VECTOR 14
ASTRA_VECTOR 15

.size astra_exception_vectors, . - astra_exception_vectors

.type astra_exception_entry, %function
astra_exception_entry:
    /* Interrupts are masked during early boot. Preserve the vector slot in x0. */
    mrs x6, CurrentEL
    cmp x6, #0x4
    b.eq .Lreport_el1
    cmp x6, #0x8
    b.eq .Lreport_el2

.Lunsupported_el:
    wfe
    b .Lunsupported_el

.Lreport_el1:
    mrs x1, ESR_EL1
    mrs x2, ELR_EL1
    mrs x3, FAR_EL1
    mov x4, sp
    mov x5, x6
    bl astra_exception_report
    b .Lunsupported_el

.Lreport_el2:
    mrs x1, ESR_EL2
    mrs x2, ELR_EL2
    mrs x3, FAR_EL2
    mov x4, sp
    mov x5, x6
    bl astra_exception_report
    b .Lunsupported_el

.size astra_exception_entry, . - astra_exception_entry
