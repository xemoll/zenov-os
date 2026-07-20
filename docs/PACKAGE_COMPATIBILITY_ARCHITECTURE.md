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

The native path supports bounded ZEX1 and static ELF32/i386 applications, search, planning, installation by name or file, upgrade, repair, explicit rollback, removal and persistent state.

## Compatibility-provider boundary

Metadata profiles exist for Proton, Darling, PCSX2, RPCS3, xemu and Xenia. They describe future providers and legal asset policy; they do not claim that the current kernel executes those runtimes.

Linux application compatibility requires a much broader ELF/syscall ABI, dynamic linking, signals, threads, virtual memory and process semantics. Windows application support additionally requires a Wine/Proton-compatible substrate, x86_64, synchronization, graphics, audio and input. macOS and console providers require similarly substantial runtime, driver and storage work, plus user-supplied legal firmware or game assets where applicable.

## Staged roadmap within the 0.1.1 line

- Deterministic package format and host tooling — implemented.
- Transactional native manager and persistent rollback — implemented.
- Signed offline roles, root rotation, delegation and anti-rollback — implemented.
- Search, plan, upgrade, repair and policy inspection — implemented.
- Network transport, mirrors, resumable downloads and repository cache — not implemented.
- General dependency solver, multi-architecture packages and dynamic linking — not implemented.
- Foreign application and game runtimes — not implemented.

The system version remains 0.1.1 while these changes harden and expand the same release line.
