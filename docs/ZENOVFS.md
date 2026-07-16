# ZenovFS1 persistent volume

ZenovOS 0.1.0 boots from a read-only FAT12 floppy image and mounts a separate
ATA PIO disk at `/data`. The separation keeps the verified boot path immutable
while allowing user files and native applications to survive a reboot.

## Device layout

- image size: 16 MiB
- logical sectors: 32,768
- sector size: 512 bytes
- block device: primary-master ATA PIO (`/dev/ata0`)
- mount point: `/data`
- filesystem magic: `ZENOVFS1`
- format version: 1

## On-disk structure

| Region | Sectors | Purpose |
|---|---:|---|
| superblock | 0 | format, geometry, label and generation |
| entry table | 1–16 | 128 fixed 64-byte file/directory records |
| reserved | 17–31 | future metadata and recovery records |
| data slots | 32 onward | one fixed 64 KiB slot per entry |

Every file record contains:

- used flag;
- file or directory type;
- normalized absolute path up to 47 bytes;
- file length;
- FNV-1a payload checksum.

## Supported operations

The current VFS surface provides:

```text
mount
df
pwd
cd <path>
ls [path]
mkdir <path>
touch <file>
write <file> <text>
append <file> <text>
cat <file>
stat <path>
cp <source> <destination>
mv <source> <destination>
rm <path>
```

Directories must be empty before removal. A directory containing descendants
cannot currently be renamed. File writes are limited to 64 KiB per entry.

## Integrity and persistence

- Every complete file read verifies the stored checksum.
- Metadata updates are written back through ATA PIO and flushed to the device.
- CI writes `PERSIST.TXT`, exits QEMU, starts a second QEMU process with the same
  runtime disk and verifies the file and checksum again.
- The pristine release data image is copied before mutation, so deterministic
  build verification is not contaminated by the persistence test.

## Why this is not FAT16 yet

ZenovFS1 is a deliberately bounded first writable filesystem. It allowed the
block-device, VFS, persistence and application-loader contracts to be verified
without moving BIOS FAT routines into the protected-mode kernel.

A later general-purpose disk layer can add partition tables and FAT16 as a
second filesystem driver. ZenovFS1 remains useful as a deterministic recovery
and test volume.

## Current limitations

- fixed file slots rather than dynamic extents;
- no crash journal;
- no permissions, ownership or timestamps;
- no sparse files or symbolic links;
- one mounted writable device;
- ATA PIO only, without DMA.

These limitations are explicit format constraints, not claimed production
features.
