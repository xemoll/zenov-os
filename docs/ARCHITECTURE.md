# ZenovOS 0.1.0 architecture

## Boot path

1. A 512-byte BIOS boot sector locates `KERNEL.BIN` in a deterministic FAT12
   image and loads it at physical address `0x10000`.
2. The assembly kernel entry gathers an E820 memory map, enables A20, installs a
   flat GDT and enters 32-bit protected mode.
3. Assembly clears `.bss`, installs a dedicated stack and calls the
   freestanding C++ kernel.

## Language split

- **Zenov (`kernel/main.zv`)**: canonical system name/version, visual defaults,
  boot messages, static commands and read-only VFS content.
- **Assembler**: BIOS loader, real-to-protected-mode transition, GDT, ISR/IRQ
  entry stubs and low-level CPU boundary.
- **C++17**: native Zenov stage0 compiler, FAT12 tools, kernel, VGA/serial
  console, IDT/PIC/PIT, keyboard driver, shell, RTC/CPUID/E820 diagnostics and
  panic path.

No Python source or Python runtime is part of the build, tests, image or kernel.

## Runtime facilities

- 32-bit flat protected mode
- 256-entry IDT with CPU exception diagnostics
- remapped 8259 PIC
- 100 Hz PIT uptime source
- IRQ1 keyboard input with shift/caps and command history navigation
- VGA text console mirrored to COM1
- BIOS E820 memory map
- CPUID vendor/features
- CMOS RTC date/time
- generated read-only VFS
- deterministic image and manifest generation
