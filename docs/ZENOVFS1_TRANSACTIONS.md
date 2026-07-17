# ZenovFS1 interrupted-write protocol

ZenovOS 0.1.1 improves single-file replacement durability without changing the ZenovFS1 superblock or 64-byte entry layout. A free slot is used as transaction staging.

## Write order

1. Validate path, parent, size and a free staging slot.
2. Build the complete final payload in memory.
3. Write all payload sectors into the staging slot.
4. Write a type-3 staging entry containing path, size, checksum and the old entry index.
5. Rewrite the staging entry as a committed type-1 file. This is the logical commit point.
6. Clear old metadata when replacing an existing file.
7. Clear the committed flag and old-index field in the new entry.

## Recovery

- A remaining type-3 entry is uncommitted and is discarded.
- A type-1 entry with the committed flag is authoritative.
- Its old entry is cleared if still present.
- Transaction fields in the committed entry are then cleared.

The host fault harness evaluates every sector-write prefix and accepts only complete old content or complete new content with the correct checksum. QEMU separately boots an intentionally interrupted committed image, requires recovery, reads the committed payload and runs kernel `fsck`.

Required evidence:

```text
ZENOVFS_FAULT_INJECTION_OK
ZENOVFS_OLD_OR_NEW_CONTENT_ONLY
ZENOVFS_INTERRUPTED_WRITE_RECOVERED
ZENOVFS_FSCK_OK
```

ZenovFS1 remains a 128-entry fixed-slot filesystem with a 64 KiB maximum per file. Multi-object transactions, variable extents and an incompatible journal belong in ZenovFS2.
