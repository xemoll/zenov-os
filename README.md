# ZenovOS

**An experimental 32-bit x86 operating system with its own kernel, desktop, storage stack, application ABI, package manager and security model.**

ZenovOS is not a Linux distribution and does not reuse the Windows or macOS userland. It boots through a BIOS loader, runs a freestanding i686 kernel written in C++17 and assembly with Zenov-generated configuration, and executes native applications in ring 3.

![ZenovOS graphical desktop](docs/screenshots/zenov-os-0.1.1-graphical-desktop.png)

> [!IMPORTANT]
> This README describes the current `main` branch. The downloadable `v0.1.1` release is immutable and pinned to source commit [`22a3eec9`](https://github.com/xemoll/zenov-os/commit/22a3eec9b97b0ef0fac35be641c2526c577b1964), published on 16 July 2026. Later work on `main` still uses the internal `0.1.1` version number but is not present in those older release images. Compare the original download with the pinned tag commit rather than with the newer `main` branch.

## What ZenovOS provides

| Area | Current `main` |
| --- | --- |
| Platform | BIOS-bootable 32-bit x86/i686 system tested primarily in QEMU |
| Desktop | Native kernel-rendered shell with Start, taskbar, Terminal, Files, Settings, Applications and System Center surfaces |
| Graphics | QEMU Standard VGA / Bochs VBE, 32-bit framebuffer, 22 verified modes from 640×480 to 1600×1200 |
| Input | PS/2 keyboard and mouse with IRQ-driven input and resolution-independent pointer mapping |
| Memory | E820 physical-memory discovery, 4 KiB paging, kernel heap and isolated ring-3 process window |
| Storage | ATA PIO/LBA28 transport and persistent ZenovFS1 with copy-on-write replacement and typed error propagation |
| Applications | Native ZEX1 and validated static little-endian ELF32/i386 applications |
| ABI | `INT 0x80` system calls, guarded userspace pointers, stable errors and per-application signed capability profiles |
| Packages | ZenPkg native package lifecycle, signed ZenRepo metadata, verified cache and deterministic launch plans |
| Security | ZenovGuard signed trust, capabilities, malware intelligence, controlled-folder policy, authenticated reads, quarantine and persistent audit |
| Hardware trust | Supervisor-only TPM 2.0 TIS FIFO transport with an explicitly provisioned persistent NV counter |
| Build | Reproducible host tools, deterministic images and QEMU-backed CI evidence |

## Desktop

ZenovOS boots into a native graphical shell rather than a Linux desktop environment or external compositor. The UI is rendered into a logical `800×600` software backbuffer and mapped into the selected physical framebuffer while preserving aspect ratio.

The current shell includes:

- a bottom taskbar with Start, pinned system surfaces and a CMOS-backed clock;
- searchable Start and Applications views;
- Terminal, Files and Settings surfaces;
- Quick Settings for theme, display, motion, cursor, density and taskbar alignment;
- persistent UI preferences in `/data/config/ui.cfg`;
- keyboard and mouse navigation across every verified display mode.

The screenshot above is a real `1024×768` QEMU framebuffer captured by CI, not a design mockup. See [Desktop documentation](docs/DESKTOP_0.1.1.md) for the display matrix, controls and persistence contract.

### Main controls

```text
Super / Windows key  Open Start
F5                   Terminal
F6                   Files
F7                   Settings
F8                   Start / Applications
F9                   Cycle verified display modes
F10                  Quick Settings
Escape               Close the active shell surface
```

## Applications and execution model

ZenovOS currently executes two native formats:

- **ZEX1 version 1** — the compact native executable format used by Zenov-built applications;
- **static ELF32/i386** — little-endian executables with bounded, validated `PT_LOAD` segments.

Applications run in a page-granular ring-3 window with a separate stack. Kernel and framebuffer pages remain supervisor-only. Executable admission rejects malformed images and writable-plus-executable ELF segments. A userspace fault terminates the foreground application and returns control to the shell; a kernel fault remains fatal.

The current system is intentionally single-foreground-process and single-threaded. It does not provide a general POSIX environment, dynamic linker, fork/exec process model or Linux binary compatibility.

See [Application ABI](docs/ABI_0.1.1.md), [ZEX ABI](docs/ZEX_ABI.md) and [ELF32 ABI](docs/ELF32_ABI.md).

## Filesystem and storage

ZenovFS1 is the native persistent filesystem used for configuration, applications, package state, security policy, audit data and ordinary files.

Current storage work includes:

- copy-on-write file replacement and interrupted-write recovery;
- checksum validation and host-side filesystem verification;
- stable typed results from the ATA boundary through ZenovFS, shell commands and file syscalls;
- bounded ATA deadlines, reset, IDENTIFY revalidation and retry behavior;
- explicit fail-closed handling for unrecoverable transport and metadata corruption;
- deterministic crash and sector-fault matrices for filesystem, audit and package transactions.

The production storage path is still synchronous ATA PIO/LBA28. AHCI, NVMe, DMA, NCQ, asynchronous queues and multi-device mounting are not implemented.

See [ZenovFS transactions](docs/ZENOVFS1_TRANSACTIONS.md).

## ZenPkg and software compatibility

ZenPkg provides a native package lifecycle with signed repository metadata, dependency/conflict planning, verification, installation, upgrade, repair, rollback, removal, a protected persistent cache and reboot recovery.

The current Runtime Provider ABI can verify content-addressed artifacts and produce deterministic launch plans. The built-in native provider is usable for supported ZEX1 and ELF32 applications.

ZenPkg can also identify and classify a bounded set of Windows, Linux, macOS, Xbox and PlayStation package, executable and media formats. Classification is not execution. Wine, Proton, Darling, Linux ABI layers and console emulators remain planned or metadata-only providers because the required kernel, graphics, audio, process and runtime substrate does not yet exist.

See [Native package manager](docs/NATIVE_PACKAGE_MANAGER_0.1.1.md), [ZenRepo](docs/ZENREPO_OFFLINE_0.1.1.md), [Runtime Provider ABI](docs/RUNTIME_PROVIDER_ABI_0.1.1.md) and [compatibility architecture](docs/PACKAGE_COMPATIBILITY_ARCHITECTURE.md).

## Security model

ZenovGuard is a bounded local integrity and prevention layer. It is not presented as a commercial antivirus, EDR platform or proof of protection against arbitrary modern malware.

The current security stack includes:

- **ZGDB2** — signed executable trust and revocation records;
- **ZCAP1** — signed per-application syscall masks and exact file scopes;
- **ZMID1** — signed bounded hash and byte-pattern intelligence;
- **ZRWP1** — signed controlled-folder writer identities and mutation budgets;
- **ZVRT1** — signed path, size and chunk-Merkle commitments for selected persistent objects;
- **ZGAL1** — persistent bounded hash-chained audit records;
- protected quarantine with metadata sidecars;
- rollback-safe signed-policy updates and a persistent power-loss recovery journal;
- TPM 2.0 TIS transport and an explicit persistent monotonic NV counter.

Executable and selected file reads are appraised after ZenovFS checksum validation. Denied reads scrub caller buffers. Security-sensitive updates use exact successor versions, final read-back validation and fail-closed recovery.

The TPM counter is not yet bound to signed-policy generations or the policy transaction journal. Complete offline disk replacement therefore remains outside the proven freshness boundary.

See [Security model](docs/SECURITY_MODEL_0.1.1.md), [ZenovGuard](docs/ZENOVGUARD_0.1.1.md) and [TPM 2.0 boundary](docs/TPM2_TIS_NV_COUNTER_0.1.1.md).

## Download and run

The published release is available from [GitHub Releases](https://github.com/xemoll/zenov-os/releases/tag/v0.1.1).

The recommended download is `ZenovOS-0.1.1-x86.zip`. It contains the boot image, writable data image, installation guide and QEMU launchers. Use the included `SHA256SUMS.txt` to verify downloaded files:

```bash
sha256sum -c SHA256SUMS.txt
```

The immutable `v0.1.1` tag identifies the exact source used for the original release. Later hardened publication runs may also expose `SOURCE-REVISION.txt`, but the original ZIP should be compared against the pinned tag, not against newer `main` commits.

## Build from source

### Dependencies

A typical Debian/Ubuntu build host needs:

```bash
sudo apt-get update
sudo apt-get install -y build-essential binutils qemu-system-x86 openssl zip unzip
```

### Build and boot

```bash
git clone https://github.com/xemoll/zenov-os.git
cd zenov-os

make
make qemu
```

### Full verification

```bash
make clean check
make deterministic
```

The repository also contains focused host and QEMU gates for display modes, storage faults, package recovery, signed policies, authenticated reads and TPM lifecycle behavior. CI checks out and validates an exact source revision before accepting evidence.

## Project boundaries

ZenovOS is an actively developed experimental operating system. The following are current limitations, not supported features:

- 32-bit i686 only;
- BIOS boot rather than UEFI;
- QEMU Standard VGA / Bochs VBE is the primary verified graphics target;
- one foreground userspace process and one thread;
- no networking, audio, Bluetooth, battery service or multi-monitor stack;
- no dynamic linking, POSIX compatibility layer or general Linux userspace;
- no Windows PE, macOS Mach-O or console binary execution;
- no production-scale malware signature corpus or cloud reputation service;
- no hardware-backed policy freshness until TPM state is bound to policy transactions;
- physical hardware, VMware and VirtualBox are not part of the current automated verification matrix.

## Documentation

Start with [the documentation index](docs/INDEX.md). The most useful entry points are:

- [Desktop and controls](docs/DESKTOP_0.1.1.md)
- [Application ABI](docs/ABI_0.1.1.md)
- [Security model](docs/SECURITY_MODEL_0.1.1.md)
- [ZenovGuard](docs/ZENOVGUARD_0.1.1.md)
- [Native package manager](docs/NATIVE_PACKAGE_MANAGER_0.1.1.md)
- [Runtime Provider ABI](docs/RUNTIME_PROVIDER_ABI_0.1.1.md)
- [Release notes for v0.1.1](docs/releases/v0.1.1.md)
- [Roadmap](docs/ROADMAP_0.1.1.md)

## Development status

The repository uses exact-head builds, deterministic image checks and retained QEMU evidence. A green workflow is treated as evidence for the tested commit only; open draft pull requests are not described as shipped functionality.

Issues and pull requests should state the exact source commit, affected subsystem, reproduction commands and observed serial or framebuffer evidence.
