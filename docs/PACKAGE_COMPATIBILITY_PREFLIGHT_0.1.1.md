# ZenPkg compatibility preflight

ZenPkg 0.1.1 has an independent, fail-closed structural safety layer for raw foreign executables. It is intentionally separate from ZenovGuard: this change does not alter the antivirus classifier, signature database or verdict logic.

## Command and result contract

```text
pkg compat check <file>
```

The command reads one exact bounded ZenovFS snapshot of 1 to 65536 bytes and reports:

- `Recognized`: the base classifier identified a supported preflight target;
- `Structure`: every format-specific bounds and layout rule passed;
- `Trust`: whether a platform signature or repository identity was verified;
- `Runtime`: whether ZenovOS has a real execution sandbox for that exact subset;
- `Verdict`: `blocked`, `inspect-only` or `runnable-sandbox`.

Raw PE, PS-X EXE, PS2 ELF and XBE files always retain `Trust: unverified`. Passing structure does not install, authorize or execute a file.

## Implemented validators

| Format | Accepted structural subset | Execution status |
| --- | --- | --- |
| Linux ELF32/i386 | Static `ET_EXEC`; bounded non-overlapping `PT_LOAD`; executable entry; no interpreter/dynamic segment; no W+X | Minimal ring-3 write/exit sandbox |
| Windows PE32/i386 | Valid DOS/PE/COFF and PE32 optional headers; executable non-DLL; bounded aligned sections; executable entry; no overlaps or W+X | Not implemented |
| PS-X EXE | Complete 0x800-byte header; bounded payload; entry/load/fill/stack inside isolated two-MiB PS1 RAM | Not implemented |
| PS2 R5900 ELF | ELF32 little-endian `ET_EXEC` with R5900/MIPS III flags; bounded static segments; executable entry; no overlaps or W+X | Not implemented |
| Original Xbox XBE | Bounded image/header/certificate/section table; raw and virtual section ranges; no overlaps or W+X | Not implemented |

The 64 KiB command window is a deliberate kernel-memory bound, not a claim that full retail games fit in memory. Large media probing remains a host-side streaming operation. Future runtime providers must consume content through bounded, user-supplied lawful inputs and must not bypass platform licensing or cryptography.

## Evidence

`make zenpkg-foreign-check` compiles the same pure validator used by the kernel and exercises 33 positive and negative cases plus every unsafe truncated prefix at 4492 boundary sizes. Mutations cover truncation, wrong architecture, DLLs, invalid entry points, dynamic/interpreted segments, out-of-range images, overlaps, bad alignment and writable-plus-executable mappings.

```text
PACKAGE_COMPATIBILITY_PREFLIGHT_TEST_OK cases=33 truncations=4492 validators=5 runtime-ready=1 fail-closed=1
```

`make zenpkg-foreign-qemu` additionally requires the in-system command to approve the Linux fixture before the ring-3 runtime starts. A host without `qemu-system-i386` can build and run all pure tests but cannot produce that guest marker.

The implementation and tests use only in-tree code and standard build tools. No paid service, subscription, telemetry or proprietary SDK is required.
