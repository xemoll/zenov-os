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

  /* The repository verifier has a deep, bounded call chain with several
   * kilobyte-scale validation records.  The previous 16 KiB stack silently
   * overflowed into adjacent .bss.  Keep a dedicated 64 KiB bootstrap/kernel
   * stack in conventional RAM until large verifier frames are moved to heap.
   */
  .kernel_stack (NOLOAD) : ALIGN(16) {
    __kernel_stack_bottom = .;
    . += 65536;
    __kernel_stack_top = .;
  } :data
  ASSERT((__kernel_stack_top - __kernel_stack_bottom) >= 65536,
         "kernel stack is too small for verified boot")

  /* Ring-3 transitions use a separate stack so an application cannot consume
   * the boot/service stack budget. */
  .ring3_kernel_stack (NOLOAD) : ALIGN(16) {
    __stack_bottom = .;
    . += 16384;
    __stack_top = .;
  } :data
  __kernel_low_end = .;
  __kernel_end = __kernel_low_end;
  ASSERT(__kernel_low_end <= 0x000A0000,
         "kernel file image or early stacks overlap VGA aperture")

  /* Static zero-initialized state no longer consumes the scarce 0x10000-
   * 0x9ffff region.  This range is already permanently reserved by the PMM:
   * it starts exactly after the Live ISO COW workspace and ends immediately
   * before the existing ring-3 physical window.  entry.S clears this range
   * after A20 and protected mode are active, before paging is enabled.
   */
  . = 0x003B6000;
  __kernel_static_start = .;
  .bss (NOLOAD) : ALIGN(4096) {
    __bss_start = .;
    *(COMMON)
    *(.bss*)
    __bss_end = .;
  } :data
  __kernel_static_end = .;
  ASSERT(__kernel_static_end <= 0x00400000,
         "kernel static state overlaps the ring-3 user window")

  /DISCARD/ : {
    *(.comment*)
    *(.note*)
    *(.eh_frame*)
    *(.gcc_except_table*)
    *(.init_array*)
    *(.fini_array*)
  }
}
