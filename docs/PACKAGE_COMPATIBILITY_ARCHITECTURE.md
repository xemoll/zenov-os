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

The native path supports bounded ZEX1 and static ELF32/i386 applications, deterministic package creation, search, planning, verified cache fetch, installation by name or file, upgrade, repair, explicit rollback, removal and persistent state. A separate unsigned compatibility command can run the documented minimal Linux/i386 console sandbox; it cannot authorize or replace a signed native package.

## Implemented Linux/i386 compatibility slice

`pkg compat run-linux` accepts a bounded static ELF32/i386 `ET_EXEC` from `/samples` or `/downloads`. It maps conventional i386 virtual addresses into the isolated process window and translates only Linux `int 0x80` calls 1 (`exit`), 4 (`write` to fd 1 or 2) and 252 (`exit_group`). All other syscall numbers return `-ENOSYS`; other descriptors return `-EBADF`.

The runtime rejects dynamic/interpreted ELF, W+X or overlapping segments, non-i386 machines, protected paths and oversized inputs. It reads through the existing unchanged on-access security path, requires ZenPkg's own exact preflight, binds the capability profile to SHA-256 and clears the ABI, mappings and capability state after return. This does not provide libc, POSIX, files, networking, signals, threads or general Linux application compatibility.

## Exact compatibility preflight

`pkg compat check <file>` is read-only and never launches the inspected artifact. The package-manager-owned validators fail closed and report four independent facts: recognition, structural validity, foreign trust and runtime availability.

| Target | Exact checks | Result when valid |
| --- | --- | --- |
| Linux/i386 ELF | Static `ET_EXEC`, load ranges, entry, overlap, page permissions and W^X | Runnable only in the minimal ring-3 sandbox |
| Windows PE32 | DOS/PE/COFF headers, i386 executable type, bounded sections, entry, overlap and W^X | Inspect-only; Win32 and Authenticode are absent |
| PS-X EXE | Header/payload size and two-MiB RAM load, entry, fill and stack ranges | Runnable only in the bounded R3000A diagnostic sandbox |
| PS2 ELF | Little-endian R5900/MIPS III `ET_EXEC`, static segments, ranges, entry, overlap and W^X | Inspect-only; Emotion Engine runtime is absent |
| Original Xbox XBE | Header/image/section-table bounds, raw/virtual sections, overlap and W^X | Inspect-only; signature trust and Xbox runtime are absent |

This subsystem does not modify ZenovGuard's classifier. Foreign signature trust remains `unverified`; structural validity alone never grants executable authority.

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

Metadata profiles exist for Proton, Darling, DuckStation, PCSX2, RPCS3, xemu and Xenia. They remain planning records. The kernel has two independent built-in foreign slices: minimal static Linux/i386 console execution and PS-X EXE R3000A diagnostics. The latter does not replace a complete PS1 emulator provider.

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
PACKAGE_COMPATIBILITY_PREFLIGHT_TEST_OK cases=35 truncations=4492 validators=5 runtime-ready=2 fail-closed=1
PSX_R3000A_TEST_OK cases=15 cpu=integer+branch-delay+load-delay+hi-lo memory=2MiB hle=A0+B0 fail-closed=1
ZENPKG_DATA_RETRY_TEST_OK atomic=1 idempotent=1 staging-clean=1
ZENPKG_FOREIGN_TEST_OK probes=39 native-import=zex1,elf32 deterministic=2 rejection=7 generations=legacy-current streaming=1
LINUX_I386_ABI_TEST_OK syscalls=write,exit,exit_group fail_closed=1
LINUX_I386_ELF_TEST_OK bias=0x08048000 negatives=wx,machine,dynamic,entry
ZENPKG_FOREIGN_QEMU_OK formats=2 preflight=linux-i386+psx linux-i386=write+exit+enosys+sandbox psx=r3000a+delay+hle-console+budget+wipe+reuse probe=zenpkg install=1 run=1 fsck=1
```

The QEMU markers prove the narrow Linux console fixture and that the PS1 text fixture can allocate, wipe, release and reuse its isolated RAM. They do not prove that general Linux applications or PlayStation games run.

## Staged roadmap within the 0.1.1 line

- Deterministic package format and host tooling — implemented.
- Transactional native manager and persistent rollback — implemented.
- Signed offline roles, root rotation, delegation and anti-rollback — implemented.
- Search, planning, verified cache, resumable offline transport, upgrade, repair and policy inspection — implemented.
- Shared historical/current package probing — implemented.
- Bounded large-file probing with streaming SHA-256 — implemented.
- Strict ZEX1 and ELF32/i386 native import — implemented.
- Exact fail-closed PE32, PS-X EXE, PS2/R5900 ELF and Original Xbox XBE structural preflight — implemented.
- Network mirrors and TLS repository download — not implemented.
- General dependency solver, multi-architecture native packages and dynamic linking — not implemented.
- Minimal static Linux/i386 console runtime — implemented.
- Bounded PS-X EXE R3000A diagnostic runtime — implemented.
- General Linux, Windows, macOS and console application/game runtimes — not implemented.

The system version remains 0.1.1 while these changes harden and expand the same release line.
