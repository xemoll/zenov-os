OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(kernel_entry16)
PHDRS {
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
}
SECTIONS
{
  . = 0x00010000;
  __kernel_start = .;
  .text : ALIGN(16) {
    KEEP(*(.text.boot))
    *(.text*)
  } :text
  .rodata : ALIGN(16) { *(.rodata*) } :text
  .data : ALIGN(16) { *(.data*) } :data
  __kernel_file_end = .;
  .bss : ALIGN(16) {
    __bss_start = .;
    *(COMMON)
    *(.bss*)
    __bss_end = .;
  } :data
  .kernel_stack (NOLOAD) : ALIGN(16) {
    __kernel_stack_bottom = .;
    . += 16384;
    __kernel_stack_top = .;
  } :data
  .ring3_kernel_stack (NOLOAD) : ALIGN(16) {
    __stack_bottom = .;
    . += 16384;
    __stack_top = .;
  } :data
  __kernel_end = .;
  /DISCARD/ : {
    *(.comment*)
    *(.note*)
    *(.eh_frame*)
    *(.gcc_except_table*)
    *(.init_array*)
    *(.fini_array*)
  }
}
