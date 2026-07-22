# ZenovOS 0.1.1 ZenovFS mount-hardening contract

ZenovFS1 disk images are untrusted input. The kernel validates the complete superblock geometry and every metadata entry before copying variable-size metadata, allocating a file buffer, recovering transactions, or exposing the filesystem to higher layers.

## Superblock validation

The shared kernel/host validator requires exact `ZENOVFS1` magic and schema, a bounded device size compatible with LBA28, one to 128 entries, the exact number of metadata sectors, non-overlapping metadata and data regions, fixed slots no larger than 64 KiB, sufficient data sectors for every slot, a non-zero generation, a canonical printable label, and zero reserved bytes.

The validator performs division-based capacity checks instead of unchecked multiplication. Therefore malformed `entry_sectors`, `data_start`, `slot_sectors`, entry counts, or device sizes cannot overflow arithmetic or drive writes beyond the static 8 KiB metadata array.

## Entry validation

Every unused entry must be completely zero. Used entries require a known type, canonical zero-padded absolute path, no control characters, no `.` or `..` components, no empty components, no trailing slash, and no more than the runtime normalizer's 16 components. Directories must have zero file metadata. Files are bounded by one slot. Parent directories must exist. Duplicate paths and forged/shared transaction references are rejected except for the exact old/new pairs produced by the ZenovFS1 copy-on-write protocol.

The same checks run again after interrupted-write recovery. Recovery may delete an old entry only when its type, flags, and canonical path match the committed replacement.

## Failed-read disclosure boundary

A sector or checksum failure zeros every disk byte already copied to the destination before returning an error, and the returned size remains zero. A boot self-test corrupts one in-memory checksum, proves rejection and complete scrubbing, restores the checksum, and proves the normal read still succeeds.

## Evidence

`tests/zenovfs_mount_validation_test.cpp` includes the production validator verbatim and executes 24 deterministic cases under AddressSanitizer, UndefinedBehaviorSanitizer, unsigned-overflow, and implicit-integer-conversion instrumentation.

`tools/zenovfs_mount_corrupt.cpp` emits seven malformed disk images. `tests/qemu_zenovfs_mount_reject.sh` boots each image and requires one exact `ZENOVFS_MOUNT_REJECTED` reason, a kernel panic, and absence of `ZENOVOS_UI_READY`.

The normal strict build, 1,662-case ZGAL1 audit COW matrix, 3,173-case ZenPkg transport matrix, full security/recovery QEMU lifecycle, and deterministic rebuild run on the same source revision.
