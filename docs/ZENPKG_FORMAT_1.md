# ZenPkg container format 1

ZenPkg is the deterministic package container used by ZenovOS 0.1.1. The format is shared by the host-side `zenpkg` tool and the bounded in-kernel native package verifier.

## Scope

Format 1 supports metadata and payload integrity, deterministic construction, strict parsing, exact target/capability resolution and safe extraction. It does not itself prove publisher identity. ZenovOS 0.1.1 therefore installs only packages admitted by its compiled bootstrap catalog; network repositories remain disabled.

## Binary layout

All integer fields are little-endian. The fixed header is 128 bytes.

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 8 | `ZENPKG1\0` magic |
| 8 | 4 | format version, currently `1` |
| 12 | 4 | flags, currently `0` |
| 16 | 8 | canonical manifest length |
| 24 | 8 | payload length |
| 32 | 32 | SHA-256 of the canonical manifest |
| 64 | 32 | SHA-256 of the payload |
| 96 | 32 | SHA-256 of header bytes `0..95` |
| 128 | variable | canonical manifest bytes |
| ... | variable | payload bytes |

A verifier rejects trailing bytes, impossible lengths, unknown flags, unsupported versions and every digest mismatch.

## Canonical manifest

The manifest is UTF-8-compatible printable ASCII using one `key=value` record per line and a final newline. Carriage returns, NUL bytes, empty records, duplicate scalar fields and unknown fields are rejected.

Scalar keys appear exactly once and in this order:

```text
format
name
version
architecture
target
kind
entrypoint
payload_type
runtime
min_os
license
source
asset_policy
```

Repeated `capability`, `dependency` and `conflict` records follow in that order and are lexicographically increasing without duplicates.

The 0.1.1 in-kernel native profile requires:

```text
format=zenpkg-manifest-1
architecture=i686
target=i686-zenov-none
kind=application
runtime=native
min_os<=0.1.1
asset_policy=redistributable
```

`payload_type` is `zex1` or `elf32`. The entrypoint is immutable and versioned:

```text
/data/apps/pkg-<name>-<version>.zex
/data/apps/pkg-<name>-<version>.elf
```

ZEX1 packages require `abi.zex1.v1`, `kernel.ring3` and `storage.zenovfs1`. ELF32 packages require `abi.elf32.i386.static`, `kernel.ring3` and `storage.zenovfs1`.

Dependencies are exact `name@version` requirements. Conflicts are exact package names. The 0.1.1 database stores at most four dependencies and four conflicts per package and eight installed package records.

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

`pack`, `index` and canonical parsing are deterministic. Unsafe output paths and path traversal are rejected.

## ZenovOS 0.1.1 authorization

Container verification is necessary but not sufficient for installation. The bootstrap installer additionally requires an exact catalog match over package name, version, versioned entrypoint, payload type, dependency/conflict shape, full-package SHA-256 and payload SHA-256.

This prevents a writable shell user from converting an arbitrary self-hashed executable into trusted code. A structurally valid but non-catalog package may be inspected with `pkg verify`, but `pkg install` rejects it.

Before every installed payload enters ring 3, the final loader read passes through:

```text
ZenovFS checksum-valid read
  -> signed ZGDB threat/revocation precheck
  -> ZEX1/ELF32 structural and W^X validation
  -> active ZPKDB1 reference re-authorized against compiled catalog
  -> path + full-package digest + payload SHA-256 match
  -> persistent ZenovGuard audit append
  -> loader mapping
```

## Limits

ZenovFS1 limits each package and installed payload to one 64 KiB slot. Format 1 has no embedded publisher signature, transparency proof, timestamp role or repository delegation. Those are repository-layer concerns for the future signed metadata implementation.
