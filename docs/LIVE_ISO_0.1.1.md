# ZenovOS 0.1.1 self-contained Live ISO

ZenovOS now uses one launch artifact:

```text
ZenovOS-0.1.1-x86.iso
```

No secondary ZenovFS disk, VDI, QCOW2, VMDK, raw image, VM configuration file, or preparation script is required.

## Boot contract

The virtual machine must provide:

- 32-bit x86 execution;
- Legacy BIOS firmware;
- at least 64 MiB RAM;
- one virtual CPU;
- a standard VGA-compatible display;
- the ISO attached as a virtual CD/DVD drive;
- optical media first in the boot order.

At boot, the El Torito loader starts the verified FAT12 image and kernel. When no persistent ATA disk is present, the kernel:

1. expands the deterministic embedded `ZLIVE003` container into the final package-seeded `ZLIVE002` sparse ZenovFS image;
2. validates its checksum, geometry, sorted ranges and exact payload length;
3. exposes it as a 16 MiB logical block device;
4. creates a writable 512 KiB RAM copy-on-write overlay;
5. mounts ZenovFS normally through the typed block-device API;
6. initializes applications, signed repository metadata, package state, security services and the graphical shell;
7. opens the desktop without a setup wizard or additional media.

Expected runtime evidence includes:

```text
ZENOVFS_LIVE_IMAGE_OK source=embedded format=ZLIVE003
RAM_BLOCK_DEVICE_READY sectors=32768 overlay=1024
ZENOVFS_LIVE_READY mode=ram-overlay persistence=session
ZENOVFS_STORAGE_MODE live-iso
ZENOVFS_MOUNT_OK
GRAPHICAL_DESKTOP_READY
ZENOVOS_UI_READY
```

## Storage semantics

The embedded base image is immutable. Modified sectors are copied into RAM on first write. Files, settings and local application data therefore work normally during the running session, including write, read, filesystem check and metadata synchronization.

Live-session changes are deliberately discarded after reboot or power-off. Persistent hard-disk installation is not part of this release.

If a valid ZenovFS ATA disk is attached, the existing persistent storage path remains available internally for development and regression testing. The public launch contract, however, requires only the ISO.

## Compact low-memory representation

The completed 16 MiB ZenovFS image is generated first, including native packages and signed ZenRepo metadata. It is converted into sorted non-zero byte ranges using the compact `ZLIVE002` sparse layout. That sparse payload is then compressed by the deterministic `ZLIVE003` LZSS transport.

Only the compressed bytes reside in the low-memory kernel image. After physical-memory and heap initialization, the kernel expands the small sparse payload into normal RAM and validates an FNV-1a checksum before exposing any sector. This keeps the Legacy BIOS kernel below the VGA aperture while preserving the complete final filesystem contents.

## Verification

The release gate must prove all of the following from the exact source commit:

- the embedded image is generated only after package and repository seeding completes;
- deterministic `ZLIVE003` compression and byte-identical regeneration;
- compressed size is smaller than the final sparse payload;
- kernel size below the verified 512 KiB loader ceiling and at least 1024 bytes of measured headroom below `0xA0000`;
- deterministic ISO rebuild;
- BIOS El Torito metadata and `/BOOT/ZENOVOS.IMG` presence;
- QEMU boot with no virtual hard disk attached;
- automatic ZenovFS mount, security initialization and desktop readiness;
- successful Live-session write, read, `fsck` and `sync` operations;
- publication of exactly one release asset;
- unauthenticated public download and byte-for-byte verification of that ISO.

## Compatibility boundary

UEFI boot and installation to physical hardware are not implemented. VirtualBox still needs AMD-V/VT-x to be enabled and available on the host; a guest ISO cannot change host firmware settings. QEMU TCG can execute the ISO without hardware virtualization, with lower performance.
