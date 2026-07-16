OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

PHDRS
{
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
}

SECTIONS
{
  . = 0;
  .text : ALIGN(16) { *(.text*) } :text
  .rodata : ALIGN(16) { *(.rodata*) } :text
  .data : ALIGN(16) { *(.data*) } :data
  .bss : ALIGN(16) { *(COMMON) *(.bss*) } :data
  /DISCARD/ : { *(.comment*) *(.note*) *(.eh_frame*) }
}
