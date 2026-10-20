.section .text.boot, "ax"
.global _start
.type _start, %function

.extern astra_exception_vectors

_start:
    msr daifset, #0xf
    adrp x1, __stack_top
    add x1, x1, :lo12:__stack_top
    /* Do not rely on inherited firmware SPSel state. */
    msr SPSel, #1
    isb
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
    b .Ldisable_inherited_mmu_el1

.Lset_vbar_el2:
    msr VBAR_EL2, x9
    b .Ldisable_inherited_mmu_el2

/*
 * UEFI AArch64 enters OS loaders with the MMU enabled and only guarantees
 * identity mappings for RAM. Device/MMIO regions such as the GOP framebuffer
 * may therefore be unmapped after ExitBootServices(). AstraOS does not yet
 * install its own page tables, so retain the physical/virtual identity model
 * for the early kernel by disabling the inherited firmware MMU before calling
 * into C. This also makes the GOP framebuffer directly addressable.
 *
 * The current kernel is deliberately an early bring-up environment without
 * virtual memory. When real page tables are introduced, this block should be
 * replaced by the kernel's MMU initialization rather than left as a permanent
 * policy.
 */
.Ldisable_inherited_mmu_el1:
    dsb sy
    mrs x11, SCTLR_EL1
    bic x11, x11, #1          // SCTLR_EL1.M: disable stage-1 MMU
    msr SCTLR_EL1, x11
    isb
    dsb sy
    b .Lvbar_ready

.Ldisable_inherited_mmu_el2:
    dsb sy
    mrs x11, SCTLR_EL2
    bic x11, x11, #1          // SCTLR_EL2.M: disable stage-1 MMU
    msr SCTLR_EL2, x11
    isb
    dsb sy

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
