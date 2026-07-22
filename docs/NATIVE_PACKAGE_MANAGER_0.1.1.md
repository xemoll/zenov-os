# ZenovOS 0.1.1 package manager

ZenovOS 0.1.1 contains a bounded transactional manager for native ZEX1 and static ELF32/i386 applications. Container integrity, repository authorization, executable appraisal and syscall authority are separate gates; no successful gate bypasses another.

Foreign package intake is deliberately separate from native installation. ZenPkg can identify several Windows, Linux, macOS, Xbox and PlayStation package families, but identification does not make their binaries executable on ZenovOS.

## Shell commands

```text
pkg status
pkg formats
pkg probe <file>
pkg list
pkg search <query>
pkg plan [name|all]
pkg verify <package.zpk>
pkg fetch <name>
pkg install <name|package.zpk>
pkg upgrade <name|all>
pkg repair <name|all>
pkg policy <name>
pkg info <name>
pkg rollback <name>
pkg remove <name>
pkg run <name> [arguments]
pkg cache status
pkg cache verify
pkg cache clean
pkg transport status
pkg transport step <name>
pkg transport resume <name>
pkg transport cancel
pkg repo status
pkg repo targets
pkg repo search <query>
pkg repo check
pkg repo refresh
```

`pkg formats` prints the support boundary implemented by this release. `pkg probe` reads a bounded file from ZenovFS, classifies its signature and reports one of these states:

- `installable` — a ZenPkg container that can continue to normal verification and signed authorization;
- `host-import` — a raw native executable that the host tool may wrap after strict loader validation;
- `inspect-only` — a recognized archive with no install transaction or script execution;
- `runtime-required` — the container is known, but its operating-system ABI is absent;
- `partner-only` — the format depends on an official platform programme, identity, keys or SDK;
- `unsupported` — no accepted signature was found.

Probe is read-only. It never extracts an archive, runs an installer script, decrypts content, changes package state or grants execution authority.

`pkg verify` validates `ZENPKG1` and reports whether the exact package is present in the signed delegated target set. Verification never installs code or grants execution authority. `pkg plan` is read-only. Installation by package name resolves the newest signed target available in the offline repository.

## Host intake commands

```text
zenpkg probe FILE
zenpkg import-native FILE \
  --name NAME \
  --version VERSION \
  --license TEXT \
  --source TEXT \
  --asset-policy redistributable \
  --output FILE.zpk
```

`zenpkg probe` reports the detected family, support state, signature confidence, byte count and SHA-256. Extension-only matches are explicitly marked and are not treated as validated containers.

`zenpkg import-native` accepts only a complete ZEX1 image or a static little-endian ELF32/i386 `ET_EXEC` image compatible with the 0.1.1 loader. ELF imports reject `PT_INTERP`, `PT_DYNAMIC`, W+X load segments, overlapping mappings, invalid entrypoints and out-of-range files. ZEX1 imports recheck the complete header, image length and checksum. The resulting package must fit the current 64 KiB package limit and must declare redistributable assets.

Import creates a deterministic ZenPkg container. It does not add the package to trusted repository metadata. A package remains unauthorized until it is added to the signed ZenRepo target set through the normal repository build process.

## Foreign package matrix

| Family | Recognized examples | 0.1.1 behaviour |
|---|---|---|
| ZenovOS | `.zpk`, ZEX1, static ELF32/i386 | Native verification, host import and signed installation |
| Linux | DEB, RPM, AppImage, Snap, Flatpak | DEB/RPM/ZIP inspection; AppImage/Snap/Flatpak require a future Linux runtime |
| Windows | PE/COFF, MSI, MSIX/AppX | Detection only; Win32/UWP loader and installer services are absent |
| macOS | XAR `.pkg`, UDIF `.dmg`, Mach-O | Detection only; Darwin ABI, Apple frameworks, filesystems and trust services are absent |
| Xbox | XVC, MSIXVC | Partner-only classification; no decryption, identity bypass or console runtime |
| PlayStation | known package signatures | Partner-only classification; no decryption, signing bypass or console runtime |

DEB/RPM maintainer scripts, MSI custom actions, macOS installer scripts and package-provided executable hooks are never run by the probe path.

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

## Search, cache, transport, repair and rollback

Search and policy inspection operate only on the verified signed target set. `pkg plan` reports install, upgrade, current, unavailable or repository-behind state without writing storage. `pkg upgrade` resolves the newest signed target and uses the normal per-package transaction.

The package cache stores verified repository objects under package-manager-controlled paths. Fetch uses a persistent journal and resumable chunk commits. The current transport source is the image-seeded offline repository; general network mirrors and TLS download are not enabled.

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
/data/var/cache/zp/*
/data/var/lib/zenpkg/state.v1
/data/var/lib/zenpkg/repo.v1
/data/var/lib/zenpkg/transport.v1
```

The package manager has narrow internal entrypoints for versioned payloads, cache objects and state. Signed repository metadata is image-seeded and immutable at runtime.

## Validation

The 0.1.1 gates cover deterministic package creation, signed root rotation, delegated authorization, repository consistency, expiry, mix-and-match rejection, signature rejection, anti-rollback state, corrupt-state refusal, cache verification, resumable transport, crash recovery, install idempotence, search, planning, upgrade, direct downgrade rejection, repair, explicit rollback, least-privilege capability activation, audited execution, removal and persistence across repeated QEMU boots.

Foreign intake adds:

- 16 direct classifier cases;
- 13 host probe cases across native, Linux, Windows, macOS, Xbox and PlayStation families;
- byte-identical repeated native import;
- rejection of PE native import, dynamic/interpreted ELF and checksum-corrupt ZEX1;
- a QEMU boot proving `pkg formats`, `pkg probe`, signed installation, execution and ZenovFS `fsck` in one guest lifecycle.

## Explicit limits

- System version remains `0.1.1`.
- General network repository transport is disabled.
- ZenovFS1 limits each package and installed payload to 64 KiB.
- The kernel has no trusted RTC or hardware monotonic counter; expiry uses a compiled trust floor and cannot fully prevent a long-term freeze after replacement of the entire disk image.
- Only one foreground userspace process is supported.
- Dynamic linking, x86_64, a general Linux syscall ABI, Win32/UWP, Darwin/Mach-O execution and console emulation are not implemented.
- Xbox and PlayStation content remains subject to official platform access, licensing, identities and key material; ZenovOS does not bypass those controls.
