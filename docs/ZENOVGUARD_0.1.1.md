# ZenovGuard 0.1.1 security contract

ZenovGuard is the local integrity and malware-prevention layer for ZenovOS 0.1.1. It is intentionally separate from desktop design and from support for additional executable formats.

## Security objective

ZenovGuard protects the existing ZEX1 and static ELF32 execution path. Its default policy is fail-closed:

1. read the requested file through checksum-validating ZenovFS1;
2. calculate SHA-256 in the kernel;
3. compare the digest with known threat signatures;
4. validate the executable container and reject malformed or W+X ELF input;
5. require an exact path-and-SHA-256 match in the immutable bundled trust baseline;
6. record the decision before any user pages are mapped;
7. deny execution unless the verdict is `TRUSTED`.

A valid executable copied to a different path is deliberately `UNTRUSTED`. This prevents a writable persistent volume from silently becoming an executable installation channel.

## Detection

The initial signature database contains:

- the SHA-256 digest of the official harmless EICAR anti-malware test file;
- a ZenovGuard-only safe regression signature used by QEMU CI.

The EICAR payload is not stored in the repository or kernel image. Only its digest is stored. The scanner also returns `SUSPICIOUS` for malformed ZEX1/ELF32 containers and ELF load segments requesting write and execute permission together.

This is not a claim that two signatures provide broad malware coverage. The useful security property in 0.1.1 is the combination of strict appraisal, structural validation, fail-closed execution and auditable quarantine. Signature-database updates and richer content rules are future work.

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

At boot ZenovGuard re-reads every trusted application and verifies its SHA-256 digest. If storage is missing or any digest differs, the OS remains recoverable but application execution stays locked.

## Quarantine

`guard quarantine <path>` accepts only `INFECTED`, `SUSPICIOUS` or `UNTRUSTED` files. The file is atomically renamed into `/data/quarantine/q-<digest-prefix>.qtn` using ZenovFS metadata operations. Quarantined paths cannot execute even if their bytes match a trusted application.

Known infected files are also quarantined automatically when an execution attempt reaches the policy gate.

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
```

The test creates a harmless ZenovGuard signature file at runtime, detects it, quarantines it, verifies quarantine persistence after reboot, copies a trusted executable to an untrusted path and proves that the copy cannot start. Every original bundled application must still pass appraisal and execute.

## Explicit limitations

- No network signature updater exists in 0.1.1.
- The trust baseline is compiled into the kernel; adding an executable requires a reviewed rebuild.
- The audit ring is not persistent or cryptographically chained.
- There is no TPM-backed measured boot or external attestation.
- ZenovFS1 metadata is checksummed and transactionally updated but is not cryptographically authenticated against an offline disk attacker.
- ZenovGuard does not parse archives, documents, scripts, PE, Mach-O or other foreign formats.
- Heuristic and behavioral analysis are not claimed.

These limitations are intentional. The engine provides a small, testable trusted-execution boundary rather than pretending to be a complete commercial endpoint product.
