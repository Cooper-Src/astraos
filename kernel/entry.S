.section .text.boot, "ax"
.global _start
.type _start, %function

.extern astra_exception_vectors

_start:
    msr daifset, #0xf
    adrp x1, __stack_top
    add x1, x1, :lo12:__stack_top
    mov sp, x1

    /* The UEFI loader may place the image away from the link address. */
    adrp x9, astra_exception_vectors
    add x9, x9, :lo12:astra_exception_vectors
    mrs x10, CurrentEL
    cmp x10, #0x4
    b.eq .Lset_vbar_el1
    cmp x10, #0x8
    b.eq .Lset_vbar_el2

.Lunsupported_el:
    wfe
    b .Lunsupported_el

.Lset_vbar_el1:
    msr VBAR_EL1, x9
    b .Lvbar_ready

.Lset_vbar_el2:
    msr VBAR_EL2, x9

.Lvbar_ready:
    isb
    bl astra_kernel_main

.Lkernel_stopped:
    wfe
    b .Lkernel_stopped

.size _start, . - _start

.section .bss.stack, "aw", %nobits
.balign 16
.global __stack_bottom
__stack_bottom:
    .skip 65536
.global __stack_top
__stack_top:
