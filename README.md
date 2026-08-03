# ZenovOS

**ZenovOS is an experimental 32-bit x86 operating system with its own BIOS loader, freestanding kernel, graphical desktop, ZenovFS filesystem, application ABI, package manager and signed local security model.**

ZenovOS is not a Linux distribution and does not reuse Windows or macOS userland. It boots through Legacy BIOS and runs supported native ZEX1 and static ELF32/i386 applications in ring 3.

![ZenovOS graphical desktop](docs/screenshots/zenov-os-0.1.1-graphical-desktop.png)

## Download and start

The current distribution is deliberately simple: **one ISO file**.

[**Download ZenovOS-0.1.1-x86.iso**](https://github.com/xemoll/zenov-os/releases/download/v0.1.1-live1/ZenovOS-0.1.1-x86.iso)

Release page: [`v0.1.1-live1`](https://github.com/xemoll/zenov-os/releases/tag/v0.1.1-live1)

No ZIP extraction, VDI, QCOW2, VMDK, raw data image, helper script, or second virtual disk is required.

### VirtualBox

1. Create a new **Other/Unknown 32-bit** virtual machine.
2. Use **64 MiB RAM or more** and **one CPU**.
3. Keep **EFI disabled** so the VM uses Legacy BIOS.
4. Attach `ZenovOS-0.1.1-x86.iso` to the virtual optical drive.
5. Put the optical drive first in the boot order and start the VM.

ZenovOS loads its embedded system image, mounts a writable Live session, initializes applications and security services, and opens the graphical desktop automatically.

> VirtualBox still requires AMD-V/VT-x to be enabled and available on the host. A guest ISO cannot change BIOS or host-hypervisor settings. QEMU TCG can run ZenovOS without hardware virtualization.

### QEMU

```bash
qemu-system-i386 \
  -machine pc,vmport=off \
  -m 64M \
  -vga std \
  -drive file=ZenovOS-0.1.1-x86.iso,format=raw,if=ide,index=2,media=cdrom,readonly=on \
  -boot order=d,strict=on
```

Without AMD-V/VT-x:

```bash
qemu-system-i386 \
  -accel tcg,thread=multi \
  -machine pc,vmport=off \
  -m 64M \
  -vga std \
  -drive file=ZenovOS-0.1.1-x86.iso,format=raw,if=ide,index=2,media=cdrom,readonly=on \
  -boot order=d,strict=on
```

## How the Live ISO works

The ISO contains both the boot system and the canonical ZenovFS system volume. At startup the kernel:

1. boots the verified FAT12 loader through BIOS El Torito;
2. decodes the embedded deterministic ZenovFS sparse image;
3. exposes it as a 16 MiB logical block device;
4. creates a writable RAM copy-on-write overlay;
5. mounts ZenovFS through the normal typed block-device interface;
6. initializes signed policy, applications, packages and the graphical shell.

Files and settings work during the running session. Changes reset after reboot or power-off because the ISO itself is read-only. Persistent installation to a hard disk is a separate future feature.

## Current capabilities

| Area | Current implementation |
| --- | --- |
| Platform | BIOS-bootable 32-bit x86/i686 system |
| Distribution | One deterministic self-contained Live ISO |
| Desktop | Native graphical shell with Start, taskbar, Terminal, Files, Settings, Applications and System Center |
| Applications | Notes, Tasks, Calendar, Clock, Calculator, Reminders and Agenda |
| Graphics | Standard VGA / Bochs VBE framebuffer with verified display modes |
| Input | IRQ-driven PS/2 keyboard and mouse |
| Memory | E820 discovery, 4 KiB paging, kernel heap and isolated ring-3 process window |
| Storage | Embedded ZenovFS base plus writable RAM overlay; optional ATA path retained for development |
| Applications | Native ZEX1 and validated static little-endian ELF32/i386 binaries |
| ABI | `INT 0x80` syscalls, guarded userspace pointers and signed capability profiles |
| Packages | ZenPkg lifecycle, signed ZenRepo metadata, verified cache and deterministic launch plans |
| Security | Signed trust, capability policy, malware intelligence, controlled folders, authenticated reads, quarantine and audit |
| Hardware trust | TPM 2.0 TIS FIFO transport with explicitly provisioned NV counter |
| Verification | Exact-head host tests, deterministic rebuild and QEMU no-disk boot evidence |

## Desktop controls

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

The current desktop is kernel-rendered and uses a logical `800×600` software backbuffer mapped into the selected framebuffer.

## Application model

ZenovOS supports:

- **ZEX1 version 1**, the compact Zenov executable format;
- **static ELF32/i386**, with bounded validated `PT_LOAD` segments.

Applications run in a page-granular ring-3 window with a separate stack. Kernel and framebuffer pages remain supervisor-only. Malformed binaries and writable-plus-executable ELF segments are rejected.

The system remains single-foreground-process and single-threaded. It does not provide POSIX, a dynamic linker, `fork`/`exec`, Linux binary compatibility, Wine, Proton, or macOS runtime compatibility.

## ZenovFS and Live storage

The canonical ZenovFS image contains configuration, applications, package state and signed security policy. For the Live ISO it is packed deterministically into sparse chunks and compiled into the boot kernel. Reads come from the immutable embedded base; changed sectors are copied into RAM on first write.

The storage layer retains:

- copy-on-write file replacement;
- checksums and filesystem verification;
- typed block and filesystem results;
- interrupted-write recovery;
- guarded security and package paths;
- bounded fail-closed handling.

Live-session storage is temporary. The public release does not require or publish a companion hard-disk image.

## ZenPkg and compatibility boundary

ZenPkg provides signed repository metadata, dependency and conflict planning, verification, installation, upgrade, repair, rollback, removal and protected cache state.

It can classify selected Windows, Linux, macOS, Xbox and PlayStation artifact families. Classification is not execution. General foreign-binary compatibility requires runtime providers that are not implemented in ZenovOS 0.1.1.

## Security model

The integrated local security stack includes:

- `ZGDB2` signed executable trust and revocation records;
- `ZCAP1` signed syscall masks and exact file scopes;
- `ZMID1` signed bounded hash and byte-pattern intelligence;
- `ZRWP1` controlled-folder writers and mutation budgets;
- `ZVRT1` signed path, size and Merkle commitments;
- `ZGAL1` hash-chained audit records;
- protected quarantine and rollback-safe policy transactions;
- TPM 2.0 TIS transport with a monotonic NV counter.

ZenovGuard is a bounded experimental integrity and prevention layer, not a claim of complete protection against arbitrary modern malware.

## Build from source

Debian/Ubuntu dependencies:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  binutils \
  qemu-system-x86 \
  xorriso \
  openssl
```

Build and verify the complete ISO-only path:

```bash
git clone https://github.com/xemoll/zenov-os.git
cd zenov-os
make clean
make all iso-check
```

The verification target:

- builds the canonical ZenovFS image;
- deterministically packs it into the kernel;
- builds the El Torito ISO;
- boots the ISO in QEMU with **no hard disk attached**;
- waits for ZenovFS mount, desktop and UI readiness;
- writes and reads a Live-session file;
- runs filesystem check and sync;
- rebuilds the ISO byte-identically.

The resulting launch artifact is:

```text
build/ZenovOS-0.1.1-x86.iso
```

## Current boundaries

ZenovOS remains experimental. Current limitations include:

- 32-bit i686 only;
- Legacy BIOS rather than UEFI;
- Live optical boot rather than hard-disk installation;
- session-only writes in the public ISO;
- Standard VGA / Bochs VBE as the primary graphics target;
- one foreground userspace process and one thread;
- no networking, audio, Bluetooth, battery service or multi-monitor stack;
- no guest additions or accelerated 3D;
- no dynamic linking or POSIX layer;
- no physical-hardware installer.

## Documentation

- [Self-contained Live ISO architecture](docs/LIVE_ISO_0.1.1.md)
- [Live ISO 1 download and startup guide](docs/releases/v0.1.1-live1.md)
- [Desktop and controls](docs/DESKTOP_0.1.1.md)
- [System applications](docs/SYSTEM_APPS_0.1.1.md)
- [Application ABI](docs/ABI_0.1.1.md)
- [Security model](docs/SECURITY_MODEL_0.1.1.md)
- [Native package manager](docs/NATIVE_PACKAGE_MANAGER_0.1.1.md)
- [Documentation index](docs/INDEX.md)

Older VM1–VM4 releases remain immutable historical snapshots. `v0.1.1-live1` is the current one-file distribution.
