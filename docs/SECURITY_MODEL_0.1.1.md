# ZenovOS 0.1.1 security model

## Enforced boundaries

- Low linear memory is supervisor-only.
- Ring-3 applications are limited to a 1 MiB segment-relative window based at `0x00400000`.
- Only current image and stack pages are present.
- `CR0.WP` makes kernel writes respect read-only application mappings.
- ELF W+X load segments and page-level permission conflicts are rejected.
- The reused physical user window is zeroed before first use and after every normal exit or recoverable fault.
- Syscall buffers are checked for containment, page presence and write permission when required.
- Every trusted application has an immutable syscall capability mask; file access additionally requires an exact normalized path scope.
- Capability authority is installed only after final-read ZGDB2 and path-plus-SHA-256 appraisal and is cleared at both launch boundaries.
- Capability and file-scope denials are committed to the persistent ZGAL1 audit journal; audit failure retains the fail-closed execution lock.
- Ring-3 faults terminate the application and return through the common scrub-and-authority-clear path; ring-0 faults panic.
- ZenovFS1 verifies metadata structure and payload checksums and uses a copy-on-write replacement protocol.

The i686 paging mode used by 0.1.1 has no general per-page NX bit. W^X is therefore an admission policy plus code-write protection, not complete hardware non-execution of writable data pages.

The syscall capability layer reduces the authority of already trusted code. It does not turn a pathname into a general object capability: `0.1.1` uses compiled masks and exact file scopes for one foreground process. See [`SYSCALL_CAPABILITIES_0.1.1.md`](SYSCALL_CAPABILITIES_0.1.1.md).

## Explicit non-goals

Version 0.1.1 does not claim concurrent-process isolation, per-process page directories, ASLR, secure boot, DMA protection, speculative-execution mitigations, authenticated storage, dynamic-linker hardening, transferable capabilities, handle-based delegation, in-process capability revocation or a user-space policy service.

Executable authenticity is limited to the current bundled path-and-SHA-256 trust baseline plus RSA-PSS-signed ZGDB2 threat/revocation policy. The per-application syscall profiles are immutable kernel policy validated against that trusted set; changing them requires a new verified OS build.
