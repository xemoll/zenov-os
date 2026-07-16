# ZenovOS 0.1.1 completion roadmap

This document defines the remaining scope for the `0.1.1` line. It is intentionally narrower than a general operating-system wishlist: graphics, networking, USB, SMP and broad hardware support belong to later releases.

## Current verified baseline

The `main` branch already provides:

- deterministic FAT12 boot and release packaging;
- E820 physical-memory discovery and a 4 KiB frame allocator;
- paging with supervisor and user address ranges;
- ring-3 execution through ZEX1 and static ELF32/i386;
- RX and RW ELF load segments;
- validated userspace pointers and file syscalls;
- persistent ATA PIO storage through ZenovFS1;
- modular Zenov configuration with guarded recursive includes;
- a 511-character interactive command line, 128 history entries and a 1024-event keyboard queue;
- two-boot QEMU verification for applications and persistence.

## P0 — required before declaring 0.1.1 complete

### 1. Page protection and fault diagnostics

Current state: the complete 4 MiB user window is mapped writable. ELF segments are validated and separated in the file, but page-table permissions do not yet enforce read-only code pages.

Required work:

- map only the pages required by the loaded program;
- clear the writable bit for ELF text/rodata pages after loading;
- keep data, BSS and stack writable;
- reject overlapping or permission-conflicting `PT_LOAD` segments;
- add a dedicated page-fault report with CR2, access type, privilege level and present/write/user bits;
- add QEMU negative tests for user writes to code and user access to supervisor memory.

Acceptance evidence:

```text
PAGE_PROTECTION_OK
USER_WRITE_TO_TEXT_BLOCKED
USER_KERNEL_ACCESS_BLOCKED
PAGE_FAULT_DIAGNOSTICS_OK
```

### 2. Reusable kernel heap

Current state: the heap is a 2 MiB monotonic bump allocator. It cannot free or reuse allocations.

Required work:

- replace the cursor-only allocator with bounded blocks and `free`;
- split and coalesce adjacent free blocks;
- preserve alignment guarantees;
- detect double-free, invalid-free and corrupted headers;
- expose used, free, peak and allocation-count diagnostics;
- stress-test repeated application and storage allocations.

Acceptance evidence:

```text
HEAP_REUSE_OK
HEAP_COALESCE_OK
HEAP_INVALID_FREE_BLOCKED
HEAP_STRESS_OK
```

### 3. Process and syscall contract completion

Current state: one foreground process runs at a time and exits through `INT 0x80`. There is no argument vector or input syscall.

Required work:

- define stable syscall error codes instead of one generic `0xFFFFFFFF` result;
- add `read_console` with bounded user buffers;
- pass `argc/argv` to ZEX1 and ELF32 applications;
- validate the initial user stack layout;
- record application name, format, exit code and fault reason in diagnostics;
- add a deliberately failing userspace test application.

Acceptance evidence:

```text
PROCESS_ARGV_OK
CONSOLE_READ_SYSCALL_OK
SYSCALL_ERRORS_OK
USER_FAULT_RETURNED_TO_SHELL
```

### 4. ZenovFS1 durability audit

Current state: metadata and payload checksums are validated, but power-loss behavior is not yet tested.

Required work:

- add host-side fault injection at every sector write boundary;
- document the exact metadata/payload write order;
- prove that interrupted writes preserve either the old file or the new file, not inconsistent metadata;
- add recovery behavior for an interrupted metadata update where possible without changing the disk format;
- defer journaling, variable extents or incompatible layout changes to `ZenovFS2` if compatibility cannot be preserved.

Acceptance evidence:

```text
ZENOVFS_FAULT_INJECTION_OK
ZENOVFS_INTERRUPTED_WRITE_RECOVERED
ZENOVFS_OLD_OR_NEW_CONTENT_ONLY
```

### 5. Zenov source-to-application path

Current state: Zenov owns system configuration, while bundled applications are assembled directly.

Required work across both repositories:

- define the supported Zenov subset for freestanding ZenovOS applications;
- compile a `.zv` application into ZEX1 or static ELF32;
- package the result into the ZenovFS data image;
- keep compiler/runtime changes in `zenov` synchronized with the OS ABI documentation;
- run the generated application in ring 3 in QEMU;
- add a cross-repository compatibility manifest containing Zenov compiler revision and ZenovOS ABI version.

Acceptance evidence:

```text
ZENOV_SOURCE_APP_BUILD_OK
ZENOV_SOURCE_APP_RING3_OK
ZENOV_COMPILER_ABI_MATCH_OK
```

## P1 — recommended for the final 0.1.1 release package

- replace scattered command dispatch logic with a command registry split by system, storage, process and diagnostics domains;
- add serial-log timestamps and structured panic/fault records;
- add release provenance: source commit, compiler commit, build manifest and image hashes in both the ZIP and the bootable system documents;
- refresh the `v0.1.1` GitHub Release assets from the final `main` commit;
- verify QEMU and VirtualBox launch instructions from a clean environment;
- publish the CI framebuffer PNG directly in the repository and release notes.

## Explicitly deferred beyond 0.1.1

The following features should not block completion of this patch line:

- preemptive multitasking and multiple concurrent processes;
- per-process address spaces;
- SMP;
- networking;
- USB;
- graphics or a window system;
- AHCI/NVMe;
- a physical-disk installer;
- PE, DOS or Win32 compatibility;
- dynamic linking;
- ZenovFS2 variable extents or journaling.

## Release rule

`0.1.1` is complete only when every P0 item has an executable regression test and the final package is rebuilt from the exact merged `main` commit. Documentation-only claims are not sufficient.