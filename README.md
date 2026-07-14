# ZenovOS 0.1.0

ZenovOS is an independent operating system built with **Zenov**, **assembler**
and **freestanding C++17**. The deep 0.1.0 update keeps the version number
unchanged while replacing the earlier Python-driven 16-bit prototype with a
native protected-mode kernel and native build tools.

## Current system surface

- BIOS/FAT12 boot path with deterministic 1.44 MiB image generation.
- 32-bit i686 protected mode, A20 and flat GDT.
- E820 memory discovery before leaving BIOS mode.
- IDT exception handling with panic diagnostics.
- remapped PIC, 100 Hz PIT and IRQ-driven PS/2 keyboard.
- VGA terminal mirrored to COM1.
- line editing, backspace, shift/caps handling and up/down history.
- CPUID, RTC, memory-map and uptime diagnostics.
- read-only VFS generated from `kernel/main.zv`.
- native C++ Zenov stage0 compiler, FAT12 builder and image verifier.
- no Python source and no Python dependency.

## Built-in shell commands

```text
help info ver uname cpu mem memmap uptime ticks date time
echo calc color ls cat history bootlog clear cls reboot halt panic
about license status
```

## Build

Required tools: GNU Make, GNU `as`/`ld`/`objcopy`, a C++17 compiler and
`qemu-system-i386` for the emulator smoke test.

```bash
make clean check   # native compiler tests + image build and validation
make qemu          # real QEMU boot, COM1 markers and framebuffer screendump
make test          # complete suite including deterministic rebuild
```

Outputs:

```text
build/BOOT.BIN
build/KERNEL.BIN
build/kernel.elf
build/kernel.map
build/zenov-os.img
build/build-manifest.json
build/qemu/serial.log
build/qemu/screenshot.ppm
```

## Zenov source contract

`kernel/main.zv` remains the canonical product configuration. Version 0.1.0 is
hard-pinned by the native compiler; a source declaring another version fails
the build.

Supported stage0 calls:

```text
system_name(string)
system_version(string)
shell_prompt(string)
theme(foreground, background)
boot_message(string)
shell_command(name, response)
vfs_file(name, content)
```

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the implementation split.

## License

Original ZenovOS code is BSD-2-Clause. FAT12 loader lineage and the retained
x16-PRos MIT notice are documented in `THIRD_PARTY.md`.
