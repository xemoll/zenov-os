# ZenovOS 0.1.1 on-access read-protection contract

## Scope

ZenovOS 0.1.1 applies the active RSA-PSS-verified `ZMID1` rules synchronously when user-visible file bytes are read. The mediator is inside the kernel storage path; it is not a polling daemon and it does not depend on a race-prone notification round trip.

The protected read path covers:

- the shell `cat` command;
- ring-3 `file_read` syscalls after ZCAP1 capability and exact-scope checks;
- other kernel callers that request ordinary user-visible bytes through `security_read_file`.

Executable loading remains on the stricter ZGDB2/ZMID1 execution-appraisal path. Internal policy and recovery reads use the raw storage primitive so policy verification and audit recovery cannot recurse through their own enforcement layer.

## Decision contract

After ZenovFS checksum verification, the complete file bytes are classified with the active ZMID1 database before they are released:

```text
CLEAN       allow
SUSPICIOUS  append durable READ-AUDIT, then allow
INFECTED    append durable READ-BLOCK, scrub the destination buffer, deny
ERROR       deny
```

A suspicious read is not reported as allowed until the ZGAL1 append succeeds. An infected read is not reported as blocked until its ZGAL1 append succeeds. Audit failure leaves ZenovGuard fail-closed.

The denied output buffer is zeroed for the exact number of bytes already read and the returned size is reset to zero. This prevents a caller from using bytes that were loaded before the verdict was known.

## Internal exclusions

The following normalized namespaces are excluded from ordinary on-access classification:

```text
/security
/repo
/var/lib/zenpkg
/quarantine
```

These are not trust exemptions for execution or mutation. They avoid recursive classification while signature databases, signed repository metadata, package state and quarantine evidence are being verified by their dedicated parsers. Existing protected-path and cryptographic checks continue to apply.

Prefix matching is path-boundary aware: `/security` and `/security/...` are excluded, while `/security-copy` is not.

## Persistent evidence

ZGAL1 action numbers added by this contract are:

```text
8  READ-BLOCK
9  READ-AUDIT
```

The host verifier and kernel parser both reject action values above `9`. The runtime-image verifier requires a valid hash chain containing:

- `READ-AUDIT`, suspicious, `/samples/pua-test.bin`;
- `READ-BLOCK`, infected, `/samples/ransomware-test.bin`;
- `READ-BLOCK`, infected, `/samples/malware-v2.bin` after the signed ZMID v1 to v2 update.

## QEMU evidence

The primary lifecycle proves that the PUA fixture remains readable only after persistent audit, the v1 ransomware fixture is blocked before shell display, and the v2-only fixture becomes blocked immediately after signed intelligence activation. The original ransomware fixture remains available to the privileged explicit-quarantine test because a read block does not mutate storage.

The source-contract gate additionally proves that `process.inc` is compiled with `read_file` mapped to `security_read_file`, and that all non-executable destination buffers route through `guarded_read_file`.

## Explicit limitations

This is bounded synchronous read mediation over complete ZenovFS1 files of at most 64 KiB. It is not fs-verity or dm-verity: there is no Merkle tree, block-level authenticated paging or immutable-file enable operation. It is not a ClamAV-compatible daemon, archive parser, YARA interpreter, cloud-reputation client, memory scanner or process-tree EDR.

The current implementation evaluates exact SHA-256 and bounded literal-pattern ZMID1 rules. General Boolean rule conditions, archive recursion and streamed multi-buffer matching remain separate future work.
