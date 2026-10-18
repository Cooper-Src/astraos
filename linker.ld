OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(kernel_main)

PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0xffffffff80000000;

    .text : ALIGN(4096)
    {
        KEEP(*(.text.kernel_main))
        *(.text .text.*)
    } :text

    .limine_requests_start : ALIGN(8)
    {
        KEEP(*(.limine_requests_start))
    } :rodata

    .limine_requests : ALIGN(8)
    {
        KEEP(*(.limine_requests))
    } :rodata

    .limine_requests_end : ALIGN(8)
    {
        KEEP(*(.limine_requests_end))
    } :rodata

    .rodata : ALIGN(4096)
    {
        *(.rodata .rodata.*)
    } :rodata

    .data : ALIGN(4096)
    {
        *(.data .data.*)
    } :data

    .bss (NOLOAD) : ALIGN(4096)
    {
        *(COMMON)
        *(.bss .bss.*)
    } :data

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame*)
        *(.note*)
    }
}
