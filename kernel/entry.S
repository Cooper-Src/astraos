.section .text.boot, "ax"
.global _start
.type _start, %function

_start:
    msr daifset, #0xf
    adrp x1, __stack_top
    add x1, x1, :lo12:__stack_top
    mov sp, x1
    bl astra_kernel_main

1:
    wfe
    b 1b

.size _start, . - _start

.section .bss.stack, "aw", %nobits
.balign 16
.global __stack_bottom
__stack_bottom:
    .skip 65536
.global __stack_top
__stack_top:
