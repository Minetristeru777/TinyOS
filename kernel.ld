ENTRY(kernel_main)
SECTIONS
{
    . = 0x8200;
    .text : { *(.text.kernel_main) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    __bss_start = .;
    .bss : { *(.bss*) *(COMMON) }
    __bss_end = .;
}
