# ZenovOS 0.1.1 signed ransomware and controlled-folder policy

## Scope

ZenovOS 0.1.1 adds `ZRWP1`, a fourth independent signed security-policy domain. It protects selected persistent paths from unauthorized or excessive mutation by ring-3 applications. The mechanism is designed for the current single-foreground-process architecture and is intentionally bounded, deterministic and fail-closed.

The four policy domains have separate roots and authority:

```text
ZGDB2  executable trust, threat records and revocation
ZCAP1  syscall masks and exact file scopes
ZMID1  malware hashes, bounded byte patterns and scan actions
ZRWP1  protected folders, exact writer identities and mutation budgets
```

A ZRWP update cannot authorize an executable, grant a syscall or add malware intelligence. A trusted application must first pass ZGDB2 and ZCAP1 before ZRWP can identify it as a writer.

## Security objective

The policy constrains ransomware-like write behavior that may not match a known signature. It provides:

- signed protected-path definitions;
- exact writer path and executable SHA-256 binding;
- independent write, rename and remove budgets;
- a byte budget for write operations;
- a bounded PIT-tick accounting window;
- `audit` and `block` modes;
- writer lockout for the remainder of a violated block-mode window;
- persistent ZGAL1 evidence before an audit-mode allow or block decision is completed;
- fail-closed policy parsing, update and boot behavior.

This complements ZMID1. ZMID1 asks whether bytes match known intelligence. ZRWP1 asks whether a specific verified application is allowed to mutate a protected path at the observed rate.

## Canonical binary format

A ZRWP1 object is:

```text
96-byte Header
N × 96-byte Record, 1 <= N <= 16
256-byte RSA-PSS signature
```

Header fields:

```text
magic                 "ZRWP"
schema                1
header_size           96
policy_version        monotonic u32
minimum_engine        0x00000101 for checked fixtures
mode                  0 audit / 1 block
record_count          1..16
payload_size          record_count × 96
window_ticks          non-zero PIT-tick window
max_writes            non-zero per-writer budget
max_renames           non-zero per-writer budget
max_removes           non-zero per-writer budget
max_bytes             non-zero write-byte budget
payload_sha256        SHA-256 over all records
key_id                8-byte root identifier
reserved              eight zero bytes
```

Record fields:

```text
type                  PROTECTED_PATH or WRITER
operations            WRITE / RENAME / REMOVE bitmask
path_length           canonical path length
path                  zero-terminated, zero-padded 48-byte field
digest                zero for protected paths;
                      exact executable SHA-256 for writers
reserved              twelve zero bytes
```

The parser rejects unknown modes, operation bits or record types; missing path terminators; non-zero padding; duplicate protected or writer identities; missing writer digests; non-zero protected-record digests; unsupported engine requirements; invalid sizes; trailing bytes; and non-zero reserved fields.

## Cryptographic root

The active root key ID is:

```text
7186b2bd819e47dc
```

The signature contract is:

```text
RSA-2048
RSASSA-PSS
SHA-256
MGF1-SHA-256
32-byte salt
```

Only the public key is committed. The private key used to create the deterministic regression fixtures is not stored in the repository or release image.

Checked fixture hashes:

```text
ZRWP v1  e8fd9fd9542d2218265ca1300b948d0564b5ab2d4e75a6e43c13498d95caacec
ZRWP v2  6506d1ece8322d72de3ca1a19846662b1622d839068497a17468a4af5a9b42da
root PEM 5c0684c8aa917701473e8ebdb01f31a860b4ed976edb27b207f69bc7ece2b5a9
```

The host gate verifies both positive fixtures with the independent parser and OpenSSL using explicit PSS parameters. Payload-tampered and unknown-key fixtures must fail.

## Checked policy fixtures

Both checked policies contain:

```text
PROTECTED /apps/userio.txt  WRITE
PROTECTED /docs             WRITE | RENAME | REMOVE
WRITER    /apps/fileio.elf  WRITE + exact FILEIO.ELF SHA-256

window_ticks = 10000
max_writes   = 1
max_renames  = 1
max_removes  = 1
max_bytes    = 128
```

Version 1 is `audit`. Version 2 is `block`.

The path matcher accepts the exact protected path or a child separated by `/`. `/docs/file.txt` is protected; `/doc/file.txt` and `/docs-old/file.txt` are not accidental prefix matches.

## Writer identity

A writer identity is not a pathname alone. Authorization requires:

```text
active process capability profile is valid
AND active application path equals the signed WRITER path
AND active application SHA-256 equals the signed WRITER digest
AND the requested operation is present in the WRITER record
```

Authority is obtained from the same active process profile that was installed after final-read executable appraisal. It is cleared at process exit, fault and failed-launch boundaries.

Shell and internal kernel maintenance operations do not impersonate a ring-3 writer. They use the existing kernel-internal path for policy updates, filesystem repair and controlled regression setup. Ordinary userspace file syscalls pass through the same guarded storage wrappers as shell-visible file operations.

## Mutation window

ZRWP maintains a bounded in-memory window for the current application identity:

```text
writer path + digest
window start PIT tick
write count
rename count
remove count
written bytes
locked flag
```

The window resets when:

- the writer identity changes;
- the configured number of PIT ticks expires;
- process authority is cleared and a later application starts a new identity window.

A request violates policy when:

- the exact writer identity is not signed for the operation;
- the actor is already locked;
- the operation count would exceed its signed budget;
- written bytes would exceed the signed byte budget;
- any operation or byte counter would overflow `uint32_t`.

Counters are deliberately volatile. They provide short-window prevention, not a persistent global reputation score. Persistent evidence is stored in ZGAL1.

## Audit and block modes

In `audit` mode, a violation requires a persistent ZGAL1 `RANSOMWARE` record. The operation is allowed only if that append succeeds.

In `block` mode, a violation requires the same persistent record, the operation is denied, and the actor is locked for the rest of the current window. Audit failure also denies the operation.

Representative markers:

```text
ZRWP_AUDIT actor=/apps/fileio.elf target=/apps/userio.txt operation=write reason=write-budget mode=audit
ZRWP_BLOCKED actor=/apps/fileio.elf target=/apps/userio.txt operation=write reason=write-budget mode=block
```

The first authorized write remains below the checked budget. A second write in the same window produces an audit allow under v1 and a denial under v2.

## Update, read-back and rollback

Active state is stored at:

```text
/security/ransomware-policy.zrwp
/security/ransomware-policy.version
```

`guard ransomware-update <path>` accepts only exact version `N+1`.

The update sequence is:

1. validate and RSA-PSS verify the candidate;
2. back up the current active policy bytes and version;
3. read the candidate again from storage and validate it again;
4. replace the active policy through ZenovFS copy-on-write;
5. read the active object back and verify exact version and signature;
6. commit the persistent version state;
7. activate the new in-memory policy;
8. append a persistent `RANSOMWARE-POLICY` audit record;
9. call `sync_metadata()` before reporting success.

If read-back, version commit, audit or synchronization fails, the previous policy and version are restored and synchronized. Failure to restore places ZRWP in a fail-closed unavailable state.

Important markers:

```text
ZRWP_KEY_REJECTED reason=unknown-key
ZRWP_TAMPER_REJECTED reason=payload-digest
ZRWP_ROLLBACK_REJECTED reason=rollback
ZRWP_ATOMIC_UPDATE_OK version=2
ZRWP_UPDATE_ROLLBACK_OK
ZRWP_UPDATE_ROLLBACK_FAILED
```

Without TPM/NVRAM, replacement of the complete kernel and data-image trust state can still roll policy back to the compiled floor. Normal updates and reboots of the same data image reject rollback below persistent state.

## Commands

```text
guard ransomware
guard ransomware-update <signed-zrwp-path>
```

`guard status` also reports the active ZRWP mode, version, protected paths, writer count, window, budgets and audit/block counters.

## Host and QEMU evidence

The independent host model checks:

- v1 audit-to-allow behavior;
- v2 block behavior;
- exact writer digest enforcement;
- per-operation and byte budgets;
- counter-overflow rejection;
- actor lockout;
- window expiry and reset;
- canonical protected-prefix boundaries;
- kernel-internal bypass isolation.

Expected host marker:

```text
ZRWP_HOST_MODEL_OK audit_to_block=yes exact_writer=yes budgets=yes locked_actor=yes window_reset=yes prefix_boundary=yes overflow=blocked
```

The QEMU lifecycle additionally proves:

1. signed v1 audit-mode boot;
2. first FILEIO write allowed;
3. second FILEIO write audited and allowed;
4. wrong-key and payload-tampered update rejection;
5. exact v1 to v2 transition;
6. first FILEIO write allowed after the new window begins;
7. second FILEIO write denied and application failure returned to shell;
8. v1 rollback rejection;
9. v2 block mode survives reboot;
10. a checksum-valid but cryptographically corrupt active ZRWP image panics before `ZENOVOS_UI_READY`;
11. explicit shell `sync`, persistent audit verification and a one-second durability barrier before each normal QEMU shutdown.

The runtime data-image verifier requires active ZMID v2 and ZRWP v2, matching persistent version files, protected quarantine state and the expected blocked/allowed files.

## Explicit limitations

ZRWP1 is a bounded controlled-folder and mutation-budget mechanism. It is not a complete behavioral EDR engine.

It does not currently provide:

- calibrated Shannon-entropy or compression-ratio scoring;
- process-tree correlation or child-process lineage;
- persistent reputation across policy windows;
- multi-process global rate aggregation;
- memory scanning or sandbox detonation;
- cloud classification, URL reputation or automatic sample submission;
- filesystem snapshots or user-facing file restoration;
- TPM/NVRAM-backed policy and counter rollback protection;
- a security-operations backend or remote response channel.

Entropy scoring is intentionally not enabled without a representative corpus, stable false-positive thresholds and a ring-3 regression application whose executable trust and syscall policies can be re-signed. The current checked mechanism prefers exact identity, signed paths and deterministic budgets over an uncalibrated heuristic presented as protection.
