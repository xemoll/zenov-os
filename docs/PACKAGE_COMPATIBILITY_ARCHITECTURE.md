# Package and compatibility architecture

ZenovOS 0.1.1 separates three concerns that must not be conflated:

1. package transport and metadata (`ZENPKG1`);
2. installation state and rollback (`ZPKDB1`);
3. execution providers and security policy (native loader, ZenovGuard and future compatibility runtimes).

## Current native baseline

The current kernel is 32-bit i686, BIOS-booted and single-foreground-process. Native execution is limited to ZEX1 and validated static ELF32/i386. ZenovFS1 provides 128 fixed metadata entries and 64 KiB per-file slots.

The implemented Stage B path is:

```text
local ZENPKG1 file
  -> strict bounded verifier
  -> compiled bootstrap authorization
  -> immutable payload write
  -> atomic ZPKDB1 active-reference commit
  -> ZGDB + ZenovGuard final-read policy
  -> ring-3 native loader
```

## Repository security direction

A future network repository must not rely on a single package signature. It needs independently versioned and expiring trust metadata with offline root keys, target authorization, a repository snapshot and a frequently refreshed timestamp. Client state must reject metadata version rollback and inconsistent combinations.

Until those roles exist, `pkg` remains local-only and the bootstrap catalog remains fail-closed.

## Compatibility providers

Foreign application support is represented as provider metadata, not as a false claim that the present kernel can execute those formats.

- Linux user applications require a Linux-compatible syscall/ELF ABI, dynamic linker, threads, signals and a substantially larger process model.
- Windows applications require an x86_64-capable substrate and a Wine/Proton provider with graphics, audio, input, filesystem and synchronization support.
- macOS applications require a Darling-like provider and Linux features that ZenovOS does not yet expose.
- PlayStation and Xbox software requires emulator providers, graphics/audio/input backends, large files and user-supplied legal firmware/game assets.

Compatibility profiles in `packages/compat-profiles` are metadata-only and use explicit `user-supplied` asset policy where proprietary assets are involved.

## Staged roadmap within the 0.1.1 line

- Stage A: deterministic host package tooling and compatibility metadata — implemented.
- Stage B: local transactional native package manager integrated with ZenovGuard — implemented.
- Stage C: signed offline repository metadata and key rotation — not implemented.
- Stage D: networking and repository transport — not implemented.
- Stage E: larger process/storage/runtime substrate for compatibility providers — not implemented.

The version remains 0.1.1 while these are hardening increments to the same release line.
