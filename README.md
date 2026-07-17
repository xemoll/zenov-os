# ZenovOS 0.1.1

ZenovOS is a compact 32-bit x86 operating system built with Zenov, assembler and freestanding C++17. Version 0.1.1 provides deterministic BIOS/FAT12 boot, E820 physical-memory discovery, 4 KiB paging, ring-3 ZEX1 and static ELF32 applications, a reusable kernel heap, guarded syscalls, transactional ZenovFS1 replacement and a real Zenov-source application target.

![ZenovOS 0.1.1 system console](./docs/screenshots/zenov-os-0.1.1-ci-console.png)

The screenshot is a real 720×400 QEMU framebuffer stored as a normal repository PNG.

## Status

The P0 completion scope is implemented with executable regression coverage:

- PMM and fragmented-heap stress tests;
- page-granular application mappings and `CR0.WP`;
- read-only code pages and W^X ELF admission policy;
- complete process-window scrubbing between applications;
- recoverable ring-3 faults with decoded diagnostics;
- stable syscall errors, guarded pointers, console input and `argc/argv`;
- copy-on-write ZenovFS1 replacement with exhaustive sector-write fault injection;
- deterministic `.zv` to ZEX1 compilation and ring-3 execution;
- deterministic system and release-package provenance.

See [`docs/INDEX.md`](docs/INDEX.md) and [`docs/ROADMAP_0.1.1.md`](docs/ROADMAP_0.1.1.md).

## Memory and isolation

- E820-backed management of the first 128 MiB;
- 4 KiB frame allocator with a 16-frame stress cycle;
- 2 MiB aligned heap with split, free, coalesce and invalid-free rejection;
- supervisor-only low kernel mapping;
- ring-3 base at `0x00400000` with a 1 MiB application limit;
- only current image and 16 KiB stack pages marked present;
- RX ELF text/rodata and writable data/BSS/stack pages;
- rejection of ELF load segments requesting both write and execute permission;
- zeroing of the reused 1 MiB process window before first use and after every normal exit or recoverable fault.

The current i686 paging mode does not provide a general per-page NX bit. W^X is an admission policy and code-write protection invariant, not complete hardware non-execution of writable data.

## Applications and ABI

Supported formats:

- ZEX1 version 1;
- static little-endian ELF32/i386 with validated `PT_LOAD` segments.

Bundled coverage:

```text
run HELLO
run FILEIO.ELF
run ARGS.ELF alpha beta
run CONSOLE.ELF
run PROTECT.ELF
run KACCESS.ELF
run ZENOVAPP.ZEX
```

The applications verify basic ZEX execution, userspace file I/O and persistence, initial stack arguments, stable errors, unmapped/RX pointer guards, bounded console input, writes to read-only code, supervisor isolation and Zenov-generated ring-3 code.

Syscalls use `INT 0x80`:

```text
0 exit            5 file_stat
1 write_console   6 get_version
2 get_ticks       7 sync
3 file_read       8 read_console
4 file_write
```

A ring-3 exception terminates only the foreground application, records vector/error/EIP and page-fault CR2 data, scrubs the process window and returns to the shell. Kernel faults remain fatal.

See [`docs/ABI_0.1.1.md`](docs/ABI_0.1.1.md) and [`docs/SECURITY_MODEL_0.1.1.md`](docs/SECURITY_MODEL_0.1.1.md).

## Zenov source target

The `zenov` repository provides a strict 0.1.1 freestanding subset:

```zenov
app("name");
say("message\n");
exit(0);
```

The compiler generates deterministic ZEX1, rejects unsupported hosted-language statements, verifies the container and compiles the same source twice for a byte-identical result. ZenovOS pins the merged compiler revision and canonical generated-artifact SHA-256 in its build manifest, packages the app into ZenovFS1 and runs it in ring 3.

## Persistent storage

The kernel drives a primary-master ATA PIO disk and mounts ZenovFS1 at `/data`.

```text
mount df fsck sync
pwd cd ls mkdir touch
write append cat stat
cp mv rm
```

ZenovFS1 retains 128 fixed metadata entries and 64 KiB file slots. Replacement writes use a compatible copy-on-write protocol: complete payload to a free slot, staging metadata, commit metadata, then old-entry cleanup. Mount recovery discards uncommitted staging or completes committed replacement. Host tests evaluate every sector-write prefix, and QEMU boots an intentionally interrupted committed image.

See [`docs/ZENOVFS1_TRANSACTIONS.md`](docs/ZENOVFS1_TRANSACTIONS.md).

## Zenov-owned configuration and shell

`kernel/main.zv` is a composition root for modules under `kernel/config/`. Guarded relative `include(...)` directives reject cycles, absolute paths, traversal outside the source root and nesting deeper than 16 levels.

The shell provides a 512-byte input buffer with 511 usable characters, 128 history entries, a 1024-event keyboard IRQ queue, horizontal scrolling and standard cursor/history editing. Stage0 verifies a 200-declaration configuration and explicit generated-text budgets.

## CI contract

The primary workflow performs:

1. strict host and freestanding compilation with warnings as errors;
2. FAT12, ZenovFS1, ZEX1 and ELF structural checks;
3. PMM, heap, process-scrub and loader-policy self-tests;
4. exhaustive host ZenovFS1 crash-boundary injection;
5. QEMU application, persistence and interrupted-recovery phases;
6. deterministic system rebuilding;
7. deterministic release ZIP generation and byte comparison;
8. evidence upload with images, applications, manifest, serial logs and framebuffer.

Important markers include:

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

## Build

Required: GNU Make, GNU binutils, a C++17 compiler, `qemu-system-i386`, `zip` and `unzip`.

```bash
make clean check
make qemu
make test
bash tools/package_release.sh build/zenov-os.img build/zenov-data.img dist package
```

## Release assets

The existing `v0.1.1` assets remain the installable baseline until rebuilt from the exact final post-hardening `main` commit. The final package must include both images, manifest, source revision, launchers, installation guide and checksums, then be re-downloaded and QEMU-verified.

[Open the ZenovOS 0.1.1 release](https://github.com/xemoll/zenov-os/releases/tag/v0.1.1)

## Limitations

ZenovOS remains a single-foreground-process i686/BIOS foundation. It intentionally does not include concurrent processes, per-process page directories, SMP, networking, USB, graphics, AHCI/NVMe, a physical-disk installer, dynamic linking, PE/DOS/Win32 compatibility or ZenovFS2 variable extents.

The automated hardware target is QEMU i386 with BIOS, floppy and IDE. VirtualBox is documented but not CI-verified.

## License

Original ZenovOS code is BSD-2-Clause. FAT12 loader lineage and the retained x16-PRos MIT notice are documented in `THIRD_PARTY.md`.
