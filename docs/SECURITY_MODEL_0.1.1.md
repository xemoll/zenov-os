# ZenovOS 0.1.1 security model

## Enforced boundaries

- Low linear memory is supervisor-only.
- Ring-3 applications are limited to a 1 MiB segment-relative window based at `0x00400000`.
- Only current image and stack pages are present.
- `CR0.WP` makes kernel writes respect read-only application mappings.
- ELF W+X load segments and page-level permission conflicts are rejected.
- The reused physical user window is zeroed before first use and after every normal exit or recoverable fault.
- Syscall buffers are checked for containment, page presence and write permission when required.
- Every trusted application receives a syscall capability mask from the independently signed ZCAP1 policy; file access additionally requires an exact normalized path scope.
- Capability authority is installed only after final-read ZGDB2, path-plus-SHA-256 appraisal and signature-valid ZCAP1 lookup, and is cleared at both launch boundaries.
- Capability and file-scope denials are committed to the persistent ZGAL1 audit journal; audit failure retains the fail-closed execution lock.
- Independently signed ZMID1 intelligence classifies bounded SHA-256 and byte-pattern rules without granting executable trust or syscall authority.
- Ordinary write, append, copy, rename and package-cache mutations are scanned before persistence; block rules reject the candidate before storage changes, while audit-only rules require a successful persistent record.
- Active ZMID state and quarantine payload/metadata are protected from ordinary mutation. A cryptographically invalid active ZMID stops boot before UI readiness.
- Ring-3 faults terminate the application and return through the common scrub-and-authority-clear path; ring-0 faults panic.
- ZenovFS1 verifies metadata structure and payload checksums and uses a copy-on-write replacement protocol.

The i686 paging mode used by 0.1.1 has no general per-page NX bit. W^X is therefore an admission policy plus code-write protection, not complete hardware non-execution of writable data pages.

The syscall capability layer reduces the authority of already trusted code. It does not turn a pathname into a general object capability: `0.1.1` uses signed masks and exact file scopes for one foreground process. See [`SYSCALL_CAPABILITIES_0.1.1.md`](SYSCALL_CAPABILITIES_0.1.1.md).

## Explicit non-goals

Version 0.1.1 does not claim concurrent-process isolation, per-process page directories, ASLR, secure boot, DMA protection, speculative-execution mitigations, authenticated storage, dynamic-linker hardening, transferable capabilities, handle-based delegation, in-process capability revocation or a user-space policy service.

Executable authenticity is limited to the current bundled path-and-SHA-256 trust baseline plus RSA-PSS-signed ZGDB2 trust/revocation policy. Syscall profiles are held in a separate RSA-PSS-signed ZCAP1 policy constrained to that same path set. Malware hash/pattern rules are held in a third RSA-PSS-signed ZMID1 domain. Each can be updated sequentially without rebuilding the kernel, but root rotation and a stronger offline rollback boundary still require a verified OS build or external monotonic state. ZMID1 is bounded prevention, not cloud antivirus, EDR, archive analysis or machine-learning classification.
