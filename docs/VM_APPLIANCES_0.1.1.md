# ZenovOS 0.1.1 VM appliances

The verified appliance distribution is published as [`v0.1.1-vm2`](https://github.com/xemoll/zenov-os/releases/tag/v0.1.1-vm2), pinned to source commit [`d60cf00f67bc07f00070a56b237843bc8387866f`](https://github.com/xemoll/zenov-os/commit/d60cf00f67bc07f00070a56b237843bc8387866f).

ZenovOS 0.1.1 uses a read-only BIOS El Torito ISO for boot and a separate writable ZenovFS disk for persistent state. The VM appliance pipeline adds native convenience containers for QEMU/KVM, VirtualBox and VMware without changing the guest-visible filesystem bytes.

## Distribution files

| File | Purpose |
| --- | --- |
| `ZenovOS-0.1.1-x86.iso` | Read-only ISO 9660 / El Torito boot medium |
| `ZenovOS-0.1.1-data.img` | Canonical raw 16 MiB ZenovFS disk |
| `ZenovOS-0.1.1-data.qcow2` | QEMU/KVM writable convenience disk |
| `ZenovOS-0.1.1-data.vdi` | VirtualBox writable convenience disk |
| `ZenovOS-0.1.1-data.vmdk` | VMware writable convenience disk |
| `ZenovOS-0.1.1.vmx` | VMware Workstation/Fusion configuration |
| `prepare-vm.sh` | Linux/macOS preparation and launch helper |
| `prepare-vm.ps1` | Windows PowerShell preparation and launch helper |

The direct `dist-vm` package intentionally does not require ZIP extraction.

Primary published checksums:

```text
ISO:      99619eadce4b881652109366887ddb9bc7ee9b5ec5a99b986b78346ab41f6ddd
Data IMG: 2d5d27155409c4284c071e0689a8184895840e541fd28ea78111b1b45e693c7b
```

Use `SHA256SUMS.txt` from the release for the complete 14-file asset set.

## Verified architecture

The raw ZenovFS image is the canonical content authority. `qemu-img` creates QCOW2, VDI and VMDK containers. Each generated image must pass all of the following checks:

1. the declared container format is confirmed with `qemu-img info`;
2. the virtual size is exactly 16,777,216 bytes;
3. `qemu-img check` reports a consistent image;
4. the container is converted back to raw;
5. the round-trip raw bytes are compared byte-for-byte with the canonical ZenovFS image.

Container metadata can contain format-specific identifiers, so byte-identical container files are not claimed across independent `qemu-img` builds. Reproducibility is defined at the guest-visible disk-content boundary. The canonical appliance manifest, launch scripts, quickstart and VMware configuration remain byte-identical across the semantic rebuild gate.

## Two-boot persistence gate

`tests/qemu_iso_persistence.sh` performs a real optical-boot persistence test:

1. converts the canonical data image to QCOW2;
2. boots ZenovOS from the ISO with the QCOW2 disk attached as primary IDE storage;
3. writes `ISO_VM_PERSISTENCE_OK` into ZenovFS;
4. executes `sync` and shuts down QEMU through the monitor;
5. boots the same ISO and writable QCOW2 disk again;
6. reads the persisted payload;
7. runs `fsck` and validates the converted-back ZenovFS image.

The gate requires two independent `ZENOVOS_BOOT_OK` markers and observes the payload in both phases. The publication workflow repeats this gate from a clean checkout of the exact release source.

## Hypervisor boundary

QEMU is the automated execution target. VirtualBox and VMware artifacts are validated structurally and through content round-trips, but proprietary hypervisor binaries are not executed on GitHub-hosted runners. Therefore the project claims prepared configurations for those hypervisors, not automated runtime certification.

## Required VM configuration

- 32-bit x86 guest;
- legacy BIOS firmware;
- 64 MiB memory;
- one CPU;
- ISO attached as an IDE CD/DVD drive;
- writable ZenovFS image attached as primary IDE disk;
- optical drive first in boot order;
- networking disabled because ZenovOS 0.1.1 has no network stack.

Attach exactly one writable ZenovOS data disk. Shut down the VM before copying, replacing or resetting it.

## Commands

Build and verify the complete VM distribution:

```bash
make clean
make all vm-check
```

The resulting direct files are placed in `dist-vm/`.

Prepare or start a local VM:

```bash
./prepare-vm.sh qemu
./prepare-vm.sh virtualbox
./prepare-vm.sh vmware
```

Windows PowerShell equivalents:

```powershell
.\prepare-vm.ps1 Qemu
.\prepare-vm.ps1 VirtualBox
.\prepare-vm.ps1 VMware
```

The helpers verify the release checksum entries for the ISO and canonical raw data seed before preparing the selected writable format. Use the documented reset option only after backing up the current writable disk.

## Publication evidence

VM Image 2 was published by workflow run `30662768581`. All 14 assets were uploaded as a draft, downloaded back and compared byte-for-byte before the release was made public. See [the complete release notes](releases/v0.1.1-vm2.md).

## Current non-goals

This work does not claim:

- UEFI boot;
- installation of the bootloader onto a virtual hard disk;
- installation on physical hardware;
- VirtualBox Guest Additions or VMware Tools;
- networking, audio or accelerated 3D graphics.

Those features require new kernel, bootloader and installer contracts rather than packaging changes.
