# ZenovOS 0.1.0

ZenovOS is a small x86 operating system built with Zenov, assembler and
freestanding C++17. It boots from a deterministic FAT12 image and provides an
interactive text-mode system console.

## System console

The console uses a fixed header and command bar, a protected scrolling area and
a restrained VGA palette. Boot diagnostics are written to COM1 instead of
filling the user workspace.

The screenshot below is captured from the verified QEMU build. It is not a
mockup.

![ZenovOS 0.1.0 system console](docs/screenshots/zenov-os-0.1.0.png)

## Download

The GitHub Release contains installation-related files only:

- `ZenovOS-0.1.0-x86.zip` — boot image and setup instructions;
- `ZenovOS-0.1.0-x86.img` — raw 1.44 MiB bootable image;
- `INSTALL.txt` — QEMU, virtual-machine and physical-media instructions;
- `SHA256SUMS.txt` — download verification.

[Open the ZenovOS 0.1.0 release](https://github.com/xemoll/zenov-os/releases/tag/v0.1.0)

ZenovOS currently boots directly from the image. Version 0.1.0 does not yet
include a hard-disk installer.

## Current system

- BIOS/FAT12 boot path;
- E820 memory discovery and A20 setup;
- 32-bit i686 protected mode with a flat GDT;
- IDT exception handling and kernel panic diagnostics;
- remapped PIC, 100 Hz PIT and IRQ-driven PS/2 keyboard;
- VGA console mirrored to COM1;
- line editing, Shift/Caps Lock and Up/Down command history;
- CPUID, RTC, memory-map and uptime commands;
- read-only VFS generated from `kernel/main.zv`;
- native C++17 Zenov stage0 compiler, FAT12 builder and image verifier;
- no Python source or runtime dependency.

## Verified build

GitHub Actions requires these serial markers before accepting the QEMU boot:

```text
ZENOVOS_BOOT_OK
ZENOVOS_UI_READY
zenov>
```

Current verified outputs:

```text
BOOT.BIN        512 bytes
KERNEL.BIN      14,664 bytes
kernel.elf      22,264 bytes
zenov-os.img    1,474,560 bytes
```

The CI suite also checks UTF-8 source integrity, Zenov parser failure cases,
FAT12 structure, undefined ELF symbols and a byte-identical rebuild manifest.

## Commands

```text
help info ver uname cpu mem memmap uptime ticks date time
echo calc color ls cat history bootlog clear cls reboot halt panic
about license status
```

## Build from source

Required tools: GNU Make, GNU `as`/`ld`/`objcopy`, a C++17 compiler and
`qemu-system-i386`.

```bash
make clean check   # compile, build the image and validate FAT12
make qemu          # boot in QEMU and capture serial/framebuffer evidence
make test          # complete suite with deterministic rebuild
```

Build outputs remain CI/developer artifacts and are not added to the public
Release:

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

`kernel/main.zv` defines the product name, version, prompt, boot messages,
project commands and read-only VFS files. The native stage0 compiler rejects a
system version other than `0.1.0`.

Supported calls:

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
