# ZenovOS 0.1.1 VM appliances

The current verified appliance distribution is [`v0.1.1-vm3`](https://github.com/xemoll/zenov-os/releases/tag/v0.1.1-vm3). It publishes the bootable ISO, writable RAW/QCOW2/VDI/VMDK disks, preparation helpers, the transactional lifecycle manager and provenance as 15 direct assets.

Older [`v0.1.1-vm2`](https://github.com/xemoll/zenov-os/releases/tag/v0.1.1-vm2) and [`v0.1.1-vm1`](https://github.com/xemoll/zenov-os/releases/tag/v0.1.1-vm1) releases remain immutable historical snapshots.

ZenovOS boots from a read-only BIOS El Torito ISO and stores persistent state on a separate writable ZenovFS disk. Convenience containers change the host-side disk format without changing the canonical guest-visible filesystem bytes.

## Distribution files

| File | Purpose |
| --- | --- |
| `ZenovOS-0.1.1-x86.iso` | Read-only ISO 9660 / El Torito boot medium |
| `ZenovOS-0.1.1-x86.img` | Raw FAT12 boot-image fallback |
| `ZenovOS-0.1.1-data.img` | Canonical raw 16 MiB ZenovFS seed |
| `ZenovOS-0.1.1-data.qcow2` | QEMU/KVM writable disk |
| `ZenovOS-0.1.1-data.vdi` | VirtualBox writable disk |
| `ZenovOS-0.1.1-data.vmdk` | VMware writable disk |
| `ZenovOS-0.1.1.vmx` | VMware Workstation/Fusion configuration |
| `prepare-vm.sh` | Linux/macOS preparation helper |
| `prepare-vm.ps1` | Windows PowerShell preparation helper |
| `manage-vm.sh` | Transactional lifecycle manager |
| `VM-QUICKSTART.txt` | Setup, backup, reset and recovery guide |
| `VM-APPLIANCE-MANIFEST.json` | Format and guest-content provenance |
| `BUILD-MANIFEST.json` | Build provenance |
| `SOURCE-REVISION.txt` | Exact release source commit |
| `SHA256SUMS.txt` | Complete checksum manifest |

The package deliberately requires no ZIP extraction.

## Canonical content and format validation

`ZenovOS-0.1.1-data.img` is the content authority. Every QCOW2, VDI and VMDK image must satisfy:

1. the declared format is confirmed with `qemu-img info`;
2. virtual size is exactly 16,777,216 bytes;
3. `qemu-img check` succeeds;
4. the image converts back to raw;
5. the round-trip raw bytes match the canonical ZenovFS seed exactly.

Container metadata can contain format-generated identifiers, so byte-identical QCOW2/VDI/VMDK files are not claimed across unrelated tool builds. Reproducibility is defined at the guest-visible raw-content boundary. The ISO, raw images, scripts and textual manifests are deterministic under the pinned build epoch.

## Optical boot and persistence acceptance

`tests/qemu_iso_smoke.sh` boots the ISO as an IDE CD-ROM and requires the kernel, ZenovFS mount and graphical desktop readiness markers.

`tests/qemu_iso_persistence.sh`:

1. creates a writable QCOW2 disk from the canonical seed;
2. boots the ISO and writes a persistent payload;
3. runs `sync` and shuts the guest down;
4. boots the same ISO and disk again;
5. reads the payload back;
6. runs ZenovFS `fsck`;
7. validates the mutated filesystem offline.

The gate requires two independent boot markers and observes the persistence payload in both phases.

## Transactional lifecycle acceptance

`packaging/manage-vm.sh` provides `status`, `create`, `verify`, `backup`, `restore`, `reset` and `remove` for RAW, QCOW2, VDI and VMDK runtime disks.

The lifecycle contract includes:

- SHA-256 validation of the canonical seed;
- exact virtual-size and format checks;
- verified temporary images before atomic replacement;
- backup-before-reset, restore and remove;
- one SHA-256 sidecar per backup;
- rejection of a tampered backup before runtime state changes;
- fail-closed locking for concurrent mutations;
- refusal of symlink targets and unsafe state directories.

`tests/vm_lifecycle_test.sh` mutates guest-visible data, backs it up, resets to the seed, restores the exact changed bytes, rejects checksum tampering, rejects an active lock and exercises all four advertised runtime formats.

## Publication contract

The VM Image 3 workflow publishes only after `make all vm-check` succeeds. It then:

- verifies exactly 15 local assets and all checksum entries;
- creates a draft release at the exact source commit;
- uploads and downloads every asset;
- performs byte-for-byte comparison;
- verifies the exact sorted asset-name set;
- makes the release public and marks it latest;
- downloads the ISO again through the public unauthenticated URL and compares it with the verified local file.

This final public-download check prevents a green build from being mistaken for a user-accessible release.

## Required VM configuration

- 32-bit x86 guest;
- legacy BIOS firmware;
- 64 MiB RAM;
- one CPU;
- ISO attached as IDE CD/DVD and first in boot order;
- exactly one writable ZenovOS data disk attached as primary IDE storage;
- networking disabled.

Attach only one ZenovOS data disk. Shut down the VM before copying, replacing or restoring the disk.

## Build commands

```bash
make clean
make all vm-check
```

Focused stages:

```bash
make iso-check
make vm-appliances-semantic
make vm-lifecycle-check
make vm-package
```

The resulting direct package is written to `dist-vm/`.

## Hypervisor boundary

QEMU is executed in automated acceptance testing. VirtualBox and VMware artifacts are validated structurally and by canonical raw round-trip, but proprietary hypervisor binaries are not run in GitHub-hosted CI. The project therefore claims prepared and verified disk/configuration artifacts, not full automated certification of those products.

## Current limitations

- legacy BIOS only;
- live optical boot rather than a bootloader installed onto the writable disk;
- separate boot and data media;
- no UEFI or physical-hardware installer;
- no networking, audio, guest additions or accelerated 3D.
