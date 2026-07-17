# ZenovOS 0.1.1 completion status

This document is the release-completion record for the `0.1.1` line. It is intentionally narrower than a general operating-system wishlist: graphics, networking, USB, SMP and broad hardware support belong to later releases.

## P0 status: implemented and executable

All original P0 areas now have implementation, host-side validation where applicable, and QEMU runtime evidence.

### 1. Page protection, process-memory hygiene and fault diagnostics

Implemented:

- only pages required by the current program and its 16 KiB stack are present;
- ELF `PT_LOAD` ranges are bounds-checked, overlap-checked and page-permission-conflict checked;
- text/rodata pages are read-only after loading and data/BSS/stack pages are writable;
- `CR0.WP` is enabled so supervisor writes respect read-only page mappings;
- ELF segments requesting both write and execute permission are rejected by the 0.1.1 W^X policy;
- the complete 1 MiB physical userspace window is scrubbed before first use and after every normal exit or recoverable user fault;
- ring-3 faults record vector, error code, EIP and, for page faults, CR2 plus present/write/user decoding;
- deliberate writes to RX code and supervisor-selector access return control to the shell.

Acceptance evidence:

```text
PAGING_OK
USER_WINDOW_SCRUB_OK
USER_WINDOW_RUNTIME_SCRUB_OK
ELF_WX_POLICY_OK
PAGE_PROTECTION_OK
USER_WRITE_TO_TEXT_BLOCKED
USER_KERNEL_ACCESS_BLOCKED
PAGE_FAULT_DIAGNOSTICS_OK
USER_FAULT_RETURNED_TO_SHELL
```

### 2. Physical memory and reusable kernel heap

Implemented:

- a 16-frame PMM cycle checks uniqueness, alignment, invalid frees and exact counter restoration;
- the 2 MiB heap uses bounded blocks rather than a monotonic cursor;
- allocations support power-of-two alignment;
- free blocks split and coalesce;
- invalid and double frees are rejected;
- boot stress testing exercises fragmentation, reuse and complete coalescence;
- used, free, peak and active-allocation counters are available to kernel diagnostics.

Acceptance evidence:

```text
PMM_STRESS_OK
PMM_OK
HEAP_REUSE_OK
HEAP_COALESCE_OK
HEAP_INVALID_FREE_BLOCKED
HEAP_STRESS_OK
```

### 3. Foreground process and syscall ABI

Implemented:

- stable negative 32-bit error values replace a single generic failure;
- syscall pointer validation checks both mapping presence and required write permission;
- bounded console input is syscall 8;
- a validated ZenovOS 0.1.1 initial stack passes `argc` and an `argv` vector;
- application name, format, exit code and fault details are recorded;
- normal exits and user faults return to the shell through one cleanup path;
- dedicated applications test arguments, unsupported calls, unmapped pointers, writes to RX destinations, console input and deliberate protection faults.

Acceptance evidence:

```text
PROCESS_ABI_0_1_1_OK
PROCESS_ARGV_OK
SYSCALL_ERRORS_OK
SYSCALL_POINTER_GUARD_OK
CONSOLE_READ_SYSCALL_OK
USER_FAULT_RETURNED_TO_SHELL
```

See [`ABI_0.1.1.md`](ABI_0.1.1.md).

### 4. ZenovFS1 interrupted-write durability

Implemented without changing the ZenovFS1 superblock or 64-byte entry format:

- replacements write payload into a free slot first;
- a staging entry records the transaction;
- converting that staging entry into a committed file is the metadata commit point;
- recovery discards uncommitted staging entries or finishes committed replacements;
- host fault injection evaluates every sector-write prefix and accepts only the complete old payload or complete new payload;
- a dedicated QEMU boot mounts a deliberately interrupted committed transaction, performs recovery, reads the committed content and passes `fsck`.

Acceptance evidence:

```text
ZENOVFS_FAULT_INJECTION_OK
ZENOVFS_OLD_OR_NEW_CONTENT_ONLY
ZENOVFS_INTERRUPTED_WRITE_RECOVERED
ZENOVFS_FSCK_OK
```

See [`ZENOVFS1_TRANSACTIONS.md`](ZENOVFS1_TRANSACTIONS.md).

### 5. Zenov source-to-ring-3 application path

Implemented across both repositories:

- `zenov` provides a strict `zenov-os-app` target for the documented freestanding `app`, `say` and `exit` subset;
- compilation produces deterministic ZEX1 for ABI `0.1.1`;
- the same source is compiled twice and compared byte-for-byte;
- malformed or unsupported hosted-language statements are rejected;
- ZenovOS packages the generated artifact into ZenovFS1 and executes it in ring 3;
- the build manifest pins the merged Zenov compiler revision, ABI version, compiler-source hash and canonical artifact hash.

Acceptance evidence:

```text
ZENOV_SOURCE_APP_BUILD_OK
ZENOV_OS_APP_ARTIFACT_OK
ZENOV_OS_APP_COMPILER_CONTRACT_OK
ZENOV_SOURCE_APP_RING3_OK
ZENOV_COMPILER_ABI_MATCH_OK
```

## Release provenance

The deterministic release ZIP contains:

- the boot and writable data images;
- launch scripts and installation instructions;
- `BUILD-MANIFEST.json` with source, compiler and output hashes;
- `SOURCE-REVISION.txt` with the exact ZenovOS commit;
- external and internal SHA-256 checksum files.

Two independently generated packages and complete system rebuilds must remain byte-identical.

## P1 remaining before freezing the release assets

These tasks improve maintainability and distribution quality but no longer block the P0 functional baseline:

- split the monolithic command dispatcher into domain registries;
- add structured serial timestamps and stable machine-readable panic/fault records;
- refresh the repository framebuffer PNG from the final post-merge CI artifact;
- rebuild and replace GitHub Release `v0.1.1` assets from the exact final `main` commit;
- verify the documented VirtualBox path manually; QEMU remains the automated hardware acceptance target;
- add a reproducible source archive or SBOM to the release assets.

## Explicitly deferred beyond 0.1.1

The following features do not belong in this patch line:

- preemptive multitasking and concurrent processes;
- per-process page directories;
- SMP;
- networking;
- USB;
- graphics or a window system;
- AHCI/NVMe;
- a physical-disk installer;
- PE, DOS or Win32 compatibility;
- dynamic linking;
- ZenovFS2 variable extents or a new incompatible journal format.

## Freeze rule

`0.1.1` may be frozen only when the final `main` commit passes strict host and freestanding compilation, host crash injection, all QEMU phases, deterministic system rebuilding and deterministic release packaging. Documentation-only claims are not sufficient.
