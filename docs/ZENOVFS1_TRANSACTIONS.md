# ZenovFS1 copy-on-write transaction protocol

ZenovOS 0.1.1 improves file replacement durability without changing the ZenovFS1 superblock or 64-byte directory-entry layout. It uses an otherwise free file slot as temporary transaction storage.

## Existing on-disk fields

A ZenovFS1 entry remains 64 bytes:

```text
uint8_t  used
uint8_t  type
uint16_t flags
char     path[48]
uint32_t size
uint32_t checksum
uint32_t reserved
```

Normal entries use type `1` for files and type `2` for directories. Type `3` is the 0.1.1 transaction staging state. Flag bit `0x0001` marks a committed replacement whose old slot still requires cleanup. For a replacement, `reserved` stores the old entry index until cleanup is complete.

## Replacement write order

For a new or replacement file:

1. validate the path, parent, size and availability of a free staging slot;
2. construct the complete final payload in the kernel file buffer;
3. write all payload sectors to the staging slot;
4. write a type-3 staging metadata entry containing path, size, checksum and old index;
5. rewrite that staging entry as a type-1 file with the committed flag;
6. clear the old metadata entry when replacing an existing file;
7. clear the committed flag and old-index field in the new entry.

Step 5 is the logical commit point. Payload is never exposed under the target path before its complete checksum-bearing metadata is committed.

## Recovery rules

During mount and before integrity/sync operations:

- an entry still in type-3 staging state is uncommitted and is discarded;
- a type-1 entry with the committed flag is the new authoritative file;
- if its stored old index still contains an entry, that old entry is cleared;
- the new entry's committed flag and old-index field are then cleared.

Recovery emits:

```text
ZENOVFS_INTERRUPTED_WRITE_RECOVERED
```

## Failure outcomes

For every prefix of the sector-write plan, post-recovery lookup must produce exactly one of:

- the complete old file with its original checksum; or
- the complete new file with its final checksum.

A partial payload, duplicate authoritative path or metadata/payload checksum mismatch is a test failure.

The host test `tools/zenovfs_fault_test.cpp` enumerates every write boundary. It also emits a deliberately interrupted committed image. The QEMU recovery phase boots that image, requires kernel recovery, reads `recovery=committed` from the recovered file and runs kernel `fsck`.

Required evidence:

```text
ZENOVFS_FAULT_INJECTION_OK
ZENOVFS_OLD_OR_NEW_CONTENT_ONLY
ZENOVFS_INTERRUPTED_WRITE_RECOVERED
ZENOVFS_RECOVERY_IMAGE_OK
ZENOVFS_FSCK_OK
```

## Scope and limitations

This protocol improves atomic replacement of one fixed-slot file. It does not provide concurrent transactions, directory-tree atomicity, write ordering guarantees across arbitrary hardware caches, variable extents, wear leveling or a general journal.

ZenovFS1 still contains 128 fixed metadata entries and 64 KiB payload slots. A different allocation model or incompatible journal belongs in a versioned ZenovFS2 migration, not a silent 0.1.1 format change.
