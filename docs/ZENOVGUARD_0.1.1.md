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

## Detection

The baseline policy contains:

- the SHA-256 digest of the official harmless EICAR anti-malware test file;
- a ZenovGuard-only safe regression signature used by QEMU CI.

The EICAR payload is not stored in the repository or kernel image. Only its digest is stored. The scanner also returns `SUSPICIOUS` for malformed ZEX1/ELF32 containers and ELF load segments requesting write and execute permission together.

This is not a claim that two signatures provide broad malware coverage. The useful security property in 0.1.1 is the combination of signed policy, strict appraisal, structural validation, fail-closed execution, revocation, persistent audit and quarantine.

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

The trusted application paths, active ZGDB, policy-version state and persistent audit journal are protected from ordinary shell and userspace mutation.

## Quarantine

`guard quarantine <path>` accepts only `INFECTED`, `SUSPICIOUS` or `UNTRUSTED` files. The file is atomically renamed into `/data/quarantine/q-<digest-prefix>.qtn` using ZenovFS metadata operations. Quarantined paths cannot execute even if their bytes match a trusted application.

Known infected files are also quarantined automatically when an execution attempt reaches either the signed ZGDB policy gate or the ordinary ZenovGuard appraisal gate. Scan and quarantine decisions are persisted separately.

## Persistent audit

ZenovGuard stores a bounded 64-record ZGAL1 journal at `/security/zenovguard.audit`. Each 128-byte record contains a monotonic sequence, PIT tick, action, verdict, canonical path, complete object SHA-256 and a record hash that commits to the previous hash.

The journal is exactly 8,288 bytes and is replaced through one ZenovFS copy-on-write transaction per event. It is fully replayed at boot. A malformed header, sequence discontinuity, canonicalization error or hash-chain mismatch prevents normal boot.

BOOT, SCAN, EXEC and QUARANTINE decisions require a successful persistent commit. If append fails, ZenovGuard restores the previous in-memory slot, marks the journal unavailable and locks subsequent execution rather than continuing without audit evidence.

A volatile 32-record mirror remains for quick inspection of the current session. `guard log` prints the persistent retained window followed by this mirror.

See [`AUDIT_JOURNAL_0.1.1.md`](AUDIT_JOURNAL_0.1.1.md) for the binary format, ring anchor, crash ordering, independent verifier and offline-attacker boundary.

## Commands

```text
guard status
guard database
guard update <signed-zgdb-path>
guard selftest
guard scan <path>
guard scan all
guard quarantine <path>
guard log
guard log verify
```

`antivirus` is an alias for `guard`.

## CI evidence

The QEMU contract must prove:

```text
ZENOV_GUARD_AUDIT_SELFTEST_OK
ZENOV_GUARD_AUDIT_REPLAY_OK
ZENOV_GUARD_AUDIT_READY
ZENOV_GUARD_AUDIT_VERIFY_OK
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
```

The first phase writes a non-empty persistent journal. The second phase must replay that journal after reboot and continue the chain. The recovery image independently verifies its factory journal while exercising interrupted ZenovFS transaction recovery.

The host verifier reads the runtime journal directly from ZenovFS. It then changes journal payload and recomputes the ZenovFS FNV checksum; the filesystem checksum passes, while the SHA-256 audit-chain verifier must reject the image.

Host CI also independently verifies positive ZGDB fixtures with OpenSSL PSS parameters and requires both negative fixtures to fail. Deterministic rebuild checks include every policy fixture and the canonical empty ZGAL1 seed.

## Explicit limitations

- No network signature updater exists in 0.1.1.
- The private signing key is not committed; an operational offline key-custody and signing process is required for future public updates.
- The kernel compiled policy floor is `3`; without TPM/NVRAM an offline attacker replacing the whole data image can roll policy and state back to that floor.
- This build has one active root key, not threshold signing.
- In-band root metadata rotation is not implemented; changing the root requires a verified OS build.
- ZGAL1 is hash-chained but not authenticated by a secret outside writable storage. A complete offline image rewrite can construct a new internally consistent chain.
- The bounded journal retains 64 records; rotated-out record contents are not recoverable without external archival.
- There is no TPM-backed measured boot, sealed audit head or remote witness.
- ZenovFS1 metadata is checksummed and transactionally updated but is not cryptographically authenticated against an offline disk attacker.
- ZenovGuard does not parse archives, documents, scripts, PE, Mach-O or other foreign formats.
- Heuristic and behavioral analysis are not claimed.

These limitations are intentional. The engine provides a small, testable trusted-execution boundary rather than pretending to be a complete commercial endpoint product.
