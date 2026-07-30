# Signed policy transaction journal — ZenovOS 0.1.1

## Scope

`ZPTJ1` closes the remaining power-loss gap across the separate persistent objects used by the three runtime-updatable signed trust domains:

- ZGDB2 executable trust and revocation policy;
- ZCAP1 per-application syscall capability policy;
- ZMID1 malware intelligence policy and its durable ZGAL1 update audit.

The previous transaction layer proved individual writes by readback and restored failures observed during the same boot. It could not recover automatically when power was lost after a live policy, version floor or audit object changed but before the update completed.

## Persistent format

The fixed protected file `/security/policy-transaction.journal` is seeded as an empty ZenovFS file. A hot journal contains:

- magic `ZPTJ` and schema 1;
- prepared state and exact trust-domain identifier;
- canonical live, version and optional auxiliary paths;
- previous policy, version and optional audit sizes;
- parsed previous monotonic version;
- exact old bytes for every object in the transaction;
- SHA-256 over the complete header and payload with the digest field zeroed.

A journal is accepted only when every size, path, domain, version and reserved byte is canonical and the digest verifies. An invalid non-empty journal stops boot before audit or signed-policy initialization.

The SHA-256 field is an unkeyed corruption and consistency check, not an authenticity primitive. Runtime modification is prevented by the protected-path boundary. After replay, the normal ZGDB, ZCAP and ZMID signature checks and the ZGAL1 chain validator still run before their domains become ready.

## Runtime protocol

For each ZGDB, ZCAP or ZMID update:

1. Validate the candidate and require the exact checked successor version.
2. Read the current live policy and canonical version bytes.
3. For ZMID, also read the current complete ZGAL1 audit object.
4. Serialize the old generation into ZPTJ1.
5. Write the journal through ZenovFS copy-on-write.
6. Read the journal back and validate its complete structure and SHA-256.
7. Synchronize ZenovFS metadata. No live object may be changed before this succeeds.
8. Re-read and revalidate the candidate.
9. Write and verify the new live policy.
10. Write and verify the new version floor.
11. Synchronize the new policy generation.
12. For ZMID, append the durable intelligence-update audit.
13. Clear the journal by committing a zero-length COW replacement, read it back and synchronize metadata.
14. Activate the new in-memory records only after journal clearance is proven.

A synchronous failure after preparation replays the persistent journal instead of relying only on volatile backup memory.

## Boot recovery

Recovery runs after ZenovFS mount and process-buffer initialization, but before ZGAL1 validation and before all signed policy initialization.

A valid hot journal restores the exact old policy, version and optional audit bytes, verifies every readback, synchronizes metadata, clears and synchronizes the journal, and publishes `POLICY_TRANSACTION_RECOVERY_OK` with the recovered domain.

An empty journal publishes `POLICY_TRANSACTION_JOURNAL_CLEAN`.

A non-empty malformed, noncanonical or digest-invalid journal fails closed with `Persistent signed policy transaction recovery failed.` No audit-ready, policy-ready or UI marker may follow.

## Crash-state matrix

| Crash point | Persistent state before reboot | Boot result |
|---|---|---|
| Before journal synchronization | No proven live mutation | Update reports failure; no activation |
| After prepared journal synchronization | Old live generation + hot journal | Replay old generation and clear journal |
| After policy write | New policy + old version + hot journal | Replay old policy/version |
| After version write | New policy/version + hot journal | Replay old policy/version |
| After policy metadata sync | New durable policy/version + hot journal | Replay old policy/version |
| After ZMID audit append | New policy/version/audit + hot journal | Replay old policy/version/audit |
| During journal clear | Old hot journal or durable empty replacement | Replay once or boot clean; both converge |
| After durable journal clear | New complete generation + empty journal | Boot new generation |

## Verification

The dedicated policy-journal gates include:

- strict GCC host builds;
- Clang ASan, UBSan, unsigned-overflow and implicit-conversion sanitizers;
- real SHA-256 journal validation;
- six simulated crash points;
- independent ZGDB, ZCAP and ZMID prepare/replay coverage;
- canonical empty auxiliary-path coverage for ZGDB and ZCAP;
- nested transaction rejection;
- exact policy/version/audit replay;
- corrupt-journal and corrupt-readback fail-closed tests;
- freestanding `-Werror` kernel build;
- ZenovFS-valid hot and corrupt image generation;
- QEMU hot replay, clean second boot and corrupt-journal fail-closed phases;
- deterministic full rebuild;
- retained binaries, disk images, serial logs and exact source contracts.

## Security boundary

ZPTJ1 provides single-machine crash recovery using the same ZenovFS block device as the protected objects. It does not provide hardware-backed freshness against offline replacement of the complete disk image. TPM, NVRAM or RPMB anchoring remains required for that threat model.

The current ownership guard remains suitable only for the existing single-core, non-preemptive policy update path. SMP or preemptible execution requires an IRQ-safe lock, ownership identity and recovery-aware cancellation protocol.

ZPTJ1 currently serializes one signed-policy transaction at a time. ZRWP1 retains its independent transaction implementation, while ZVRT1 remains boot-loaded and has no runtime update command.
