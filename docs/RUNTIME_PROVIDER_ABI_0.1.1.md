# ZenUniverse Runtime Provider ABI v1 — ZenovOS 0.1.1

## Purpose

This contract separates package-format recognition from executable runtime support. A foreign artifact is runnable only when a concrete runtime provider exists, its architecture matches the host, every required host capability is present, and all required proprietary assets were supplied by the user.

The contract deliberately does not treat a recognized ISO, EXE, PBP or CHD as executable support.

## Commands

```text
zenuniverse host-profile --name zenov-0.1.1-i686
zenuniverse runtime-status --input packages/universe --runtime duckstation --host-profile zenov-0.1.1-i686
zenuniverse runtime-plan --input packages/universe --package org.zenov.profile.playstation1-game --host-profile zenov-0.1.1-i686
```

`runtime-plan` accepts a built-in host profile rather than arbitrary capability claims. It exits with status `3` while any provider, architecture or capability blocker remains.

The legacy `resolve --capability ...` command remains available for deterministic resolver unit tests, but prints `capability-source=manual-unverified`. It is not an execution authorization result.

## Current verified host profile

`zenov-0.1.1-i686` records the substrate actually implemented by the current system:

- i686/x86 execution;
- ZEX1 and static ELF32 loading;
- one foreground process;
- framebuffer graphics;
- keyboard and mouse input;
- ZenovFS small-file storage.

It does not claim x86-64, threads, general process management, `mmap`, JIT memory, dynamic linking, OpenGL, Vulkan, streaming audio, gamepad input or large-file storage.

## Provider descriptor extensions

Runtime descriptors may declare repeated fields:

```text
accepts=disc-image
accepts=chd
asset=firmware.playstation1-bios
requires=kernel.threads
provides=runtime.duckstation
```

`accepts` is valid only on `kind=runtime` records and must use a registered artifact family. `asset` is a canonical user-supplied asset identifier. Assets are emitted into the plan but are never downloaded by ZenovOS.

## First provider contracts

### DuckStation / PlayStation 1

The provider accepts PS-X executable, BIN/CUE, CHD, generic disc-image and unencrypted PBP families. It requires a user-dumped PlayStation BIOS plus a 64-bit runtime substrate, SSE4.1 for the normal x86 build, process/thread and memory services, JIT support, a modern graphics API, audio, gamepad input and large-file storage.

### PPSSPP / PlayStation Portable

The provider accepts ISO/disc-image, CSO and PBP families. It requires process/thread and memory services, OpenGL 3.0-or-Vulkan-class graphics, audio, gamepad input and large-file storage. No PSP BIOS asset is declared.

### PCSX2 / PlayStation 2

The existing provider now declares accepted ISO/CSO/disc-image families and the required user-dumped PS2 BIOS. The host requirements include x86-64/SSE4.1-class execution, threads, memory/JIT services, OpenGL 3.3-or-Vulkan 1.1-class graphics, audio, gamepad input and large-file storage.

## Fail-closed behavior

Provider architecture is checked independently from package architecture. This closes the previous gap where an x86 host could receive a plan containing an x86-64 provider.

Repeated blockers are deduplicated. A planned or metadata-only runtime never becomes runnable solely because the caller supplied arbitrary capability strings.

## Current outcome

On `zenov-0.1.1-i686`, PS1, PSP and PS2 runtime plans are expected to be blocked. That is a correct executable result, not a placeholder success. The blockers define the implementation order for the next kernel/userspace work:

1. large-file storage;
2. x86-64 userspace and dynamic loading;
3. multiple processes and threads;
4. `mmap` and controlled JIT memory;
5. gamepad and streaming audio APIs;
6. OpenGL/Vulkan-class graphics substrate;
7. provider packaging and runtime execution tests.

## Legal boundary

Console firmware and commercial game content remain user-supplied. ZenUniverse stores identifiers and requirements only; it does not publish mirrors, hashes or bundled proprietary bytes for those assets.
