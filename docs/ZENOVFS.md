# ZenovFS1 persistent volume

ZenovOS 0.1.1 boots from a read-only FAT12 floppy image and mounts a separate
ATA PIO disk at `/data`. The separation keeps the verified boot path immutable
while allowing user files, configuration and native applications to survive a
reboot.

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

Every file record contains a used flag, type, normalized absolute path, file
length and FNV-1a payload checksum. Names are case-sensitive, so `Notes.txt` and
`NOTES.TXT` are distinct paths.

## Seeded 0.1.1 tree

```text
/data
├── apps
│   ├── hello.zex
│   └── fileio.elf
├── config
│   └── system.ini
└── docs
    ├── readme.txt
    └── release.txt
```

`system.ini` is a persistent configuration seed rather than a compile-time file.
The current kernel exposes it through normal VFS operations; automatic setting
application will be expanded in a later release.

## Supported operations

```text
mount
df
fsck
sync
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

The host-side `zenovfs-verify` tool validates:

- exact image geometry and superblock fields;
- entry-table capacity;
- unique printable absolute paths;
- valid parent directories;
- supported entry types;
- slot bounds;
- every file checksum;
- required 0.1.1 seed files.

The kernel `fsck` command independently repeats metadata, parent, duplicate and
payload-checksum validation through the ATA driver. `sync` writes the superblock
and complete entry table and advances the generation counter.

CI tests two independent QEMU processes using the same runtime data disk. The
first boot creates a shell file and runs `FILEIO.ELF`, which creates a second file
through userspace syscalls. The second boot reads both files and performs another
kernel `fsck`.

The pristine release data image is copied before mutation, so deterministic
build verification is not contaminated by the persistence test.

## Current limitations

ZenovFS1 remains a bounded recovery/development filesystem:

- fixed file slots rather than dynamic extents;
- no crash journal or atomic multi-entry transaction;
- no permissions, ownership or timestamps;
- no sparse files, links or mount namespaces;
- one mounted writable device;
- ATA PIO only, without DMA.

A future general-purpose storage layer can add partition tables, dynamic block
allocation and FAT16 or another filesystem without changing the immutable FAT12
boot path.
