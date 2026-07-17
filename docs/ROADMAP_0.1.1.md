# ZenovOS 0.1.1 completion status

This document records the completed scope of the `0.1.1` line. Graphics, networking, USB, SMP and broad hardware support belong to later versions.

## P0: implemented with executable evidence

### Memory protection and fault handling

Implemented:

- page-granular application and stack mappings;
- read-only ELF text/rodata, writable data/BSS/stack and `CR0.WP`;
- overlap, bounds, alignment and page-permission-conflict validation;
- W^X ELF admission policy;
- complete 1 MiB process-window scrub before first use and after every exit/fault;
- recoverable ring-3 page/general-protection faults with vector, error, EIP and CR2 diagnostics.

Evidence:

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

### PMM and reusable heap

Implemented:

- 16-frame PMM stress cycle with exact counter restoration;
- bounded aligned allocator with split, free and coalesce;
- invalid/double-free rejection;
- fragmentation/reuse stress cycle and diagnostics counters.

Evidence:

```text
PMM_STRESS_OK
PMM_OK
HEAP_REUSE_OK
HEAP_COALESCE_OK
HEAP_INVALID_FREE_BLOCKED
HEAP_STRESS_OK
```

### Process and syscall ABI

Implemented:

- stable error values;
- bounded console input syscall;
- `argc/argv` initial stack;
- mapped/writable user-pointer guards;
- application identity, exit and fault records;
- deliberate negative applications returning safely to the shell.

Evidence:

```text
PROCESS_ABI_0_1_1_OK
PROCESS_ARGV_OK
SYSCALL_ERRORS_OK
SYSCALL_POINTER_GUARD_OK
CONSOLE_READ_SYSCALL_OK
USER_FAULT_RETURNED_TO_SHELL
```

See [`ABI_0.1.1.md`](ABI_0.1.1.md).

### ZenovFS1 durability

Implemented without changing the on-disk superblock or 64-byte entry format:

- free-slot copy-on-write staging;
- metadata commit point and mount recovery;
- exhaustive host simulation of every sector-write prefix;
- QEMU boot from an intentionally interrupted committed transaction.

Evidence:

```text
ZENOVFS_FAULT_INJECTION_OK
ZENOVFS_OLD_OR_NEW_CONTENT_ONLY
ZENOVFS_INTERRUPTED_WRITE_RECOVERED
ZENOVFS_FSCK_OK
```

See [`ZENOVFS1_TRANSACTIONS.md`](ZENOVFS1_TRANSACTIONS.md).

### Zenov source application

Implemented in both repositories:

- strict `app` / `say` / `exit` freestanding subset;
- deterministic `.zv` to ZEX1 compilation;
- negative compiler contract tests;
- ZenovFS packaging and ring-3 execution;
- pinned Zenov revision, ABI and canonical output hash in the OS manifest.

Evidence:

```text
ZENOV_SOURCE_APP_BUILD_OK
ZENOV_OS_APP_ARTIFACT_OK
ZENOV_OS_APP_COMPILER_CONTRACT_OK
ZENOV_SOURCE_APP_RING3_OK
ZENOV_COMPILER_ABI_MATCH_OK
```

## Remaining release-freeze work

- merge the post-merge hardening PR after its full workflow passes;
- require a green push workflow on the resulting `main` commit;
- refresh the README framebuffer PNG from that exact CI artifact;
- rebuild and replace `v0.1.1` release assets from the final `main` commit;
- re-download, hash and QEMU-boot the public package;
- manually verify VirtualBox or keep it explicitly marked unverified.

See [`RELEASE_CHECKLIST_0.1.1.md`](RELEASE_CHECKLIST_0.1.1.md).

## Deferred beyond 0.1.1

- preemptive multitasking and concurrent processes;
- per-process page directories;
- SMP, networking, USB and graphics;
- AHCI/NVMe and a physical-disk installer;
- dynamic linking and foreign PE/DOS/Win32 compatibility;
- ZenovFS2 variable extents or an incompatible journal.

## Freeze rule

The version may be frozen only when the exact final `main` commit passes strict compilation, host crash injection, all QEMU phases, deterministic rebuilding and deterministic package generation. Documentation-only claims are not sufficient.
