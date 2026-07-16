# ZenovOS 0.1.1

ZenovOS is a compact 32-bit x86 operating system built with Zenov, assembler and freestanding C++17. Version 0.1.1 provides E820-backed physical memory management, 4 KiB paging, a validated static ELF32 loader, userspace file syscalls, modular Zenov-owned system configuration and a scalable interactive shell while retaining the deterministic FAT12 boot path and writable ATA/ZenovFS data volume.

![ZenovOS 0.1.1 system console](docs/screenshots/zenov-os-0.1.1-paging-elf.svg)

The image embeds the real 720×400 framebuffer capture produced by the QEMU CI run. It is not a mockup.

## Release package

The `v0.1.1` GitHub Release contains installation files only:

- `ZenovOS-0.1.1-x86.zip` — recommended package with both images, checksums, guide and QEMU launchers;
- `ZenovOS-0.1.1-x86.img` — bootable 1.44 MiB FAT12 system image;
- `INSTALL.txt` — QEMU, VirtualBox, storage and application instructions;
- `SHA256SUMS.txt` — hashes for the public downloads.

The ZIP also contains `ZenovOS-0.1.1-data.img`, a writable 16 MiB ATA/ZenovFS volume. ZenovOS still boots directly from the system image and does not yet install itself to a physical hard disk.

[Open the ZenovOS 0.1.1 release](https://github.com/xemoll/zenov-os/releases/tag/v0.1.1)

## Memory architecture

Version 0.1.1 provides:

- E820-backed physical frame discovery for the first 128 MiB;
- a 4 KiB frame allocator with an allocation/free startup self-test;
- paging enabled through CR3 with `CR0.PG` and `CR0.WP`;
- a 4 MiB supervisor-only low kernel mapping;
- a 4 MiB user-accessible mapping beginning at `0x00400000`;
- a 1 MiB ring-3 segment limit inside that mapping;
- a dedicated 16 KiB TSS transition stack, separate from the kernel call stack.

Diagnostics are available through `pmm`, `vm`, `mem` and `memmap`.

## Native applications

ZenovOS supports two native formats:

- `ZEX1` — the deterministic flat ZenovOS container;
- static ELF32/i386 executables with validated `PT_LOAD` segments.

Included applications:

```text
run HELLO
run FILEIO.ELF
```

`HELLO.ZEX` verifies the ZEX1 ring-3 path. `FILEIO.ELF` obtains the system version, writes a file, reads it back, checks metadata and content, flushes the filesystem and returns through `INT 0x80`.

The 0.1.1 syscall ABI provides:

```text
0  exit
1  write_console
2  get_ticks
3  file_read
4  file_write
5  file_stat
6  get_version
7  sync
```

User pointers are validated against the ring-3 address window before kernel access. One foreground application runs at a time; there is no scheduler, process spawning or dynamic linker yet.

See [`docs/ZEX_ABI.md`](docs/ZEX_ABI.md) and [`docs/ELF32_ABI.md`](docs/ELF32_ABI.md).

## Persistent storage

The protected-mode kernel drives a primary-master ATA PIO disk and mounts `ZenovFS1` at `/data`.

```text
mount  df  fsck  sync
pwd  cd  ls  mkdir  touch
write  append  cat  stat
cp  mv  rm
```

The release data image includes:

```text
/data/apps/hello.zex
/data/apps/fileio.elf
/data/config/system.ini
/data/docs/readme.txt
/data/docs/release.txt
```

ZenovFS1 currently uses 128 fixed entries and 64 KiB file slots. Paths are case-sensitive. Complete file reads verify FNV-1a checksums. The host verifier and kernel `fsck` independently validate metadata, parent directories, duplicates, bounds and payload checksums.

See [`docs/ZENOVFS.md`](docs/ZENOVFS.md).

## Zenov source architecture and shell scale

`kernel/main.zv` is a small composition root. Product identity, boot diagnostics, shell commands and generated system documents live in separate modules under `kernel/config/` and are assembled through guarded relative `include(...)` directives. Stage0 rejects include cycles, absolute paths, traversal outside the source root and nesting deeper than 16 levels.

The release shell has a 512-byte input buffer with 511 usable characters, 128 history entries and a 1024-event keyboard IRQ queue. Long commands use a horizontal viewport instead of being truncated by the 80-column VGA display. Stage0 separately verifies a 200-declaration configuration and enforces explicit per-item and aggregate generated-text budgets.

See [`docs/SOURCE_ARCHITECTURE.md`](docs/SOURCE_ARCHITECTURE.md).

## Verified QEMU behavior

CI performs two independent QEMU processes against the same runtime data disk.

First boot:

1. validates PMM and enables paging;
2. mounts ZenovFS;
3. runs kernel `fsck`;
4. enters a command beyond the legacy 80-byte input and 128-event keyboard boundaries;
5. writes `PERSIST.TXT` from the shell;
6. runs `HELLO.ZEX` in ring 3;
7. runs `FILEIO.ELF`, which creates `/data/apps/userio.txt` through syscalls;
8. cleanly returns both applications to the shell.

Second boot:

1. reads both persisted files;
2. verifies the ELF-created payload;
3. runs `fsck` again;
4. verifies file size and checksum.

Required serial evidence includes:

```text
PMM_OK
PAGING_OK
ZENOVFS_MOUNT_OK
PROCESS_ABI_0_1_1_OK
longinputend511ok
HELLO_ZEX_0_1_1_OK
FILEIO_ELF_OK
FILE_SYSCALL_PERSIST_OK
ZENOVFS_FSCK_OK
APP_EXIT code=0
```

The same workflow also verifies strict native compilation, absence of Python, source encoding, FAT12 structure, undefined symbols, host-side ZenovFS integrity, deterministic boot/data/application rebuilds and byte-identical Release ZIP generation.

## `.exe` compatibility

ZenovOS 0.1.1 does not run arbitrary Windows or DOS `.exe` files.

- Windows executables use PE/PE32+ and require Win32/NT APIs, DLL loading, virtual memory and other services.
- DOS MZ executables require 16-bit execution, PSP/environment handling and DOS interrupt services.

Static ZenovOS-targeted ELF32 applications now run, but a filename extension alone does not make a foreign executable compatible.

## Build from source

Required tools: GNU Make, GNU `as`/`ld`/`objcopy`, a C++17 compiler, `qemu-system-i386`, `zip` and `unzip`.

```bash
make clean check
make qemu
make test
bash tools/package_release.sh build/zenov-os.img build/zenov-data.img dist package
```

`kernel/main.zv` composes the modules under `kernel/config/`. The native stage0 compiler expands and validates those modules, generates the kernel configuration header and pins this branch to version `0.1.1`.

## Current limitations

ZenovOS remains an early protected-mode operating-system foundation. It does not yet provide multitasking, per-process page directories, a general heap with free/reuse, writable FAT16, a hard-disk installer, graphics, networking, USB, dynamic linking, permissions or DOS/Windows compatibility.

## License

Original ZenovOS code is BSD-2-Clause. FAT12 loader lineage and the retained x16-PRos MIT notice are documented in `THIRD_PARTY.md`.
