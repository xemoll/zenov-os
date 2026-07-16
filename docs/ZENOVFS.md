# ZenovFS1 persistent volume

ZenovOS 0.1.1 boots from a read-only FAT12 system image and mounts a separate
ATA PIO disk at `/data`. Revision 2 adds a mandatory full integrity pass before
persistent configuration or applications may consume the mounted volume.

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
| reserved | 17–31 | future metadata/recovery records |
| data slots | 32 onward | one fixed 64 KiB slot per entry |

Every file record contains a used flag, type, normalized absolute path, file
length and FNV-1a payload checksum. Names are case-sensitive.

## Seeded Revision 2 tree

```text
/data
├── apps
│   ├── hello.zex
│   ├── fileio.elf
│   └── fault.elf
├── config
│   └── system.ini
└── docs
    ├── readme.txt
    └── release.txt
```

`system.ini` is now active runtime configuration. The kernel reads its console
`theme` after the boot-time filesystem check; the shipped image selects
`graphite`. Invalid values or an unavailable volume fall back to compiled
defaults and emit a serial diagnostic.

## Supported operations

```text
mount  df  fsck  sync  pwd  cd  ls
mkdir  touch  write  append  cat  stat
cp  mv  rm
```

Directories must be empty before removal. A directory containing descendants
cannot currently be renamed. File writes are limited to 64 KiB per entry.

## Integrity model

The host-side `zenovfs-verify` tool validates:

- exact image size, magic, geometry and entry-table capacity;
- unique printable absolute paths and valid parent directories;
- supported entry types and file-slot bounds;
- every complete file checksum;
- the required Revision 2 seed tree.

`make check` also mutates a copy of the image and requires the host verifier to
reject it.

The kernel performs the same class of checks through ATA PIO:

1. `mount()` validates the superblock and loads the entry table.
2. `verify_boot_integrity()` walks all used entries, validates metadata/parents,
   reads every file and verifies every payload checksum.
3. Only a successful pass emits `ZENOVFS_BOOT_FSCK_OK` and permits settings or
   applications to use `/data`.
4. A failure emits `ZENOVFS_BOOT_FSCK_FAILED`, clears the mounted state and boots
   the console in degraded mode with built-in configuration.
5. Manual `fsck` repeats the check while the volume is online.

The degraded QEMU scenario leaves the superblock and metadata structurally valid
but mutates a seeded payload byte. This proves that quarantine is caused by the
full checksum pass rather than only by mount rejection.

## Persistence verification

CI uses the same runtime disk in two independent QEMU processes:

- the shell writes `PERSIST.TXT`;
- `FILEIO.ELF` creates `/data/apps/userio.txt` through syscalls;
- QEMU exits completely;
- a second QEMU process reads both payloads and runs `fsck` again.

A third QEMU process uses the payload-corrupted disk and must still reach a
working console with `/data` disabled. Any kernel panic fails the workflow.

The pristine release image is never mutated by runtime tests; CI works on copies,
so deterministic build comparison remains meaningful.

## Sync behavior

`sync` writes the superblock and full entry table and advances the generation
counter. Individual file mutations also flush the affected entry after payload
I/O. This is not a journal or a transactional filesystem.

## Current limitations

ZenovFS1 remains a bounded development/recovery filesystem:

- fixed file slots rather than dynamic extents;
- no crash journal or atomic multi-entry transaction;
- no permissions, ownership or timestamps;
- no sparse files, links or mount namespaces;
- one mounted writable device;
- ATA PIO only, without DMA.

A future general-purpose storage layer can add partition tables, dynamic block
allocation and FAT16 or another filesystem without changing the immutable FAT12
boot path.
