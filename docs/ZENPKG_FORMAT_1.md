# ZenPkg container format

ZenPkg is the deterministic package container used by ZenovOS 0.1.1. The same format is consumed by the dependency-free host tool and the bounded in-kernel verifier.

## Binary layout

All integer fields are little-endian. The fixed header is 128 bytes.

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 8 | `ZENPKG1\0` magic |
| 8 | 4 | schema, currently `1` |
| 12 | 4 | flags, currently `0` |
| 16 | 8 | canonical manifest length |
| 24 | 8 | payload length |
| 32 | 32 | SHA-256 of canonical manifest |
| 64 | 32 | SHA-256 of payload |
| 96 | 32 | SHA-256 of header bytes `0..95` |
| 128 | variable | canonical manifest |
| ... | variable | payload |

Verification rejects trailing bytes, impossible lengths, unsupported flags or schema values, malformed text and every digest mismatch.

## Canonical manifest

The manifest contains printable ASCII `key=value` records and a final newline. Scalar keys appear exactly once in canonical order. Repeated capabilities, dependencies and conflicts are sorted, duplicate-free and placed after scalar fields. Unknown fields, carriage returns, NUL bytes and non-canonical records are rejected.

The native 0.1.1 profile requires i686 Zenov target metadata, a native runtime, a ZEX1 or static ELF32 payload, a versioned `/data/apps/pkg-*` entrypoint and redistributable assets. Dependencies use exact `name@version` requirements. Conflicts use exact package names.

## Host commands

```text
zenpkg pack
zenpkg verify
zenpkg inspect
zenpkg extract
zenpkg resolve
zenpkg index
zenpkg manifest-check
zenpkg hash
```

Construction and indexing are deterministic. Extraction rejects absolute paths, traversal and unsafe output names.

## Authorization

A valid container is not automatically trusted. Installation additionally requires a match against verified signed repository metadata over:

- package name and semantic version;
- immutable entrypoint and payload type;
- exact package byte length;
- complete package SHA-256;
- payload SHA-256;
- least-privilege syscall mask and exact path scopes.

A structurally valid package that is absent from signed targets may be inspected, but installation and execution are rejected.

## Limits

ZenovFS1 limits a package and its installed payload to one 64 KiB slot. Publisher authorization, delegation, expiration and anti-rollback are repository-layer responsibilities rather than container fields. Network transport is not enabled in ZenovOS 0.1.1.
