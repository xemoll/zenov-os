# Package and compatibility architecture

ZenovOS 0.1.1 separates package transport, installation state, executable authority, foreign-format intake and compatibility runtimes. A package manager can safely identify or inspect software from another platform without claiming that the kernel can execute it.

## Implemented native path

```text
signed offline ZenRepo target
  -> strict deterministic ZenPkg verifier
  -> exact package and payload digests
  -> immutable ZenovFS payload
  -> atomic ZPKDB1 active-reference commit
  -> ZGDB and ZenovGuard final-read policy
  -> signed least-privilege syscall profile
  -> native ring-3 loader
```

The native path supports bounded ZEX1 and static ELF32/i386 applications, deterministic package creation, search, planning, verified cache fetch, installation by name or file, upgrade, repair, explicit rollback, removal and persistent state.

## Intake is not execution

ZenPkg uses one shared base classifier plus a generation policy in the host tool and the in-system package manager. Each result carries an explicit state:

```text
installable | host-import | inspect-only | runtime-required | partner-only | unsupported
```

- `installable` and `host-import` are limited to the native ZenovOS chain.
- `inspect-only` permits identification and metadata work but never executes scripts or payloads.
- `runtime-required` identifies a usable historical or current format whose ABI/emulator is absent.
- `partner-only` marks signed, encrypted or licensed platform formats that require official tooling and identity.

The classifier never grants an executable capability. It does not extract protected content, emulate activation, provide title keys, bypass signatures or replace platform licensing.

Host probing is designed for real game images rather than only tiny fixtures. It computes the complete SHA-256 incrementally and retains a bounded 64 KiB head plus 512-byte tail sample for classification. This preserves ISO header and DMG trailer detection without allocating memory proportional to a multi-gigabyte image.

## Native import boundary

`zenpkg import-native` accepts redistributable ZEX1 and static ELF32/i386 `ET_EXEC` images only. Architecture classification happens before loader validation, so x86-64, MIPS, PowerPC and console-specific ELF files cannot accidentally enter the native import path.

Generic `EM_MIPS` is not sufficient evidence of a PlayStation 2 executable. The generation policy requires little-endian ELF32 plus the R5900 machine and MIPS III architecture flag combination; other MIPS binaries remain `elf-foreign`.

The generated `.zpk` remains unauthorized until its length and digests are included in signed ZenRepo metadata. Import is conversion, not trust enrollment.

## Generation-aware intake

The classifier distinguishes historical and current families rather than treating an extension as universal compatibility:

- Windows: PE/COFF; MSI, MSP and MSM databases; AppX/MSIX generations; CAB/MSU servicing containers; WIM deployment images.
- Linux: DEB, RPM, Alpine APK v2 and ADB-based APK v3, multiple Arch `pkg.tar` compression generations, AppImage, Snap and Flatpak.
- macOS: thin/universal Mach-O, XAR installer packages and UDIF disk images.
- Original Xbox: XBE.
- Xbox 360: XEX2 and STFS content containers.
- Xbox One and Series: XVC.
- Xbox/Windows PC: MSIXVC and the newer PC-only MSIXVC2 generation.
- PlayStation and PS1: PS-X EXE and optical media candidates.
- PlayStation 2: R5900/MIPS III ELF and disc media; generic MIPS ELF is kept separate.
- PSP and PlayStation Vita: PBP, platform-tagged PKG and VPK.
- PlayStation 3: PKG, SELF and PUP.
- PlayStation 4: `CNT` package, SELF and SCE ELF.
- Media preservation: ISO 9660 and CHD.

PS5-specific support is not asserted without a stable public format contract that can be verified independently. Shared PlayStation container signatures may still be classified, but the result is not labelled as proven PS5 compatibility.

## Runtime-provider boundary

Metadata profiles exist for Proton, Darling, PCSX2, RPCS3, xemu and Xenia. They are planning records, not runnable components in the current kernel.

A functional provider requires substantially more than a package parser:

- Linux compatibility requires a wider ELF ABI, dynamic linking, signals, threads, virtual memory and filesystem/process semantics.
- Windows compatibility additionally requires a Wine/Proton-style Win32 substrate, x86-64, synchronization primitives, graphics, audio and input.
- macOS compatibility requires a Darwin loader, Apple filesystem semantics, frameworks and trust integration.
- PS1/PS2/PS3/PS4 and Xbox generations each require their corresponding CPU, GPU, memory, audio, input, timing and system-service models.
- Signed/encrypted console packages require lawful title ownership and official or user-supplied material; those controls are not replaced by ZenPkg.

## Test boundary

The generation-aware intake is covered by direct signature classification, bounded host probing, deterministic native conversion, negative import tests and ASan/UBSan execution. Guest QEMU testing proves that the added classifier and commands do not break the signed native install/run path.

Current expected evidence:

```text
PACKAGE_FOREIGN_FORMAT_TEST_OK cases=46 generations=legacy-current
ZENPKG_FOREIGN_TEST_OK probes=39 native-import=zex1,elf32 deterministic=2 rejection=7 generations=legacy-current streaming=1
ZENPKG_FOREIGN_QEMU_OK formats=1 probe=zenpkg install=1 run=1 fsck=1
```

These markers prove recognition and native-path isolation. They do not prove that foreign applications or games run.

## Staged roadmap within the 0.1.1 line

- Deterministic package format and host tooling — implemented.
- Transactional native manager and persistent rollback — implemented.
- Signed offline roles, root rotation, delegation and anti-rollback — implemented.
- Search, planning, verified cache, resumable offline transport, upgrade, repair and policy inspection — implemented.
- Shared historical/current package probing — implemented.
- Bounded large-file probing with streaming SHA-256 — implemented.
- Strict ZEX1 and ELF32/i386 native import — implemented.
- Network mirrors and TLS repository download — not implemented.
- General dependency solver, multi-architecture native packages and dynamic linking — not implemented.
- Foreign application and game runtimes — not implemented.

The system version remains 0.1.1 while these changes harden and expand the same release line.
