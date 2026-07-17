# ZenovGuard persistent audit journal for ZenovOS 0.1.1

ZenovGuard records security decisions in `/security/zenovguard.audit`. The journal is designed to survive reboot, detect structural corruption and make retained-record modification, deletion or reordering evident through a SHA-256 chain.

## ZGAL1 format

The canonical file is exactly 8,288 bytes:

```text
Header (96 bytes)
├── magic: ZGAL
├── schema: 1
├── header and record sizes
├── capacity: 64 records
├── retained record count
├── next ring index
├── next monotonic sequence
├── anchor hash
├── current head hash
└── zero reserved bytes

Records (64 × 128 bytes)
├── monotonic sequence
├── PIT tick
├── action: BOOT / SCAN / EXEC / QUARANTINE
├── verdict
├── canonical path length and zero-padded path
├── full SHA-256 object digest
├── SHA-256 record hash
└── zero reserved bytes
```

Every record hash commits to:

```text
"ZENOV-AUDIT-V1\0\0"
|| previous record hash
|| canonical little-endian metadata
|| fixed 48-byte path field
|| full object digest
```

The domain string prevents the same hash construction from being confused with an unrelated ZenovOS structure.

## Bounded ring and anchor

The journal retains 64 records. Before the oldest slot is overwritten, its record hash becomes the header anchor. Verification begins from that anchor and walks the retained records in sequence order to the current head hash.

The anchor preserves cryptographic continuity for the retained window while keeping disk usage and verification cost bounded. It does not preserve the text of records that have rotated out.

## Boot contract

Boot performs these checks before normal application execution is available:

1. ZenovFS checksum validation;
2. exact 8,288-byte file size;
3. magic, schema, capacities and reserved-zero validation;
4. canonical empty-state or ring-state validation;
5. monotonic sequence validation;
6. canonical path and record validation;
7. complete SHA-256 chain replay;
8. final replayed hash equality with the stored head hash.

Failure is fatal during boot. The operating system does not silently discard or recreate a malformed existing journal.

A factory image contains a canonical empty journal. ZenovGuard appends the boot-baseline event after the trust baseline has been verified.

## Append and crash behavior

Each BOOT, SCAN, EXEC and QUARANTINE event updates the in-memory journal and writes the complete journal through ZenovFS1 copy-on-write replacement.

ZenovFS writes the payload to a staging slot, commits replacement metadata and then retires the previous slot. Consequently, an interrupted update resolves to either the previous complete journal or the new complete journal; header and record payload are not updated independently.

If append fails, ZenovGuard restores the previous in-memory header and record slot, marks the persistent journal unavailable and locks further application execution. Audit failure is therefore not treated as a harmless logging error.

## Protected path

`/security/zenovguard.audit` cannot be modified through ordinary shell or userspace operations. Write, remove, rename and copy-over attempts are blocked by the same protected-path boundary used for trusted applications and active ZGDB state.

Internal journal writes bypass those wrappers only inside the audit subsystem.

## Commands

```text
guard status
guard log
guard log verify
```

`guard status` reports capacity, next sequence, replay state and head-hash prefix. `guard log` prints the persistent retained window followed by the current volatile session mirror. `guard log verify` re-reads the exact disk file and compares the verified disk head to the active in-memory state.

## Independent host verification

`tools/zenovfs_audit_verify.cpp` independently parses a ZenovFS image and verifies both the ZenovFS FNV checksum and the ZGAL1 SHA-256 chain.

The QEMU gate verifies the non-empty runtime image and emits a negative image by changing journal payload and recomputing the ZenovFS checksum. This proves that the filesystem checksum accepts the modified payload while the independent audit-chain verifier rejects it.

Expected evidence includes:

```text
ZENOV_GUARD_AUDIT_SELFTEST_OK
ZENOV_GUARD_AUDIT_REPLAY_OK count=<n> next=<n+1>
ZENOV_GUARD_AUDIT_READY
ZENOV_GUARD_AUDIT_VERIFY_OK
persistent audit host verification: OK
```

The second QEMU boot must replay a non-empty journal from the first phase.

## Security boundary

ZGAL1 is tamper-evident, not externally authenticated.

It detects accidental corruption and unauthorized runtime mutation that does not also rewrite every affected record, anchor, head and ZenovFS checksum. It also detects modification, deletion, insertion, reordering and sequence discontinuity within the retained window.

An offline attacker with complete write access to the data image can construct a new internally consistent journal and recompute its filesystem checksum. Detecting that attack requires a secret MAC key protected outside writable storage, TPM/NVRAM-sealed state, remote witnessing or another externally anchored head hash.

The journal also cannot prove the contents of records that have already rotated out of the bounded 64-record window. Long-term archival requires export to a trusted external collector.
