# ZenovOS 0.1.1 security model

## Enforced boundaries

- Low linear memory is supervisor-only.
- Ring-3 applications are limited to a 1 MiB segment-relative window based at `0x00400000`.
- Only current image and stack pages are present.
- `CR0.WP` makes kernel writes respect read-only application mappings.
- ELF W+X load segments and page-level permission conflicts are rejected.
- The reused physical user window is zeroed before first use and after every normal exit or recoverable fault.
- Syscall buffers are checked for containment, page presence and write permission when required.
- Ring-3 faults terminate the application and return through the common scrub path; ring-0 faults panic.
- ZenovFS1 verifies metadata structure and payload checksums and uses a copy-on-write replacement protocol.

The i686 paging mode used by 0.1.1 has no general per-page NX bit. W^X is therefore an admission policy plus code-write protection, not complete hardware non-execution of writable data pages.

## Explicit non-goals

Version 0.1.1 does not claim concurrent-process isolation, per-process page directories, ASLR, cryptographic executable signatures, secure boot, DMA protection, speculative-execution mitigations, authenticated storage or dynamic-linker hardening.
