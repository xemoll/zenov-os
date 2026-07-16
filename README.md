# ZenovOS 0.1.0

ZenovOS is a compact x86 operating system built with Zenov, assembler and
freestanding C++17. It boots from a deterministic FAT12 image and provides an
interactive text-mode system workspace.

## System console

The home screen is organized into three practical areas: current system state,
keyboard shortcuts and common workspace actions. A fixed header shows the
session, execution mode, readiness and uptime. The bottom bar keeps the primary
shortcuts visible without filling the workspace with boot diagnostics.

![ZenovOS 0.1.0 system console](docs/screenshots/zenov-os-0.1.0.png)

The screenshot is captured from the verified QEMU build. It is not a mockup.

Console controls:

- `F1` opens the command reference;
- `F2` opens system details;
- `F3` lists built-in files;
- `F4` returns to the home dashboard;
- `Tab` completes command names;
- `Esc` clears the current input;
- Left/Right, Home/End, Backspace and Delete edit the command line;
- Up/Down recalls the last 16 commands while preserving the unfinished draft.

Three restrained VGA themes are available:

```text
theme midnight
theme graphite
theme amber
```

## Download

The GitHub Release contains installation-related files only:

- `ZenovOS-0.1.0-x86.zip` — recommended package with the boot image, guide and QEMU launchers for Linux/macOS and Windows;
- `ZenovOS-0.1.0-x86.img` — raw 1.44 MiB bootable image;
- `INSTALL.txt` — QEMU, VirtualBox and physical-media instructions;
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
- VGA workspace mirrored to COM1;
- full single-line editing, Tab completion and function-key navigation;
- `status`, `devices`, `disk`, CPU, RTC, memory-map and uptime views;
- read-only VFS generated from `kernel/main.zv`;
- native C++17 Zenov stage0 compiler, FAT12 builder and image verifier;
- deterministic installation ZIP builder;
- no Python source or runtime dependency.

## Commands

```text
home help info system status devices disk ver version uname
cpu mem memmap uptime ticks date time echo calc theme color
ls files dir cat open view history bootlog clear cls
reboot halt shutdown panic about license
```

## Verified build

GitHub Actions boots the system and exercises the interface before accepting a
commit. The QEMU smoke test opens Help with `F1`, returns Home with `F4`, and
executes `status` through `sta<Tab>` completion.

Required serial evidence:

```text
ZENOVOS_BOOT_OK
ZENOVOS_UI_READY
COMMAND REFERENCE
SYSTEM STATUS
zenov>
```

Current verified outputs:

```text
BOOT.BIN        512 bytes
KERNEL.BIN      25,008 bytes
kernel.elf      33,344 bytes
zenov-os.img    1,474,560 bytes
```

CI also checks UTF-8 source integrity, Zenov parser failure cases, FAT12
structure, undefined ELF symbols, a byte-identical system rebuild and two
byte-identical installation-package builds.

## Build from source

Required tools: GNU Make, GNU `as`/`ld`/`objcopy`, a C++17 compiler,
`qemu-system-i386`, `zip` and `unzip`.

```bash
make clean check   # compile, build the image and validate FAT12
make qemu          # boot and exercise the console in QEMU
make test          # complete suite with deterministic rebuild
bash tools/package_release.sh build/zenov-os.img dist package
```

Developer outputs remain CI artifacts and are not added to the public Release:

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
