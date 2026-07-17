# ZenovGuard 0.1.1 security contract

ZenovGuard is the local integrity and malware-prevention layer for ZenovOS 0.1.1. It is intentionally separate from desktop design and from support for additional executable formats.

## Security objective

ZenovGuard protects the existing ZEX1 and static ELF32 execution path. Its default policy is fail-closed:

1. read the requested file through checksum-validating ZenovFS1;
2. calculate SHA-256 in the kernel;
3. apply the active signed ZGDB threat and revocation policy;
4. validate the executable container and reject malformed or W+X ELF input;
5. require an exact path-and-SHA-256 match in the immutable bundled trust baseline;
6. record the decision before any user pages are mapped;
7. deny execution unless the verdict is `TRUSTED` and the digest is not revoked.

A valid executable copied to a different path is deliberately `UNTRUSTED`. This prevents a writable persistent volume from silently becoming an executable installation channel.

## Detection

The initial policy contains:

- the SHA-256 digest of the official harmless EICAR anti-malware test file;
- a ZenovGuard-only safe regression signature used by QEMU CI.

The EICAR payload is not stored in the repository or kernel image. Only its digest is stored. The scanner also returns `SUSPICIOUS` for malformed ZEX1/ELF32 containers and ELF load segments requesting write and execute permission together.

This is not a claim that two signatures provide broad malware coverage. The useful security property in 0.1.1 is the combination of signed policy, strict appraisal, structural validation, fail-closed execution, revocation and auditable quarantine.

## Signed ZGDB policy

Threat and revocation records are loaded from `/security/zenovguard.zgdb`. The database has a deterministic binary representation, payload SHA-256 and an RSA-2048 PKCS#1 v1.5 SHA-256 signature verified by the kernel.

Only the public verification root is stored in the repository. The database must contain each of the seven compiled trusted records exactly once; a signed database cannot add a new trusted executable path. See [`ZGDB_0.1.1.md`](ZGDB_0.1.1.md) for the complete format, update order and anti-rollback limits.

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

At boot ZenovGuard re-reads every trusted application and verifies its SHA-256 digest. It also verifies the active signed ZGDB. If storage, the trust baseline or policy signature is unavailable, application execution remains locked.

The trusted application paths, active ZGDB and persistent policy-version state are protected from ordinary shell and userspace mutation.

## Quarantine

`guard quarantine <path>` accepts only `INFECTED`, `SUSPICIOUS` or `UNTRUSTED` files. The file is atomically renamed into `/data/quarantine/q-<digest-prefix>.qtn` using ZenovFS metadata operations. Quarantined paths cannot execute even if their bytes match a trusted application.

Known infected files are also quarantined automatically when an execution attempt reaches either the signed ZGDB policy gate or the ordinary ZenovGuard appraisal gate.

## Audit log

A fixed 32-record in-kernel ring stores:

- PIT tick;
- action: boot, scan, execute or quarantine;
- verdict;
- normalized path;
- SHA-256 prefix.

The ring is bounded so an attacker cannot exhaust kernel memory with audit events. It is volatile in 0.1.1; persistent tamper-evident logging requires a separately designed append-only store.

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
```

`antivirus` is an alias for `guard`.

## CI evidence

The QEMU contract must prove:

```text
ZENOV_GUARD_SELFTEST_OK
ZENOV_GUARD_TRUST_BASELINE_OK
ZENOV_GUARD_READY
ZENOV_GUARD_STATUS_OK
ZENOV_GUARD_DETECTED
ZENOV_GUARD_QUARANTINE_OK
ZENOV_GUARD_UNTRUSTED_BLOCKED
ZENOV_GUARD_FULL_SCAN_OK
ZENOV_GUARD_EXEC_ALLOWED
ZGDB_SIGNATURE_OK
ZGDB_TAMPER_REJECTED
ZGDB_ATOMIC_UPDATE_OK version=2
ZGDB_ROLLBACK_REJECTED
ZGDB_REVOCATION_BLOCKED
```

The test detects and quarantines a harmless regression file, proves quarantine persistence, blocks an untrusted application copy, runs every original bundled application under policy version 1, rejects a tampered update, activates signed policy version 2, blocks the revoked application, rejects rollback to version 1 and confirms revocation after reboot.

## Explicit limitations

- No network signature updater exists in 0.1.1.
- The private signing key is not committed; an operational offline key-custody and signing process is still required for future public updates.
- The kernel compiled policy floor is `1`; without TPM/NVRAM an offline attacker replacing the whole data image can roll policy and state back to that floor.
- The audit ring is not persistent or cryptographically chained.
- There is no TPM-backed measured boot or external attestation.
- ZenovFS1 metadata is checksummed and transactionally updated but is not cryptographically authenticated against an offline disk attacker.
- ZenovGuard does not parse archives, documents, scripts, PE, Mach-O or other foreign formats.
- Heuristic and behavioral analysis are not claimed.

These limitations are intentional. The engine provides a small, testable trusted-execution boundary rather than pretending to be a complete commercial endpoint product.
