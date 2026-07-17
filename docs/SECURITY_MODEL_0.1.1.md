# ZenovOS 0.1.1 security model

## Trust boundaries

The kernel, bootloader and generated system configuration are trusted. ZEX1 and ELF32 application bytes stored on ZenovFS1 are untrusted until validated by the loader. Application-provided pointers, paths, sizes and syscall numbers are always treated as untrusted.

## Memory isolation

- Low linear memory from `0x00000000` through `0x003FFFFF` is supervisor-only.
- Ring-3 code and data use segment-relative offsets inside a 1 MiB window based at linear `0x00400000`.
- Only pages required by the current image and stack are present.
- Kernel writes honor read-only mappings because `CR0.WP` is enabled.
- ELF writable and executable load permissions may not coexist in one segment.
- ELF segments with conflicting write permissions may not share a 4 KiB page.
- The reused physical application window is zeroed before first use and after every normal application exit or recoverable user fault.

The i686 paging mode used by 0.1.1 does not provide a general per-page execute-disable bit. W^X is therefore a loader admission policy plus write protection for code pages, not full hardware NX enforcement for writable data pages.

## Syscall boundary

Every userspace range is validated for:

- arithmetic overflow and containment inside the 1 MiB process window;
- page presence;
- user accessibility;
- writable permission when the kernel will store data.

String pointers are checked byte-by-byte until a terminator is found within the syscall-specific bound. Unsupported calls return a stable error value rather than entering undefined dispatch behavior.

## Exception boundary

Ring-3 exceptions terminate only the foreground application. The kernel records application identity, vector, error code and EIP. Page-fault records also include CR2 and decoded present/write/user bits. Control returns through the same cleanup path used by `exit`, and that path scrubs the process window.

Exceptions originating in ring 0 remain fatal and enter the kernel panic path.

## Storage boundary

ZenovFS1 validates entry types, paths, parents, duplicate names, payload bounds and FNV-1a checksums. Replacement writes use a copy-on-write staging slot and recovery protocol. FNV-1a is an integrity checksum for accidental corruption; it is not a cryptographic authenticity mechanism.

## Explicit non-goals

Version 0.1.1 does not claim:

- isolation between concurrent processes;
- per-process page directories;
- address-space randomization;
- cryptographic executable signatures;
- secure boot;
- protection against malicious DMA devices;
- speculative-execution mitigations;
- filesystem confidentiality or authenticated storage;
- dynamic-linker hardening.

These limits are explicit so successful regression markers are not misrepresented as a general-purpose hardened operating system.
