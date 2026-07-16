OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)
SECTIONS
{
  . = 0;
  .text : ALIGN(16) { *(.text*) }
  .rodata : ALIGN(16) { *(.rodata*) }
  .data : ALIGN(16) { *(.data*) }
  .bss : ALIGN(16) { *(COMMON) *(.bss*) }
  /DISCARD/ : { *(.comment*) *(.note*) *(.eh_frame*) }
}
