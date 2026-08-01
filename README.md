# ZenovOS

**ZenovOS is an experimental 32-bit x86 operating system with its own BIOS loader, freestanding kernel, graphical desktop, persistent filesystem, application ABI, package manager and security model.**

ZenovOS is not a Linux distribution and does not reuse the Windows or macOS userland. The kernel is built from C++17 and x86 assembly with Zenov-generated configuration, boots through legacy BIOS and runs supported native applications in ring 3.

![ZenovOS graphical desktop](docs/screenshots/zenov-os-0.1.1-graphical-desktop.png)

> [!IMPORTANT]
> The original [`v0.1.1`](https://github.com/xemoll/zenov-os/releases/tag/v0.1.1) release is immutable and does not contain the current ISO distribution. For virtual machines, use [`v0.1.1-vm3`](https://github.com/xemoll/zenov-os/releases/tag/v0.1.1-vm3). VM Image 3 is built from the current integrated source and publishes the bootable ISO plus writable disks as direct release assets.

## Download the bootable ISO

No ZIP extraction is required.

- **Bootable ISO:** [`ZenovOS-0.1.1-x86.iso`](https://github.com/xemoll/zenov-os/releases/download/v0.1.1-vm3/ZenovOS-0.1.1-x86.iso)
- **QEMU/KVM disk:** [`ZenovOS-0.1.1-data.qcow2`](https://github.com/xemoll/zenov-os/releases/download/v0.1.1-vm3/ZenovOS-0.1.1-data.qcow2)
- **VirtualBox disk:** [`ZenovOS-0.1.1-data.vdi`](https://github.com/xemoll/zenov-os/releases/download/v0.1.1-vm3/ZenovOS-0.1.1-data.vdi)
- **VMware disk:** [`ZenovOS-0.1.1-data.vmdk`](https://github.com/xemoll/zenov-os/releases/download/v0.1.1-vm3/ZenovOS-0.1.1-data.vmdk)
- **Raw ZenovFS disk:** [`ZenovOS-0.1.1-data.img`](https://github.com/xemoll/zenov-os/releases/download/v0.1.1-vm3/ZenovOS-0.1.1-data.img)
- **Checksums:** [`SHA256SUMS.txt`](https://github.com/xemoll/zenov-os/releases/download/v0.1.1-vm3/SHA256SUMS.txt)

The ISO is read-only. Files, settings, packages and security state are stored on the separate writable ZenovFS disk.

### VM configuration

| Setting | Required value |
| --- | --- |
| Architecture | 32-bit x86 / i686 |
| Firmware | Legacy BIOS; EFI/UEFI disabled |
| Memory | 64 MiB |
| CPU | One virtual CPU |
| Optical drive | `ZenovOS-0.1.1-x86.iso` as IDE CD/DVD |
| Primary IDE disk | Exactly one writable ZenovOS data disk |
| Boot order | Optical drive first |
| Network | Disabled; ZenovOS 0.1.1 has no network stack |

QEMU example:

```bash
qemu-system-i386 \
  -machine pc,vmport=off \
  -m 64M \
  -vga std \
  -drive file=ZenovOS-0.1.1-data.qcow2,format=qcow2,if=ide,index=0,media=disk \
  -drive file=ZenovOS-0.1.1-x86.iso,format=raw,if=ide,index=2,media=cdrom,readonly=on \
  -boot order=d,strict=on
```

Verify downloaded assets:

```bash
sha256sum -c SHA256SUMS.txt
```

The release also contains `prepare-vm.sh`, `prepare-vm.ps1`, `manage-vm.sh`, `VM-QUICKSTART.txt`, a VMware `.vmx` file and provenance manifests.

## Transactional VM lifecycle manager

`manage-vm.sh` creates and manages writable runtime disks without modifying the canonical release seed in place.

```bash
chmod +x manage-vm.sh
./manage-vm.sh status
./manage-vm.sh create
./manage-vm.sh verify
./manage-vm.sh backup
./manage-vm.sh reset
./manage-vm.sh restore /path/to/backup.qcow2
./manage-vm.sh remove
```

Supported runtime formats are `raw`, `qcow2`, `vdi` and `vmdk`:

```bash
ZENOV_VM_FORMAT=qcow2 ./manage-vm.sh create
ZENOV_VM_FORMAT=vdi ./manage-vm.sh create
```

The manager validates the canonical seed checksum and exact 16 MiB virtual size, checks container structure through `qemu-img`, verifies temporary images before atomic replacement, creates checksummed backups before destructive operations and rejects tampered backups, symlink targets, unsafe directories and concurrent lifecycle mutations.

## Current capabilities

| Area | Current implementation |
| --- | --- |
| Platform | BIOS-bootable i686 system tested primarily in QEMU |
| Desktop | Native graphical shell with Start, taskbar, Terminal, Files, Settings, Applications and System Center |
| Productivity | Persistent Notes, Tasks, Calendar, Clock, Calculator, Reminders and Agenda |
| Graphics | QEMU Standard VGA / Bochs VBE, 32-bit framebuffer and 22 verified modes from 640×480 to 1600×1200 |
| Input | IRQ-driven PS/2 keyboard and mouse |
| Memory | E820 discovery, 4 KiB paging, kernel heap and isolated ring-3 process window |
| Storage | ATA PIO/LBA28 and persistent ZenovFS1 with copy-on-write replacement and typed error propagation |
| Applications | Native ZEX1 and validated static little-endian ELF32/i386 applications |
| ABI | `INT 0x80` syscalls, guarded userspace pointers and signed per-application capability profiles |
| Packages | ZenPkg lifecycle, signed ZenRepo metadata, verified cache and deterministic launch plans |
| Security | Signed trust, capabilities, malware intelligence, controlled-folder policy, authenticated reads, quarantine and persistent audit |
| Hardware trust | Supervisor-only TPM 2.0 TIS FIFO transport with an explicitly provisioned NV counter |
| Distribution | Deterministic El Torito ISO plus RAW, QCOW2, VDI and VMDK writable disks |
| Verification | Exact-head host tests, deterministic rebuilds and QEMU-backed acceptance evidence |

## Desktop and system applications

ZenovOS boots into a kernel-rendered graphical shell rather than an external Linux compositor. The UI uses a logical `800×600` software backbuffer and maps it into the selected framebuffer while preserving aspect ratio.

Main controls:

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

Persistent native applications include:

- **Notes / Notepad** — local Markdown vault, search, daily notes, scratchpad, lightweight properties and `[[wikilink]]` backlinks;
- **Tasks** — Markdown checkbox aggregation, priorities, due dates, waiting state, filters and source-file mutation;
- **Calendar** — Gregorian month navigation, RTC-backed Today action and persistent local events;
- **Clock** — CMOS time/date, PIT stopwatch and bounded countdown timer;
- **Calculator** — bounded expression parsing and deterministic arithmetic behavior;
- **Reminders / Agenda** — persistent local reminder records and integrated daily agenda.

The current application layer is local-only. It does not claim network synchronization, CalDAV, cloud accounts, plugins, background agents or rich Markdown rendering.

## Application model

ZenovOS supports two native executable families:

- **ZEX1 version 1** — the compact Zenov executable format;
- **static ELF32/i386** — little-endian binaries with bounded validated `PT_LOAD` segments.

Applications run in a page-granular ring-3 window with a separate stack. Kernel and framebuffer pages remain supervisor-only. Malformed images and writable-plus-executable ELF segments are rejected. A userspace fault terminates the foreground application and returns control to the shell.

The system remains single-foreground-process and single-threaded. It does not provide POSIX, a dynamic linker, `fork`/`exec`, Linux binary compatibility or general Windows/macOS execution.

## Storage and ZenovFS

ZenovFS1 stores configuration, applications, package state, security policy, audit data and user files. The current storage path includes:

- copy-on-write file replacement and interrupted-write recovery;
- checksum validation and offline filesystem verification;
- typed ATA and filesystem result propagation;
- bounded ATA deadlines, reset, IDENTIFY revalidation and retry behavior;
- fail-closed handling for unrecoverable transport or metadata corruption;
- deterministic crash and sector-fault matrices for filesystem, audit and package transactions.

The production device path is synchronous ATA PIO/LBA28. AHCI, NVMe, DMA, NCQ, asynchronous queues and multi-device mounting are not implemented.

## ZenPkg and compatibility boundary

ZenPkg provides signed repository metadata, dependency/conflict planning, verification, installation, upgrade, repair, rollback, removal, protected cache state and reboot recovery.

ZenPkg can classify selected Windows, Linux, macOS, Xbox and PlayStation formats. Classification is not execution. Wine, Proton, Darling, Linux ABI layers and console emulators are not implemented because the required process, graphics, audio and runtime substrates do not yet exist.

## Security model

ZenovGuard is a bounded local integrity and prevention layer, not a claim of complete protection against arbitrary modern malware. The integrated stack includes:

- `ZGDB2` signed executable trust and revocation records;
- `ZCAP1` signed syscall masks and exact file scopes;
- `ZMID1` signed bounded hash and byte-pattern intelligence;
- `ZRWP1` controlled-folder writers and mutation budgets;
- `ZVRT1` signed path, size and Merkle commitments;
- `ZGAL1` persistent hash-chained audit records;
- protected quarantine and rollback-safe policy transactions;
- TPM 2.0 TIS transport with a persistent monotonic NV counter.

The TPM counter is not yet bound to every signed-policy generation and transaction. Complete offline disk replacement therefore remains outside the proven freshness boundary.

## Build from source

Debian/Ubuntu dependencies:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  binutils \
  qemu-system-x86 \
  qemu-utils \
  xorriso \
  openssl \
  zip \
  unzip \
  python3
```

Build and boot:

```bash
git clone https://github.com/xemoll/zenov-os.git
cd zenov-os
make
make qemu
```

Build and verify the complete VM distribution:

```bash
make clean
make all vm-check
```

This creates the bootable ISO, performs normal optical boot and two-boot persistence checks, builds RAW/QCOW2/VDI/VMDK disks, runs the VM lifecycle acceptance matrix and creates the 15-file direct distribution under `dist-vm/`.

Full repository verification:

```bash
make clean check
make deterministic
make all vm-check
```

## Current boundaries

ZenovOS remains an experimental operating system. Current limitations include:

- 32-bit i686 only;
- legacy BIOS rather than UEFI;
- live optical boot rather than installation of a bootloader onto a hard disk;
- QEMU Standard VGA / Bochs VBE as the primary verified graphics target;
- one foreground userspace process and one thread;
- no networking, audio, Bluetooth, battery service or multi-monitor stack;
- no guest additions/tools or accelerated 3D;
- no dynamic linking, POSIX layer or general Linux userspace;
- no Windows PE, macOS Mach-O or console binary execution;
- no physical-hardware installer;
- VirtualBox and VMware runtime execution are not part of hosted CI, although their disk formats are structurally and semantically verified.

## Documentation

Start with [the documentation index](docs/INDEX.md). Key references:

- [VM Image 3 release and download guide](docs/releases/v0.1.1-vm3.md)
- [VM appliance architecture](docs/VM_APPLIANCES_0.1.1.md)
- [Desktop and controls](docs/DESKTOP_0.1.1.md)
- [System applications](docs/SYSTEM_APPS_0.1.1.md)
- [Productivity utilities](docs/PRODUCTIVITY_UTILITIES_0.1.1.md)
- [Application ABI](docs/ABI_0.1.1.md)
- [Security model](docs/SECURITY_MODEL_0.1.1.md)
- [Native package manager](docs/NATIVE_PACKAGE_MANAGER_0.1.1.md)
- [Runtime Provider ABI](docs/RUNTIME_PROVIDER_ABI_0.1.1.md)
- [Roadmap](docs/ROADMAP_0.1.1.md)

Historical images remain available as [`v0.1.1-vm2`](https://github.com/xemoll/zenov-os/releases/tag/v0.1.1-vm2) and [`v0.1.1-vm1`](https://github.com/xemoll/zenov-os/releases/tag/v0.1.1-vm1), but they are pinned to older source snapshots.

## Development status

CI evidence is valid only for the exact tested commit. Draft pull requests are not treated as shipped functionality. Changes intended for distribution must pass exact-head build, deterministic image, QEMU boot, persistence, packaging and checksum gates before publication.
