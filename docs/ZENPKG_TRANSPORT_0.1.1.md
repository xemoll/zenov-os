# ZenPkg transfer and recovery contract — ZenovOS 0.1.1

## Scope

This layer moves an already authorized ZenRepo target into the persistent ZenPkg cache. It is a bounded transport state machine and recovery protocol, not a new source of trust.

The current provider is the signed offline repository under `/data/packages`. DNS, TCP/IP, HTTP and TLS are not implemented and must not be reported as available.

## Trust boundary

The active ZenRepo target supplies the expected:

- package name and version;
- entrypoint and payload type;
- exact package length;
- full package SHA-256;
- payload SHA-256;
- signed syscall policy.

Transport bytes, `.part` objects and the transport journal remain untrusted hints. They cannot authorize installation. A package is exposed only after the complete object matches current signed metadata and the final `.zpk` is verified after atomic rename.

## Persistent journal

ZenPkg stores one active transfer in:

`/var/lib/zenpkg/transport.v1`

The packed `ZPTRN1` journal records:

- schema and monotonic local generation;
- phase: `downloading`, `ready` or `failed`;
- offline provider identifier;
- package name and version;
- exact expected length;
- full signed package digest;
- trusted source and digest-addressed partial paths;
- committed offset;
- retry state;
- SHA-256 of the committed prefix;
- checksum of the complete 248-byte journal.

The path is protected from ordinary shell and userspace write/remove operations. Journal generation is an ordering and recovery field, not cryptographic anti-rollback state. Trust still comes from ZenRepo metadata and full package verification.

## Recovery invariants

At boot ZenPkg:

1. validates journal magic, schema, checksum, field termination, provider, phase and bounds;
2. resolves the exact target against the currently trusted repository;
3. derives source, partial and final paths again instead of trusting arbitrary journal paths;
4. checks the real `.part` length;
5. compares the stored partial SHA-256 with the same prefix of the verified source;
6. reconciles journal offset to the durable ZenovFS object after an interrupted commit window;
7. accepts `ready` only when the complete partial verifies as the signed target;
8. clears a stale journal when the final verified `.zpk` already exists;
9. resets invalid or no-longer-authorized state without activating any bytes.

## State machine

1. Resolve a target from verified repository metadata.
2. Reuse a valid final cache object when present.
3. Verify the provider source against the signed target.
4. Create or recover the `ZPTRN1` journal.
5. Inspect the digest-addressed `.part` object.
6. Reject a partial object when type, length or prefix digest is invalid.
7. Append at most 512 bytes per ZenovFS transaction.
8. Synchronize metadata after every committed chunk.
9. Re-read and hash every committed prefix.
10. Persist and read back the updated journal.
11. Never allow the partial length to exceed the signed target length.
12. Retry a failed chunk at most three consecutive times.
13. Mark exhausted transfers `failed` instead of pretending they completed.
14. Verify package format, package SHA-256, payload SHA-256 and target identity.
15. Mark the journal `ready` only after complete verification.
16. Atomically rename `.part` to `.zpk`.
17. Verify the final cache object again.
18. Remove the journal only after the final object is durable and verified.

## Commands

- `pkg fetch <name>` performs the complete transfer.
- `pkg transport status` reports persistent phase, package, offset and generation.
- `pkg transport step <name>` commits at most one 512-byte chunk and returns with a resumable journal.
- `pkg transport resume <name>` continues the active target to verified completion.
- `pkg transport cancel` removes the authorized target partial and its journal.

Only one transfer may be active because ZenovOS 0.1.1 still has one foreground process and a single package scratch buffer. A request for another target while a journal is active fails with `ZENPKG_TRANSPORT_BUSY`.

## Evidence markers

The serial log emits:

- `ZENPKG_TRANSPORT_BEGIN`;
- `ZENPKG_TRANSPORT_ADOPTED_PARTIAL`;
- `ZENPKG_TRANSPORT_CHUNK_COMMIT`;
- `ZENPKG_TRANSPORT_PAUSED`;
- `ZENPKG_TRANSPORT_JOURNAL_RECOVERED`;
- `ZENPKG_TRANSPORT_JOURNAL_RECONCILED`;
- `ZENPKG_TRANSPORT_RESUME`;
- `ZENPKG_TRANSPORT_RETRY`;
- `ZENPKG_TRANSPORT_COMPLETE`;
- `ZENPKG_TRANSPORT_JOURNAL_CLEARED`;
- `ZENPKG_TRANSPORT_RETRY_EXHAUSTED` on fail-closed exhaustion.

The dedicated QEMU recovery test uses one persistent data image across three boots:

1. commit exactly one 512-byte chunk of the 647-byte signed package and terminate QEMU;
2. recover `offset=512`, resume with the remaining 135 bytes, verify, rename and clear the journal;
3. confirm that journal cleanup and cache cleanup survive another reboot.

The workflow also compiles the journal-format regression under ASan and UBSan and rejects checksum corruption, illegal offsets, incomplete `ready` state, unknown providers, zero generation, oversized targets and unterminated fields.

## Security properties

- `.part` append access is restricted to the package-manager cache writer.
- `/var/lib/zenpkg/transport.v1` is a protected package-manager path.
- Ordinary shell and userspace storage operations remain blocked for `/var/cache/zp`.
- Resume cannot authorize a package.
- Corrupt or mismatched partial prefixes are reset rather than continued.
- Crash-window reconciliation can move the journal only to a prefix that is already durable and matches the verified source.
- A short digest in the cache filename is only a storage key; full SHA-256 verification remains mandatory.

## Remaining work

A network provider must implement the same bounded read contract and preserve all journal invariants. Before it can be marked available, ZenovOS still needs a real network stack, DNS resolution, TLS certificate and hostname validation, HTTP response framing, strict `Range`/`Content-Range` validation, timeout accounting, mirror identity and retry/backoff policy.
