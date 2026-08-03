# ZenovOS 0.1.1 VM appliances

The current virtual-machine distribution is VM Image 4. It is designed around a read-only BIOS El Torito ISO and a separate writable ZenovFS disk.

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
| `prepare-vm.sh` | Linux/macOS preparation, repair and launch helper |
| `prepare-vm.ps1` | Windows PowerShell preparation and launch helper |
| `manage-vm.sh` | Transactional lifecycle manager |
| `VM-QUICKSTART.txt` | Setup, backup, reset and recovery guide |
| `VM-APPLIANCE-MANIFEST.json` | Format and guest-content provenance |
| `BUILD-MANIFEST.json` | Build provenance |
| `SOURCE-REVISION.txt` | Exact release source commit |
| `SHA256SUMS.txt` | Complete checksum manifest |

The package deliberately requires no ZIP extraction.

## VirtualBox AMD-V/VT-x recovery

VirtualBox depends on host hardware virtualization. The ZenovOS ISO and VDI cannot enable AMD-V/VT-x in firmware or release it from another host hypervisor.

VM Image 4 fixes the distributed Linux/macOS launcher instead. `prepare-vm.sh` repairs an existing powered-off VirtualBox VM, applies the verified ZenovOS settings and starts it normally. When VirtualBox returns a recognized hardware-virtualization error, the helper opens the same ISO and writable VDI with QEMU TCG software emulation.

For an existing VM named `gergre`:

```bash
ZENOV_VM_NAME=gergre ./prepare-vm.sh virtualbox
```

The recognized failures include `VERR_SVM_DISABLED`, `VERR_SVM_IN_USE`, `VERR_VMX_NO_VMX`, `VERR_VMX_IN_VMX_ROOT_MODE`, `VERR_NEM_NOT_AVAILABLE` and the Hyper-V raw-mode error. Set `ZENOV_VM_SOFTWARE_FALLBACK=0` to disable fallback. Set `ZENOV_QEMU_ACCEL=tcg` to force software emulation.

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

`tests/qemu_iso_persistence.sh` creates a writable disk from the canonical seed, boots the ISO twice, writes and replays a persistent payload, runs ZenovFS `fsck` and validates the mutated filesystem offline.

## Launcher acceptance

`tests/vm_launcher_test.sh` uses deterministic VirtualBox and QEMU shims. It verifies:

- creation of a new VirtualBox VM;
- repair of the existing powered-off `gergre` VM;
- successful native VirtualBox start;
- `VERR_SVM_DISABLED` fallback to the same VDI through QEMU TCG;
- explicit fallback disablement;
- refusal to modify a running VM;
- direct QEMU TCG launch.

The launcher test is part of `make vm-check`, the BIOS/VM workflow and the VM Image 4 publication gate.

## Transactional lifecycle acceptance

`packaging/manage-vm.sh` provides `status`, `create`, `verify`, `backup`, `restore`, `reset` and `remove` for RAW, QCOW2, VDI and VMDK runtime disks.

The lifecycle contract includes checksum and virtual-size validation, verified temporary images before atomic replacement, backup-before-destructive-operation, checksummed backup sidecars, rejection of tampered backups, fail-closed locking and refusal of unsafe symlink targets.

## Publication contract

VM Image 4 publishes only after `make all vm-check` succeeds. The workflow then verifies exactly 15 assets, creates a draft release at the exact merge commit, re-downloads every asset for byte comparison, makes the release public, downloads the ISO and `prepare-vm.sh` without authentication, checks their published SHA-256 values and validates the ISO El Torito metadata.

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

The resulting direct package is written to `dist-vm/`.

## Hypervisor boundary

QEMU is executed in automated acceptance testing. VirtualBox and VMware artifacts are validated structurally and by command-contract shims, but proprietary hypervisor binaries are not run in GitHub-hosted CI.

QEMU TCG does not require AMD-V/VT-x but is slower than AMD-V/KVM or native VirtualBox acceleration. Native VirtualBox startup still requires hardware virtualization to be enabled in firmware and available to VirtualBox.

## Current limitations

- legacy BIOS only;
- live optical boot rather than a bootloader installed onto the writable disk;
- separate boot and data media;
- no UEFI or physical-hardware installer;
- no networking, audio, guest additions or accelerated 3D.
