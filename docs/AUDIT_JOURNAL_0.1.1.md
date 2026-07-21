# ZenovGuard persistent audit journal for ZenovOS 0.1.1

ZenovGuard records security decisions in `/security/zenovguard.audit`. The journal survives reboot, detects structural corruption and makes retained-record modification, deletion or reordering evident through a SHA-256 chain.

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
├── action: BOOT / SCAN / EXEC / QUARANTINE / WRITE-BLOCK / INTELLIGENCE-UPDATE / RANSOMWARE / RANSOMWARE-POLICY
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

Each BOOT, SCAN, EXEC, QUARANTINE, WRITE-BLOCK, INTELLIGENCE-UPDATE, RANSOMWARE and RANSOMWARE-POLICY event updates the in-memory journal and writes the complete journal through ZenovFS1 copy-on-write replacement.

The normal write order is:

```text
17 payload sectors
transaction metadata
committed replacement metadata
old-entry retirement
final committed-flag cleanup
```

An ordered power loss before the commit point recovers the previous complete journal. An ordered power loss after the commit point recovers the new complete journal. Torn, corrupted, missing or incorrectly reordered writes may instead produce an invalid journal; that state is rejected explicitly rather than being accepted as a third valid history.

If append fails at runtime, ZenovGuard restores the previous in-memory header and record slot, marks the persistent journal unavailable and locks further application execution. Audit failure is therefore not treated as a harmless logging error.

## Exhaustive COW fault model

`tools/zenovfs_audit_fault_test.cpp` applies the actual ZenovFS1 entry and data-slot layout to two journal transitions:

- factory-empty journal to one retained record;
- full 64-record ring to a rotated ring with a new anchor.

For each transition it evaluates:

```text
every ordered crash prefix
head and tail torn-sector writes at 1, 64, 128, 255, 256, 384 and 511 bytes
deterministic garbage sectors
every single dropped write
every single duplicated write
every adjacent write swap
all 24 permutations of the four metadata writes against every payload prefix
```

The only accepted classifications are:

```text
OLD          previous journal verifies
NEW          replacement journal verifies
FAIL-CLOSED  no unique checksum-valid and chain-valid journal is available
```

Any internally valid journal different from both the exact old and exact new state is a test failure.

The verified matrix contains 1,662 cases:

```text
empty-to-one       831 cases: old=298 new=370 fail-closed=163
full-ring-rotation 831 cases: old=298 new=98 fail-closed=435
```

The larger fail-closed set during rotation is expected: every retained record and the anchor participate in the new chain, so incomplete payload delivery cannot be treated as a valid replacement.

## Cross-verifier contract

The host verifier and fault generator share `tools/zenov_audit_format.hpp`, which provides the canonical structures and SHA-256 implementation. Both tools run the SHA-256 `abc` known-answer test. The fault generator additionally requires a fixed ZGAL1 record-hash vector before producing any image.

This prevents a test generator and its checker from accepting the same incorrect private interpretation of the journal format.

## QEMU crash images

The fault tool emits three full 16 MiB ZenovFS images that are booted by the real kernel:

```text
zenov-data-audit-old-recovery.img
  payload written + transaction metadata
  mount removes staging metadata
  replay: count=0 next=1

zenov-data-audit-new-recovery.img
  payload written + committed replacement metadata
  mount completes replacement
  replay: count=1 next=2

zenov-data-audit-corrupt.img
  committed metadata with one missing payload sector
  boot: ZENOV_GUARD_AUDIT_INVALID
  UI must not become ready
```

This complements the host matrix with the actual ATA/ZenovFS mount recovery and kernel journal replay path.

## Protected path

`/security/zenovguard.audit`, active ZMID/ZRWP state and quarantine metadata cannot be modified through ordinary shell or userspace operations. Write, remove, rename and copy-over attempts are blocked by the same protected-path boundary used for trusted applications and active ZGDB state.

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

The QEMU gate verifies the non-empty runtime image and emits a negative image by changing journal payload and recomputing the ZenovFS checksum. This proves that the filesystem checksum accepts the modified payload while the audit-chain verifier rejects it.

Expected evidence includes:

```text
ZENOV_AUDIT_COW_FAULT_MATRIX_OK
ZENOV_AUDIT_COW_FAULT_INJECTION_OK total_cases=1662
ZENOV_AUDIT_COW_OLD_OR_NEW_OR_FAIL_CLOSED_ONLY
ZENOV_GUARD_AUDIT_REPLAY_OK count=0 next=1
ZENOV_GUARD_AUDIT_REPLAY_OK count=1 next=2
ZENOV_GUARD_AUDIT_INVALID
ZENOV_GUARD_AUDIT_VERIFY_OK
```

## Security boundary

ZGAL1 is tamper-evident, not externally authenticated.

It detects accidental corruption and unauthorized runtime mutation that does not also rewrite every affected record, anchor, head and ZenovFS checksum. It also detects modification, deletion, insertion, reordering and sequence discontinuity within the retained window.

The fault matrix is a deterministic software model of ZenovFS sector and metadata writes. It does not prove behavior of physical disks with undocumented caches, firmware bugs, controller-level remapping or dishonest flush completion.

An offline attacker with complete write access to the data image can construct a new internally consistent journal and recompute its filesystem checksum. Detecting that attack requires a secret MAC key protected outside writable storage, TPM/NVRAM-sealed state, remote witnessing or another externally anchored head hash.

The journal also cannot prove the contents of records that have already rotated out of the bounded 64-record window. Long-term archival requires export to a trusted external collector.
