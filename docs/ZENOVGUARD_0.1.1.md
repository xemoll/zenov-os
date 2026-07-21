# ZenovGuard 0.1.1 security contract

ZenovGuard is the local integrity and malware-prevention layer for ZenovOS 0.1.1. It is intentionally separate from desktop design and from support for additional executable formats.

## Security objective

ZenovGuard protects the existing ZEX1 and static ELF32 execution path. Its default policy is fail-closed:

1. read the requested file through checksum-validating ZenovFS1;
2. calculate SHA-256 in the kernel;
3. apply the active ZGDB2 signed threat and revocation policy;
4. validate the executable container and reject malformed or W+X ELF input;
5. require an exact path-and-SHA-256 match in the immutable bundled trust baseline;
6. commit the decision to the persistent audit journal before user pages are mapped;
7. deny execution unless the verdict is `TRUSTED`, the digest is not revoked and the audit commit succeeds.

A valid executable copied to a different path is deliberately `UNTRUSTED`. This prevents a writable persistent volume from silently becoming an executable installation channel.

## Detection and prevention

ZenovGuard has two independent signed inputs. ZGDB2 controls executable trust, threat digests and revocation. ZMID1 controls bounded malware intelligence for scans and persistent mutations. Keeping them separate prevents an antimalware-rule update from minting executable trust or syscall authority.

ZMID1 version 1 contains two exact SHA-256 records and three bounded byte-pattern records. Version 2 adds one pattern. The fixtures include the official harmless EICAR digest without embedding its payload and Zenov-only safe regression markers for block/quarantine and audit-only behavior. They verify the engine and update path; they are not described as broad malware coverage.

Ordinary write, append, copy, rename, package-payload and package-cache operations are scanned before durable mutation. Append reconstructs and scans the complete proposed final file in a bounded 64 KiB buffer, preventing a pattern from being split across multiple operations. Audit-only rules allow the mutation only after a persistent security record succeeds. Block rules reject it before storage changes.

The implementation supports exact SHA-256 and literal byte patterns up to 32 bytes, with at most 32 records. It intentionally excludes unbounded regular expressions, archive recursion, emulation and statistical classification from the kernel. See [`ANTIMALWARE_0.1.1.md`](ANTIMALWARE_0.1.1.md).

## ZGDB2 signed policy

Threat and revocation records are loaded from `/security/zenovguard.zgdb`. Schema 2 has a deterministic binary representation, payload SHA-256, an explicit root key ID and an RSA-2048 PSS signature using SHA-256, MGF1-SHA-256 and a fixed 32-byte salt.

The active verification root has key ID:

```text
6f788074c018f5aa
```

The previous PKCS#1 v1.5 root was removed from kernel trust. This build accepts schema 2 from compiled policy floor `3`; policies 1 and 2 are not valid under the rotated root.

Only the public verification root is stored in the repository. The database must contain each of the seven compiled trusted records exactly once; a signed database cannot add a new trusted executable path. Unknown key IDs are rejected before signature verification.

Interactive updates must be exactly sequential (`N → N+1`). See [`ZGDB_0.1.1.md`](ZGDB_0.1.1.md) for the complete format, RSA-PSS verification, update order and anti-rollback boundary.

## Trust baseline

Seven bundled applications are pinned by normalized ZenovFS path and SHA-256 digest:

```text
/apps/hello.zex
/apps/fileio.elf
/apps/args.elf
/apps/console.elf
/apps/protect.elf
/apps/kaccess.elf
/apps/zenovapp.zex
```

At boot ZenovGuard re-reads every trusted application and verifies its SHA-256 digest. It then validates the active ZGDB2 key ID, payload hash, RSA-PSS signature, policy version and exact trusted-record set. If storage, the trust baseline, persistent audit or signed policy is unavailable, application execution remains locked.

The trusted application paths, active ZGDB2, ZCAP1 and ZMID1 objects, their version state, persistent audit journal and quarantine contents are protected from ordinary shell and userspace mutation.

## Quarantine

`guard quarantine <path>` accepts only a non-clean classification. The file is atomically renamed into `/data/quarantine/q-<digest-prefix>.qtn`, and a protected `/data/quarantine/q-<digest-prefix>.qtn.meta` sidecar is written. The sidecar begins with `ZQMD1` and records the original normalized path, verdict and matched intelligence rule.

Quarantine payload and metadata cannot be modified through ordinary write, append, remove, copy or rename operations. `guard quarantine list` reports both objects, while `guard quarantine delete <quarantine-path>` uses a privileged deletion path and removes the matching sidecar. Quarantined paths cannot execute even if their bytes match a trusted application.

Existing infected or suspicious objects are quarantined explicitly. New malicious content is prevented before write rather than written and moved afterward. This distinction avoids claiming rollback of a write that never became durable.

## Persistent audit

ZenovGuard stores a bounded 64-record ZGAL1 journal at `/security/zenovguard.audit`. Each 128-byte record contains a monotonic sequence, PIT tick, action, verdict, canonical path, complete object SHA-256 and a record hash that commits to the previous hash.

The journal is exactly 8,288 bytes and is replaced through one ZenovFS copy-on-write transaction per event. It is fully replayed at boot. A malformed header, sequence discontinuity, canonicalization error or hash-chain mismatch prevents normal boot.

BOOT, SCAN, EXEC, QUARANTINE, WRITE-BLOCK and INTELLIGENCE-UPDATE decisions require a successful persistent commit. If append fails, ZenovGuard restores the previous in-memory slot, marks the journal unavailable and locks subsequent execution rather than continuing without audit evidence.

A volatile 32-record mirror remains for quick inspection of the current session. `guard log` prints the persistent retained window followed by this mirror.

The audit COW path is verified with 1,662 deterministic sector/metadata fault cases across empty-journal append and full-ring rotation. Ordered crash prefixes must recover the exact old or exact new journal. Torn, corrupted, missing or reordered writes may also produce an invalid state, but that state must be rejected fail-closed; no third valid journal is accepted.

The host verifier and fault generator use a shared canonical ZGAL1 module, both run the SHA-256 `abc` known-answer test, and the generator requires a fixed record-hash vector before producing images.

Three generated ZenovFS images are booted by QEMU: old-state recovery, new-state recovery and a committed replacement with a missing payload sector. The last image must stop before UI readiness on `ZENOV_GUARD_AUDIT_INVALID`.

See [`AUDIT_JOURNAL_0.1.1.md`](AUDIT_JOURNAL_0.1.1.md) for the binary format, ring anchor, complete fault matrix, crash images, independent verifier and offline-attacker boundary.

## Commands

```text
guard status
guard database
guard update <signed-zgdb-path>
guard intelligence
guard intelligence-update <signed-zmid-path>
guard selftest
guard scan <path>
guard scan all
guard quarantine <path>
guard quarantine list
guard quarantine delete <quarantine-path>
guard log
guard log verify
```

`antivirus` is an alias for `guard`.

## CI evidence

The CI and QEMU contract must prove:

```text
ZENOV_AUDIT_COW_FAULT_MATRIX_OK
ZENOV_AUDIT_COW_FAULT_INJECTION_OK total_cases=1662
ZENOV_AUDIT_COW_OLD_OR_NEW_OR_FAIL_CLOSED_ONLY
ZENOV_GUARD_AUDIT_SELFTEST_OK
ZENOV_GUARD_AUDIT_REPLAY_OK count=0 next=1
ZENOV_GUARD_AUDIT_REPLAY_OK count=1 next=2
ZENOV_GUARD_AUDIT_READY
ZENOV_GUARD_AUDIT_VERIFY_OK
ZENOV_GUARD_AUDIT_INVALID
ZENOV_GUARD_SELFTEST_OK
ZENOV_GUARD_TRUST_BASELINE_OK
ZENOV_GUARD_READY
ZENOV_GUARD_STATUS_OK
ZENOV_GUARD_DETECTED
ZENOV_GUARD_QUARANTINE_OK
ZENOV_GUARD_UNTRUSTED_BLOCKED
ZENOV_GUARD_FULL_SCAN_OK
ZENOV_GUARD_EXEC_ALLOWED
ZGDB_ROOT_KEY_OK id=6f788074c018f5aa
ZGDB_PSS_SIGNATURE_OK
ZGDB_POLICY_VERSION_OK version=3
ZGDB_KEY_REJECTED reason=unknown-key
ZGDB_TAMPER_REJECTED
ZGDB_ATOMIC_UPDATE_OK version=4
ZGDB_ROLLBACK_REJECTED
ZGDB_REVOCATION_BLOCKED
ZGDB_POLICY_VERSION_OK version=4
ZMID_ROOT_KEY_OK id=6ca6a5275544c533
ZMID_PSS_SIGNATURE_OK
ZMID_DATABASE_VERSION_OK version=1
ZMID_KEY_REJECTED reason=unknown-key
ZMID_TAMPER_REJECTED reason=payload-digest
ZMID_ATOMIC_UPDATE_OK version=2
ZMID_ROLLBACK_REJECTED reason=rollback
ZMID_DATABASE_VERSION_OK version=2
ZENOV_GUARD_WRITE_BLOCKED
ZENOV_GUARD_WRITE_AUDIT
ZENOV_ANTIMALWARE_RUNTIME_IMAGE_OK
ZENOV_ANTIMALWARE_GATE_OK
```

The normal runtime phase writes a non-empty persistent journal and exercises signed intelligence, pre-write blocking, audit-only allow, cross-append prevention, protected quarantine, exact v1-to-v2 update and rollback rejection. The second phase must replay that journal after reboot and continue the chain. A generic recovery image verifies interrupted ZenovFS recovery. Three additional phases prove audit pre-commit recovery, post-commit recovery and invalid-journal fail-closed boot.

The host verifier reads the runtime journal directly from ZenovFS. It then changes journal payload and recomputes the ZenovFS FNV checksum; the filesystem checksum passes, while the SHA-256 audit-chain verifier must reject the image.

Host CI also independently verifies positive ZGDB fixtures with OpenSSL PSS parameters and requires both negative fixtures to fail. Deterministic rebuild checks include every policy fixture and the canonical empty ZGAL1 seed.

## Explicit limitations

- No network intelligence updater, cloud reputation or automatic sample submission exists in 0.1.1.
- The private signing key is not committed; an operational offline key-custody and signing process is required for future public updates.
- The kernel compiled policy floor is `3`; without TPM/NVRAM an offline attacker replacing the whole data image can roll policy and state back to that floor.
- This build has one active root key, not threshold signing.
- In-band root metadata rotation is not implemented; changing the root requires a verified OS build.
- ZGAL1 is hash-chained but not authenticated by a secret outside writable storage. A complete offline image rewrite can construct a new internally consistent chain.
- The bounded journal retains 64 records; rotated-out record contents are not recoverable without external archival.
- The deterministic fault matrix models ZenovFS writes, not undocumented physical-drive cache or firmware behavior.
- There is no TPM-backed measured boot, sealed audit head or remote witness.
- ZenovFS1 metadata is checksummed and transactionally updated but is not cryptographically authenticated against an offline disk attacker.
- ZenovGuard does not parse archives, documents, scripts, PE, Mach-O or other foreign formats.
- Statistical, machine-learning and general behavioral classification are not claimed.
- There is no sandbox detonation, process-memory scanning, URL reputation or network traffic inspection.

These limitations are intentional. The engine provides a small, testable trusted-execution boundary rather than pretending to be a complete commercial endpoint product.
