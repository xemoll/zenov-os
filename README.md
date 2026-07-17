# ZenovOS 0.1.1

ZenovOS is a compact 32-bit x86 operating system built with Zenov, assembler and freestanding C++17. Version 0.1.1 is a protected-mode foundation with deterministic BIOS/FAT12 boot, E820 physical-memory discovery, 4 KiB paging, ring-3 ZEX1 and static ELF32 applications, a reusable kernel heap, guarded syscalls, a transactional ZenovFS1 data volume and a real Zenov-source application path.

![ZenovOS 0.1.1 system console](./docs/screenshots/zenov-os-0.1.1-ci-console.png)

The image is a real 720×400 QEMU framebuffer stored as a normal repository PNG.

## 0.1.1 status

The P0 completion scope is implemented with executable regression coverage:

- PMM and fragmented-heap stress tests;
- page-granular application mappings and `CR0.WP`;
- W^X ELF policy and read-only text pages;
- complete userspace-window scrubbing between applications;
- recoverable ring-3 page/general-protection faults;
- stable syscall errors, guarded pointers, console input and `argc/argv`;
- copy-on-write ZenovFS1 replacement with exhaustive sector-boundary fault injection;
- deterministic `.zv` to ZEX1 compilation and ring-3 execution;
- deterministic system images and release ZIP provenance.

See [`docs/ROADMAP_0.1.1.md`](docs/ROADMAP_0.1.1.md) for the completion record and remaining release-freeze tasks.

## Release package

The published `v0.1.1` release remains the installable baseline until its assets are rebuilt from the final post-hardening `main` commit.

The deterministic package generator produces:

- `ZenovOS-0.1.1-x86.img` — 1.44 MiB FAT12 boot image;
- `ZenovOS-0.1.1-data.img` — 16 MiB writable ATA/ZenovFS1 volume;
- `ZenovOS-0.1.1-x86.zip` — both images, QEMU launchers and installation guide;
- `BUILD-MANIFEST.json` — ABI, compiler revision and source/output hashes;
- `SOURCE-REVISION.txt` — exact ZenovOS source commit;
- external and in-package SHA-256 checksum files.

[Open the ZenovOS 0.1.1 release](https://github.com/xemoll/zenov-os/releases/tag/v0.1.1)

## Memory and isolation

Version 0.1.1 provides:

- E820-backed management of the first 128 MiB;
- a 4 KiB frame allocator with a 16-frame allocation/free stress cycle;
- a 2 MiB bounded heap with aligned allocation, free, split and coalesce;
- invalid/double-free rejection and fragmentation/reuse stress testing;
- a supervisor-only low 4 MiB kernel mapping;
- a ring-3 linear base at `0x00400000` with a 1 MiB application limit;
- only executable/data/stack pages for the current application marked present;
- a 16 KiB writable application stack;
- read-only ELF text/rodata and writable data/BSS pages;
- rejection of ELF load segments requesting both write and execute permission;
- full zeroing of the reused 1 MiB process window before first use and after every exit or recoverable fault.

Diagnostics are available through `pmm`, `vm`, `mem` and `memmap`.

## Foreground application runtime

ZenovOS supports:

- `ZEX1` — a deterministic flat ZenovOS container;
- static little-endian ELF32/i386 with validated `PT_LOAD` segments.

Bundled applications:

```text
run HELLO
run FILEIO.ELF
run ARGS.ELF alpha beta
run CONSOLE.ELF
run PROTECT.ELF
run KACCESS.ELF
run ZENOVAPP.ZEX
```

Coverage:

- `HELLO.ZEX` — basic ZEX1 ring-3 execution;
- `FILEIO.ELF` — version, write, stat, read, content verification and sync;
- `ARGS.ELF` — initial stack, stable errors and pointer guards;
- `CONSOLE.ELF` — bounded console-input syscall;
- `PROTECT.ELF` — deliberate write to an RX page;
- `KACCESS.ELF` — deliberate supervisor-selector access;
- `ZENOVAPP.ZEX` — code generated from `user/hello_zenov.zv`.

One foreground application runs at a time. User faults terminate only that application, record vector/error/EIP and page-fault CR2 data, scrub the process window and return to the shell. Kernel faults remain fatal.

### Syscalls

```text
0  exit
1  write_console
2  get_ticks
3  file_read
4  file_write
5  file_stat
6  get_version
7  sync
8  read_console
```

User ranges are checked for overflow, presence and required write permission before kernel access.

See [`docs/ABI_0.1.1.md`](docs/ABI_0.1.1.md), [`docs/ZEX_ABI.md`](docs/ZEX_ABI.md) and [`docs/ELF32_ABI.md`](docs/ELF32_ABI.md).

## Zenov source application target

The `zenov` repository and ZenovOS share a pinned 0.1.1 application contract. The strict freestanding subset currently contains:

```zenov
app("name");
say("message\n");
exit(0);
```

The compiler produces deterministic ZEX1, verifies the container and checksum, rejects unsupported hosted-language statements, and compiles the same source twice for a byte-identical comparison. ZenovOS records the merged compiler revision and canonical generated-artifact SHA-256 in its build manifest, packages the application into `/data/apps`, and executes it in ring 3 under QEMU.

## Persistent storage

The protected-mode kernel drives a primary-master ATA PIO disk and mounts ZenovFS1 at `/data`.

```text
mount  df  fsck  sync
pwd  cd  ls  mkdir  touch
write  append  cat  stat
cp  mv  rm
```

ZenovFS1 keeps its compatible 128-entry, fixed 64 KiB-slot layout. File replacement is now copy-on-write:

1. write the complete new payload into a free slot;
2. write staging metadata;
3. commit the new file metadata;
4. clear old metadata and transaction fields.

Mount recovery discards uncommitted staging entries or completes committed replacements. A host harness evaluates every possible sector-write prefix and permits only complete old or complete new content. QEMU separately boots an intentionally interrupted committed image, performs recovery, reads the committed content and passes kernel `fsck`.

See [`docs/ZENOVFS.md`](docs/ZENOVFS.md) and [`docs/ZENOVFS1_TRANSACTIONS.md`](docs/ZENOVFS1_TRANSACTIONS.md).

## Zenov-owned system configuration and shell

`kernel/main.zv` is a composition root. Product identity, boot diagnostics, static shell commands and generated documents live under `kernel/config/` and are assembled through guarded relative `include(...)` directives. Stage0 rejects cycles, absolute paths, traversal outside the source root and nesting deeper than 16 levels.

The shell provides:

- a 512-byte buffer with 511 usable characters;
- 128 history entries;
- a 1024-event keyboard IRQ queue;
- horizontal scrolling for long commands;
- Left/Right, Home/End, Delete, history and Tab completion.

Stage0 separately compiles a 200-declaration regression configuration and enforces explicit per-item and aggregate generated-text budgets.

See [`docs/SOURCE_ARCHITECTURE.md`](docs/SOURCE_ARCHITECTURE.md).

## Verified CI behavior

The primary workflow performs:

1. strict host and freestanding compilation with warnings as errors;
2. FAT12, ZenovFS1, ZEX1 and ELF structural checks;
3. PMM, heap and loader-policy self-tests;
4. exhaustive host ZenovFS1 crash-boundary injection;
5. a first QEMU boot running all seven application paths, long shell input and persistence writes;
6. a second QEMU boot verifying persisted shell and userspace files;
7. a third QEMU boot recovering a deliberately interrupted filesystem transaction;
8. deterministic full-system rebuilding;
9. deterministic release ZIP generation and byte comparison;
10. upload of images, applications, manifest, serial logs and framebuffer evidence.

Important runtime markers include:

```text
PMM_STRESS_OK
HEAP_STRESS_OK
USER_WINDOW_SCRUB_OK
USER_WINDOW_RUNTIME_SCRUB_OK
ELF_WX_POLICY_OK
PROCESS_ARGV_OK
CONSOLE_READ_SYSCALL_OK
USER_WRITE_TO_TEXT_BLOCKED
USER_KERNEL_ACCESS_BLOCKED
PAGE_FAULT_DIAGNOSTICS_OK
ZENOVFS_OLD_OR_NEW_CONTENT_ONLY
ZENOVFS_INTERRUPTED_WRITE_RECOVERED
ZENOV_SOURCE_APP_RING3_OK
ZENOV_COMPILER_ABI_MATCH_OK
```

## Build from source

Required tools: GNU Make, GNU `as`/`ld`/`objcopy`, a C++17 compiler, `qemu-system-i386`, `zip` and `unzip`.

```bash
make clean check
make qemu
make test
bash tools/package_release.sh build/zenov-os.img build/zenov-data.img dist package
```

## Current limitations

ZenovOS remains an early single-address-space operating-system foundation. Version 0.1.1 intentionally does not include preemptive multitasking, concurrent processes, per-process page directories, SMP, networking, USB, graphics, AHCI/NVMe, a physical-disk installer, dynamic linking, PE/DOS/Win32 compatibility or ZenovFS2 variable extents.

The automated hardware acceptance target is QEMU/i386 with BIOS, floppy and IDE. VirtualBox settings are documented but are not CI-verified.

## License

Original ZenovOS code is BSD-2-Clause. FAT12 loader lineage and the retained x16-PRos MIT notice are documented in `THIRD_PARTY.md`.
