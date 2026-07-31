# ZenUniverse Runtime Provider ABI v1 — ZenovOS 0.1.1

## Purpose

Runtime Provider ABI v1 separates four states that must not be conflated:

1. an artifact family is recognized;
2. a user-supplied artifact has a content-addressed manifest;
3. a concrete runtime provider can accept that artifact family;
4. the current host has enough verified substrate to build a launch plan.

Recognition alone never authorizes execution. A plan is ready only when the provider exists, its architecture matches the verified host profile, every required capability or capability alternative is satisfied, all required user-owned assets are supplied and hashed, and the artifact fits the host storage contract.

## Command surface

```text
zenuniverse host-profile --name zenov-0.1.1-i686
zenuniverse runtime-status --input packages/universe --runtime native --host-profile zenov-0.1.1-i686
zenuniverse runtime-plan --input packages/universe --package org.zenov.profile.playstation1-game --artifact chd --host-profile zenov-0.1.1-i686
zenuniverse artifact-manifest --input packages/universe --profile PROFILE --artifact FAMILY --file FILE --ownership user-owned --output FILE.zartifact
zenuniverse verify-artifact --manifest FILE.zartifact --file FILE
zenuniverse launch-plan --input packages/universe --manifest FILE.zartifact --file FILE --host-profile zenov-0.1.1-i686 [--asset ID=FILE ...] [--output FILE.zlaunch]
```

Exit status `3` means the request is structurally valid but cannot run on the selected verified host. Invalid descriptors, manifests, files or command arguments return status `2`.

## Typed capability registry

All `requires=`, `provides=` and manually supplied test capabilities must exist in one registry. Misspellings such as `kernel.threadz` are rejected while parsing; they cannot silently become permanent unsatisfied dependencies.

Alternatives use repeated `requires-any=` fields:

```text
requires-any=graphics.opengl3.1|graphics.vulkan1.0
```

The resolver evaluates each branch on isolated plan state and chooses a zero-blocker branch when one exists. If no branch is satisfiable, it emits one canonical blocker and diagnostics for each rejected choice. Synthetic capability names such as `graphics.opengl3.1-or-vulkan1.0` are not accepted.

## ZENHOST1 verified host profile

The built-in `zenov-0.1.1-i686` profile records only substrate already implemented by the current system:

```text
architecture=x86
artifact-bytes-limit=65536
process-limit=1
thread-limit=1
```

Capabilities include the current ZEX1/static ELF32 loader, one foreground process, framebuffer graphics, keyboard/mouse and bounded ZenovFS storage. The profile does not claim x86-64, general processes, threads, `mmap`, JIT, dynamic linking, OpenGL/Vulkan, streaming audio, gamepad input or large-file storage.

The legacy `resolve --capability` surface remains for deterministic resolver tests and prints `capability-source=manual-unverified`. It is not an execution authorization interface.

## Provider descriptor contract

Every `kind=runtime` descriptor must declare:

```text
provider-abi=zen-runtime-provider-1
launch-mode=builtin|exec|external
accepts=<registered artifact family>
provides=runtime.<provider-name>
```

Rules:

- `accepts=` is mandatory for providers and forbidden for ordinary applications/profiles;
- the provided runtime capability must exactly match the provider ID suffix;
- provider architecture is checked independently from application architecture;
- a runtime marked `available` must be backed by `builtin`, `embedded` or verified HTTPS delivery;
- `metadata-only` providers remain non-runnable even if a caller invents capabilities;
- `entrypoint` and launch arguments are canonical and bounded;
- `%artifact%` and `%asset:<id>%` are the only dynamic launch placeholders;
- console firmware and commercial content remain user-supplied and are never mirrored by ZenUniverse.

## ZENARTIFACT1

`artifact-manifest` opens the artifact as a regular file with `O_NOFOLLOW`, streams it in bounded chunks and records:

```text
ZENARTIFACT1
profile=<catalog profile id>
artifact=<family>
ownership=redistributable|user-owned
bytes=<canonical decimal>
sha256=<lowercase SHA-256>
```

The opened file descriptor is inspected before and after hashing. Identity, size, modification-time or status-change-time changes reject the snapshot. Symlinks, empty files, non-regular files, non-canonical manifests, unsupported provider families and console content not declared `user-owned` are rejected.

Manifest and descriptor text reads are bounded. The SHA-256 implementation uses explicit 32-bit modular reduction rather than relying on sanitizer-visible unsigned wraparound and is checked against a known-answer vector.

`verify-artifact` reopens and hashes the file; a changed byte or size invalidates the manifest.

## ZENLAUNCH1

`launch-plan` verifies the artifact manifest and bytes before resolving anything. It then:

1. resolves the application/profile and exact runtime provider;
2. confirms that the provider accepts the manifest artifact family;
3. applies the verified host architecture and capabilities;
4. enforces the host artifact-size limit;
5. requires every declared user-owned firmware asset;
6. hashes each supplied asset and rejects undeclared extras;
7. emits a deterministic provider entrypoint and argument vector;
8. returns ready only when the blocker set is empty.

The current native provider is a real successful ABI path:

```text
org.zenov.runtime.native
provider-abi=zen-runtime-provider-1
launch-mode=builtin
entrypoint=@kernel-loader
accepts=zex1
accepts=elf32
availability=available
```

A content-addressed native ZEX1 fixture produces `ZENUNIVERSE_LAUNCH_READY`. This verifies the ABI, manifest and launch-plan path; actual guest execution continues to use the existing ZenPkg/kernel loader lifecycle.

## Foreign providers

DuckStation, PPSSPP, PCSX2, Wine, Proton, Darling, RPCS3, xemu and Xenia now use the same provider ABI and typed capability registry. They remain `planned`/`metadata-only` because the required host substrate and provider binaries are not present.

DuckStation's descriptor uses its current batch CLI shape (`-batch -- %artifact%`) and declares a user-supplied PS1 BIOS. Its upstream documentation requires a BIOS dumped from a console, a 64-bit host and OpenGL/Vulkan-class graphics. PCSX2 likewise requires a user-dumped PS2 BIOS, a 64-bit host, SSE4.1 and OpenGL 3.3 or Vulkan 1.1-class graphics. These requirements are represented as blockers, not as claimed ZenovOS features.

Primary references:

- https://github.com/stenzek/duckstation/blob/master/README.md
- https://github.com/stenzek/duckstation/blob/master/src/duckstation-qt/qthost.cpp
- https://github.com/PCSX2/pcsx2
- https://pcsx2.net/docs/setup/requirements/

## Verified boundaries

Implemented in this pass:

- deterministic provider catalog;
- typed capabilities and real alternatives;
- provider architecture/delivery/entrypoint validation;
- current-host resource limits;
- content-addressed artifact and asset hashing;
- native ready plan;
- foreign fail-closed plans;
- tamper, symlink, ownership, artifact-family and typo rejection;
- GCC strict build plus Clang ASan/UBSan, unsigned-overflow and integer-conversion gates.

Not implemented:

- x86-64 guest execution;
- multiprocess/threaded guest runtime;
- `mmap`, JIT or dynamic linker;
- large-file storage inside ZenovFS;
- OpenGL/Vulkan, streaming audio or gamepad APIs;
- bundled emulator/provider binaries;
- PS1/PSP/PS2 foreign execution.
