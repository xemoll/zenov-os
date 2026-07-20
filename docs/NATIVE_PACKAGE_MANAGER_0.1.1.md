# ZenovOS 0.1.1 native package manager

ZenovOS 0.1.1 contains a bounded transactional manager for native ZEX1 and static ELF32/i386 applications. Container integrity, repository authorization, executable appraisal and syscall authority are separate gates; no successful gate bypasses another.

## Commands

```text
pkg status
pkg list
pkg search <query>
pkg plan [name|all]
pkg verify <package.zpk>
pkg install <name|package.zpk>
pkg upgrade <name|all>
pkg repair <name|all>
pkg policy <name>
pkg info <name>
pkg rollback <name>
pkg remove <name>
pkg run <name> [arguments]
pkg repo status
pkg repo targets
pkg repo search <query>
pkg repo check
pkg repo refresh
```

`pkg verify` validates `ZENPKG1` and reports whether the exact package is present in the signed delegated target set. Verification never installs code or grants execution authority. `pkg plan` is read-only. Installation by package name resolves the newest signed target available in the offline repository.

## Persistent state

Installed state is stored in `/data/var/lib/zenpkg/state.v1` using `ZPKDB1`. Repository anti-rollback floors are stored separately in `/data/var/lib/zenpkg/repo.v1` using `ZRST1`.

Each installed record retains an active reference and one previous reference containing the semantic version, immutable entrypoint, payload type, complete package SHA-256 and payload SHA-256. The database checksum detects accidental corruption but is not a trust root: every reference is re-authorized against the currently verified signed target set during boot, repair, rollback and execution.

## Installation transaction

1. Read the package into a bounded 64 KiB buffer.
2. Verify the complete container, canonical manifest and all SHA-256 fields.
3. Resolve target, capability, exact dependency and conflict requirements.
4. Match signed delegated target metadata, including package length and both digests.
5. Appraise the executable payload through ZenovGuard and signed ZGDB policy.
6. Write the payload to an immutable versioned path.
7. Re-read filesystem metadata and validate size and checksum.
8. Build the next ZPKDB1 generation in memory.
9. Atomically commit the active reference through ZenovFS1 metadata replacement.
10. Remove an obsolete retained payload only after the database commit is durable.

A failed database commit leaves the prior active version selected. A payload written before that failure remains inactive and cannot pass final execution authorization.

## Search, planning, upgrade, repair and rollback

Search and policy inspection operate only on the verified signed target set. `pkg plan` reports install, upgrade, current, unavailable or repository-behind state without writing storage. `pkg upgrade` resolves the newest signed target and uses the normal per-package transaction.

`pkg repair` revalidates installed payloads. It reports a healthy active version, rolls back to a still-authorized retained version, or remains fail-closed when no trusted payload is available. Direct installation of an older version is rejected. Explicit rollback is limited to the retained previous reference and requires both digest validation and current signed authorization.

## Final execution boundary

```text
ZenovFS checksum-valid final read
  -> signed ZGDB threat/revocation precheck
  -> ZEX1/ELF32 structural and W^X validation
  -> active ZPKDB1 path and digest match
  -> current signed delegated target match
  -> persistent ZenovGuard audit append
  -> signed least-privilege syscall profile activation
  -> ring-3 loader
```

Audit failure, repository failure, digest mismatch or capability activation failure locks execution closed. The example package receives only `console-write` (`0x00000001`) and no file, synchronization, clock, version or console-input authority.

## Protected paths

Shell and userspace mutation is denied for:

```text
/data/apps/pkg-*
/data/repo/*
/data/var/lib/zenpkg/state.v1
/data/var/lib/zenpkg/repo.v1
```

The package manager has narrow internal entrypoints for versioned payloads and state. Signed repository metadata is image-seeded and immutable at runtime.

## Validation

The 0.1.1 gates cover deterministic package creation, signed root rotation, delegated authorization, repository consistency, expiry, mix-and-match rejection, signature rejection, anti-rollback state, corrupt-state refusal, install idempotence, search, planning, upgrade, direct downgrade rejection, repair, explicit rollback, least-privilege capability activation, audited execution, removal and persistence across three QEMU boots.

## Explicit limits

- System version remains `0.1.1`.
- Network repository transport is disabled.
- ZenovFS1 limits each package and installed payload to 64 KiB.
- The kernel has no trusted RTC or hardware monotonic counter; expiry uses a compiled trust floor and cannot fully prevent a long-term freeze after replacement of the entire disk image.
- Only one foreground userspace process is supported.
- Dynamic linking, x86_64, Win32/PE, Mach-O and console emulation are not implemented.
