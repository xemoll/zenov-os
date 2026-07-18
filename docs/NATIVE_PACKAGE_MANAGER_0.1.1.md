# ZenovOS 0.1.1 native package manager

ZenovOS 0.1.1 contains a bounded, transactional package manager for native ZEX1 and static ELF32/i386 applications. It is intentionally narrow: the implementation strengthens the existing trusted-execution boundary instead of claiming Windows, macOS or console compatibility that the current kernel cannot provide.

## Commands

```text
pkg status
pkg list
pkg verify <package.zpk>
pkg install <package.zpk>
pkg info <name>
pkg rollback <name>
pkg remove <name>
pkg run <name> [arguments]
```

`pkg verify` reports structural integrity and whether the package is authorized by the 0.1.1 bootstrap catalog. Verification alone never grants execution trust.

## Persistent state

Installed state is stored at:

```text
/data/var/lib/zenpkg/state.v1
```

The `ZPKDB1` database is fixed-size and FNV-checksum-protected for accidental-corruption detection; its checksum is not a trust primitive. It stores eight package records. Every record has an active reference and one retained previous reference, each containing:

- version;
- immutable entrypoint;
- payload type;
- complete authorized package SHA-256;
- installed payload SHA-256.

The shell and userspace cannot mutate the database or `/data/apps/pkg-*` directly. Only the package-manager internal storage entrypoints can write or remove those paths.

## Installation transaction

Installation is ordered as follows:

1. Read the package into a bounded 64 KiB buffer.
2. Verify the complete ZENPKG1 structure and all SHA-256 fields.
3. Resolve exact target, capabilities, dependencies and conflicts.
4. Require a compiled bootstrap-catalog authorization match over semantic identity, full package SHA-256 and payload SHA-256.
5. Scan the payload through ZenovGuard structural and threat checks.
6. Write the payload to an immutable versioned path.
7. Re-read and verify filesystem size/checksum metadata.
8. Build the next ZPKDB1 generation in memory.
9. Commit the database using ZenovFS1 copy-on-write replacement.
10. Remove an obsolete third version only after the new database is committed.

A failure before step 9 leaves the previous active reference unchanged. A payload written before a failed database commit remains inactive and cannot pass execution authorization. The next successful transaction may collect it.

## Upgrade and rollback

A newer authorized version moves the old active reference into the previous slot. Direct downgrade installation is rejected. `pkg rollback` is the only supported downgrade operation and only switches to the retained previous reference after re-reading its payload and validating the recorded SHA-256.

This distinguishes an intentional local rollback from replaying an older package at the installation interface.

## Execution trust

Installed package execution does not bypass ZenovGuard. The final bytes consumed by the loader must satisfy all of these conditions:

- signed ZGDB policy is ready;
- the digest is not a threat or revocation record;
- the executable is structurally valid and does not violate W+X policy;
- normalized path equals the active ZPKDB1 entrypoint;
- active database reference is still present in the compiled catalog;
- full package and payload digests in that reference match the catalog;
- final-read payload SHA-256 equals the active reference;
- persistent ZenovGuard audit append succeeds.

Audit failure locks execution closed.

## Bootstrap catalog

The initial catalog contains two authorized versions of `hello-native`. A third structurally valid package is seeded as a negative fixture and must fail installation. The catalog pins both deterministic `.zpk` bytes and executable bytes. Every active and retained previous DB reference is re-authorized during boot, rollback and execution, so an offline attacker cannot turn a recomputed FNV database checksum into new executable trust.

This is a secure bootstrap, not a general public repository. It prevents arbitrary writable-storage code from becoming trusted while repository signing infrastructure is absent.

## Validation

Host regression covers:

- SHA-256 known-answer behavior;
- strict canonical package parsing;
- dependency rejection;
- same-version idempotence and unauthorized conflicting-package rejection;
- idempotent installation;
- upgrade and direct-downgrade rejection;
- explicit rollback;
- database recovery after runtime reset;
- safe orphan behavior on database-write failure;
- payload tamper rejection;
- removal persistence;
- corrupt database refusal;
- syntactically valid, FNV-recomputed database forgery rejection by catalog re-authorization.

The independent QEMU test performs three boots and verifies catalog authorization, unauthorized-package rejection, install, idempotence, upgrade, downgrade blocking, rollback, audited execution, persistence, removal and post-removal persistence.

## Explicit limitations

- System version remains `0.1.1`.
- Network repositories are disabled.
- There is no publisher signature or public transparency log in the package format.
- The bootstrap catalog is compiled into the kernel.
- Offline replacement of the whole data image can roll back ZPKDB1 state; hardware monotonic state is not available.
- Each package and payload is limited to 64 KiB by ZenovFS1.
- Only one foreground userspace process is supported.
- Dynamic linking, x86_64, Win32/PE, Mach-O and console emulation are not implemented.
