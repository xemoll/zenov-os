# Package and compatibility architecture

ZenovOS 0.1.1 separates package transport, installation state, execution policy and foreign-runtime providers. A package manager can securely install native software without implying compatibility with Windows, Linux, macOS, PlayStation or Xbox binaries.

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

The native path supports bounded ZEX1 and static ELF32/i386 applications, search, planning, verified cache fetch, installation by name or file, upgrade, repair, explicit rollback, removal and persistent state.

## Foreign intake boundary

ZenPkg now has a shared signature classifier used by the host tool and the in-system package manager. It recognizes native ZenPkg/ZEX1/ELF inputs and selected Linux, Windows, macOS, Xbox and PlayStation container families. Every result carries an explicit support state:

```text
installable | host-import | inspect-only | runtime-required | partner-only | unsupported
```

The classifier is advisory and read-only. It does not extract archives, run package scripts, decrypt console content or grant execution authority. Extension-only matches are reported as such.

The only foreign-to-native conversion implemented in 0.1.1 is `zenpkg import-native` for redistributable ZEX1 and static ELF32/i386 executables that already satisfy the native loader contract. The generated `.zpk` remains unauthorized until it is included in signed ZenRepo metadata.

## Compatibility-provider boundary

Metadata profiles exist for Proton, Darling, PCSX2, RPCS3, xemu and Xenia. They describe future providers and legal asset policy; they do not claim that the current kernel executes those runtimes.

Linux application compatibility requires a broader ELF/syscall ABI, dynamic linking, signals, threads, virtual memory and process semantics. Windows application support additionally requires a Wine/Proton-compatible substrate, x86_64, synchronization, graphics, audio and input. macOS requires a Darwin-compatible loader, filesystem support, frameworks and trust integration.

Xbox and PlayStation package handling is a separate partner boundary. Official SDK access, package identities, signing/decryption material, licensing and target runtimes cannot be replaced by a generic archive parser. ZenovOS only reports these formats as partner-only and does not attempt to bypass platform controls.

## Staged roadmap within the 0.1.1 line

- Deterministic package format and host tooling — implemented.
- Transactional native manager and persistent rollback — implemented.
- Signed offline roles, root rotation, delegation and anti-rollback — implemented.
- Search, planning, verified cache, resumable offline transport, upgrade, repair and policy inspection — implemented.
- Shared foreign package probing and strict native import — implemented.
- Network mirrors and TLS repository download — not implemented.
- General dependency solver, multi-architecture packages and dynamic linking — not implemented.
- Foreign application and game runtimes — not implemented.

The system version remains 0.1.1 while these changes harden and expand the same release line.
