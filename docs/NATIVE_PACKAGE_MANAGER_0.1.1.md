# ZenovOS 0.1.1 package manager

ZenovOS 0.1.1 contains a bounded transactional manager for native ZEX1 and static ELF32/i386 applications. Container integrity, repository authorization, executable appraisal and syscall authority are separate gates; no successful gate bypasses another.

Foreign intake is deliberately separate from native installation. ZenPkg identifies historical and current Windows, Linux, macOS, Xbox and PlayStation package, executable and media families, but identification does not make their binaries executable on ZenovOS.

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

`pkg formats` prints the support boundary implemented by this release. `pkg probe` reads a bounded file from ZenovFS and reports one of these states:

- `installable` — a ZenPkg container that may continue to normal verification and signed authorization;
- `host-import` — a raw native executable that the host tool may wrap after strict loader validation;
- `inspect-only` — a recognized archive or deployment image with no install transaction or script execution;
- `runtime-required` — the format is recognized, but its ABI, emulator or platform services are absent;
- `partner-only` — the format depends on official platform access, identity, signing, encryption keys or licensed tooling;
- `unsupported` — no accepted signature was found.

Probe is read-only. It never runs setup directives, package scripts, custom actions, firmware payloads or title code. It never decrypts protected content, changes package state or grants execution authority.

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

`zenpkg import-native` accepts only:

- a complete ZEX1 image compatible with the 0.1.1 ring-3 loader;
- a little-endian ELF32/i386 `ET_EXEC` image with no dynamic interpreter or dynamic segment.

ELF import rejects `PT_INTERP`, `PT_DYNAMIC`, W+X load segments, overlapping mappings, conflicting page permissions, invalid entrypoints, unsupported architectures and out-of-range files. ZEX1 import rechecks the complete header, image length, stack/BSS constraints and checksum.

The output is a deterministic ZenPkg container. Import does not add the package to trusted repository metadata. The resulting `.zpk` remains unauthorized until it is included in a signed ZenRepo target set through the repository build process.

## Historical and current format matrix

| Family | Recognized generations and examples | 0.1.1 behaviour |
|---|---|---|
| ZenovOS | ZENPKG1, ZEX1, static ELF32/i386 | Native verification, deterministic host import, signed installation and ring-3 execution |
| Generic ELF | x86_64, MIPS and other foreign ELF classes/ABIs | Classified as `runtime-required`; never passed to native import |
| Debian / RPM | `.deb`, `.rpm` | Inspect-only; maintainer and transaction scripts are never executed |
| Alpine | APK v2 gzip-stream packages and APK v3 ADB packages | Inspect-only; signatures, scripts and filesystem deployment remain external |
| Arch Linux | `.pkg.tar.zst`, `.pkg.tar.xz`, `.pkg.tar.gz`, `.pkg.tar.bz2`, `.pkg.tar.lz4` | Inspect-only; ALPM hooks, signatures and dependency actions are never executed |
| Linux application bundles | AppImage, Snap, Flatpak / Flatpak references | Runtime-required; Linux ABI, dynamic linking, namespaces and service daemons are absent |
| Windows applications | PE/COFF, MSI/MSP/MSM, MSIX/AppX and bundles | Runtime-required; Win32/UWP ABI and Windows Installer services are absent |
| Windows archives and deployment | CAB, MSU, WIM | Inspect-only; setup directives and Windows servicing actions are never executed |
| macOS | Mach-O, universal Mach-O, XAR installer PKG, UDIF DMG | Runtime-required; Darwin ABI, frameworks, filesystems and Apple trust services are absent |
| Original Xbox | XBE | Runtime-required; Xbox kernel, graphics, audio and input services are absent |
| Xbox 360 | XEX2 and STFS `CON`/`LIVE`/`PIRS` content | Runtime-required; Xenon/PowerPC and Xbox services are absent; license metadata is not bypassed |
| Xbox One / Series | XVC | Partner-only; official GDK identity, licenses and package keys are required |
| Xbox / Windows PC | MSIXVC and the newer PC-only MSIXVC2 generation | Partner-only; official GDK packaging and identity remain required |
| PlayStation / PS1 | PS-X EXE and ISO/BIN/CUE media candidates | Runtime-required; MIPS R3000A and console services are absent |
| PlayStation 2 | MIPS ELF and optical media images | Runtime-required; Emotion Engine/IOP and console services are absent |
| PSP / PlayStation Vita | PBP, platform-tagged PKG and ZIP-based VPK | PBP/VPK are runtime-required; protected PKG remains partner-only |
| PlayStation 3 | platform-tagged PKG, SELF and PUP update package | Partner-only; encryption, licenses, firmware validation and signatures are not bypassed |
| PlayStation 4 | `CNT` package, SELF and SCE x86-64 ELF | Protected package/SELF are partner-only; decrypted SCE ELF is runtime-required |
| PlayStation 5 | package/update candidates without a stable public signature contract | Not claimed as signature-verified; only known shared container families are reported |
| Media preservation | ISO 9660 and CHD | Runtime-required; media decoding/mounting and target runtime are absent |
| Generic archives | ZIP | Inspect-only; no executable package contract is inferred |

Filename-only matches such as `.xvc`, `.msixvc`, `.msixvc2`, `.flatpak`, `.dmg`, `.pkg`, `.iso`, `.bin` and `.cue` are reported with `confidence=extension-only`. They are never treated as verified package signatures.

## Persistent state

Installed state is stored in `/data/var/lib/zenpkg/state.v1` using `ZPKDB1`. Repository anti-rollback floors are stored separately in `/data/var/lib/zenpkg/repo.v1` using `ZRST1`.

Each installed record retains an active reference and one previous reference containing the semantic version, immutable entrypoint, payload type, complete package SHA-256 and payload SHA-256. Every reference is re-authorized against the currently verified signed target set during boot, repair, rollback and execution.

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

Audit failure, repository failure, digest mismatch or capability activation failure locks execution closed. Foreign-format classification does not participate in this authority chain and therefore cannot make foreign code executable.

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

## Validation

The 0.1.1 gates cover deterministic package creation, signed root rotation, delegated authorization, repository consistency, expiry, mix-and-match rejection, signature rejection, anti-rollback state, corrupt-state refusal, cache verification, resumable transport, crash recovery, install idempotence, search, planning, upgrade, direct downgrade rejection, repair, explicit rollback, least-privilege capability activation, audited execution, removal and persistence across repeated QEMU boots.

The expanded foreign-intake suite adds:

- 44 direct classifier cases spanning native, Windows, Linux, macOS, Xbox, PlayStation and media generations;
- 37 host probe cases using generated signature fixtures;
- byte-identical repeated import for both ZEX1 and static ELF32/i386;
- six fail-closed import cases: PE, interpreted/dynamic ELF, W+X ELF, x86-64 ELF, PS2/MIPS ELF and checksum-corrupt ZEX1;
- false-positive regressions for bare MZ and Java `CAFEBABE`;
- a QEMU lifecycle proving `pkg formats`, `pkg probe`, signed installation, execution and ZenovFS `fsck`.

Expected evidence markers:

```text
PACKAGE_FOREIGN_FORMAT_TEST_OK cases=44 generations=legacy-current
ZENPKG_FOREIGN_TEST_OK probes=37 native-import=zex1,elf32 deterministic=2 rejection=6 generations=legacy-current
ZENPKG_FOREIGN_QEMU_OK formats=1 probe=zenpkg install=1 run=1 fsck=1
```

## Explicit limits

- System version remains `0.1.1`.
- General network repository transport is disabled.
- ZenovFS1 limits each package and installed payload to 64 KiB.
- Only one foreground userspace process is supported.
- Dynamic linking, x86_64 native execution, a general Linux syscall ABI, Win32/UWP, Darwin/Mach-O execution and console emulation are not implemented.
- Xbox and PlayStation encrypted or signed content remains subject to official platform access, licensing, identities and key material; ZenovOS does not bypass those controls.
- A recognized historical package or game image is not equivalent to a working emulator. Runtime providers remain separate future components.
