OUTPUT_FORMAT("elf32-powerpc")
ENTRY(booter)
SECTIONS
{
	. = 0x81700000;
	.text : { *(.text.boot) *(.text*) *(.rodata*) *(.data*) *(.sdata*) *(.sbss*) *(.bss*) *(COMMON) }
	/DISCARD/ : { *(.comment) *(.eh_frame*) }
}
