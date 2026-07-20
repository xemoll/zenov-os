# ZenUniverse repository and runtime-provider contract

ZenUniverse extends ZenPkg and ZenRepo with a platform-neutral software catalog. It does not treat an installer format as proof that ZenovOS can execute it. Every catalog entry identifies the original platform, architecture, artifact format, delivery policy, exact runtime provider and required host capabilities.

## Descriptor format

A `.zsource` file uses canonical `key=value` records:

```text
schema=zen-source-1
id=publisher.application
version=1.2.3
kind=application
platform=windows
architecture=x86_64
artifact=msix
delivery=https
runtime=wine
availability=available
entrypoint=application.exe
channel=stable
category=development
license=MIT
description=Example application.
homepage=https://example.org/application
bytes=123456
sha256=<64 lowercase hexadecimal characters>
mirror=https://downloads.example.org/application.msix
requires=storage.large-files
```

Repeated `mirror`, `requires` and `provides` fields are sorted before catalog compilation. Duplicate identities, duplicate capabilities, unknown fields, insecure URLs, invalid hashes and platform/runtime mismatches are rejected.

## Supported source platforms and artifacts

| Platform | Accepted catalog artifacts | Runtime route |
|---|---|---|
| ZenovOS | ZPK, ZEX1, static ELF32 | native |
| Linux | ELF32/64, AppImage, Flatpak, DEB, RPM, TAR | Linux ABI, QEMU user/system or external provider |
| Windows | EXE, MSI, MSIX, AppX | Wine, Proton, QEMU system or external provider |
| macOS | APP, DMG, PKG | Darling, QEMU system or external provider |
| PlayStation 2/3 | ISO or disc image | PCSX2 or RPCS3 |
| Xbox/Xbox 360 | ISO, disc image or ROM container | xemu or Xenia |

The catalog can describe these formats now. Execution is enabled only when the selected runtime provider is marked `available` and all of its capabilities are present. Planned providers remain visible in search and planning but cannot be reported as runnable.

## Runtime-provider resolution

`zenuniverse resolve` builds a deterministic installation chain:

```text
requested program
  -> required runtime capability
  -> runtime provider
  -> provider dependencies
  -> kernel, graphics, audio, input and storage capabilities
```

The resolver detects missing providers, unavailable providers, architecture mismatches and dependency cycles. It never silently falls back from a requested compatibility layer to full-system virtualization.

Examples:

```text
zenuniverse resolve --input packages/universe \
  --package org.zenov.profile.windows-game \
  --host-arch x86_64 \
  --capability kernel.processes \
  --capability graphics.vulkan

zenuniverse fetch-plan --input packages/universe \
  --package publisher.application
```

## Download contract

For `delivery=https`, a descriptor must contain at least one HTTPS mirror, an exact byte size and a lowercase SHA-256 digest. The future on-device downloader must:

1. write only to a non-executable temporary object;
2. use bounded redirects and HTTPS-only mirrors;
3. resume only when the server supplies a compatible byte range;
4. verify final length and SHA-256 over the complete artifact;
5. pass the verified object to ZenRepo authorization;
6. atomically move the authorized object into package storage;
7. keep incomplete or failed objects non-runnable.

`fetch-plan` exposes the machine-readable inputs for that downloader. ZenovOS 0.1.1 still lacks the networking, TLS, large-file storage and process infrastructure required to execute the plan inside the kernel.

## Legal asset policy

Console game and firmware content is always `delivery=user-supplied`. Such entries may describe import and runtime requirements, but they cannot publish a mirror, byte length or repository-controlled digest. ZenovOS must not download or redistribute proprietary console assets.

## Current provider inventory

The catalog contains planned providers for Wine, Proton, a Linux ABI layer, Darling, PCSX2, RPCS3, xemu and Xenia. They are deliberately marked `planned`, because the current 32-bit single-process ZenovOS kernel cannot host them.

The main prerequisites are:

- x86-64 userspace and ABI;
- concurrent processes and threads;
- virtual memory mapping, signals, futex-like synchronization and dynamic linking;
- large files and scalable package storage;
- networking, DNS, TLS and resumable downloads;
- Vulkan/OpenGL, accelerated audio and gamepad APIs;
- namespaces or equivalent isolation for compatibility runtimes.

This separation lets repository work continue without claiming compatibility that the operating system cannot yet provide.
