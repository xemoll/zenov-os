# Signed policy update transactions — ZenovOS 0.1.1

## Scope

This pass hardens runtime updates for the three signed trust domains that previously wrote a new object and version floor without proving the complete persistent result:

- ZGDB2 executable appraisal and revocation database;
- ZCAP1 per-application syscall capability policy;
- ZMID1 malware intelligence database.

ZRWP1 retains its existing independent backup/readback/audit/sync transaction. ZVRT1 remains boot-loaded and does not expose a runtime update command in this pass.

## Commit protocol

Each newly hardened update follows this order:

1. Reject updates while the active policy is not ready.
2. Compute the exact successor with checked `next_version()`; `UINT32_MAX` is terminal.
3. Verify the candidate signature, key ID, schema, records, trust constraints and exact successor version.
4. Copy the current live signed object into one 16 KiB supervisor-only rollback workspace.
5. Re-read and revalidate the candidate to close the validation-to-use window.
6. Write the candidate to the live path.
7. Read the live object back and repeat full cryptographic and structural validation.
8. Write canonical non-zero decimal version state and read it back through the strict parser.
9. Synchronize ZenovFS metadata.
10. For ZMID1, append the durable signed-intelligence update audit after the policy and version are synchronized.
11. Activate the new in-memory records only after every required persistent step succeeds.

Every failure after the live object may have changed attempts to restore the exact previous bytes, reads the restored object back, restores and verifies the previous version state, synchronizes metadata, wipes the rollback workspace and retains the previous in-memory policy. If restoration cannot be proven, that trust domain sets `ready=false` and remains fail-closed.

## Memory domain

The shared rollback bytes are not part of low kernel BSS. They occupy the reserved supervisor-only physical range `0x00312000–0x00315FFF`, after the PMM bitmap and before the Live-image workspaces and independently of the high ring-3 base at `0x40000000`. Compile-time assertions reject overlap with adjacent fixed regions. The workspace is serialized by a `busy` guard and is wiped through volatile stores on release.

## Verification

The dedicated `Signed Policy Transactions` workflow:

- compiles the host transaction model with GCC strict warnings;
- repeats it with Clang ASan, UBSan, unsigned-overflow and implicit-conversion sanitizers;
- tests bounded backup, nested transaction rejection, exact policy readback, canonical version readback, corrupt-readback detection, write failure propagation and non-elidable wipe;
- runs `make clean check`;
- runs the complete two-boot QEMU lifecycle that updates ZMID1, ZCAP1 and ZGDB2 and verifies their persisted versions after reboot;
- runs deterministic rebuild;
- uploads binaries, logs, source contracts, images and runtime evidence.

## Security boundary

This protocol closes ordinary runtime partial-success paths and detects a storage layer that reports success but returns altered policy or version bytes on readback. It does not claim power-loss atomicity across two separate files.

The write order makes the normal crash window `new signed object + old version floor`, which boot can safely reconcile forward after signature validation. A crash during rollback can leave `old signed object + new version floor`; boot then fails closed. A future persistent transaction-intent record is required for automatic cross-file recovery in every crash window.

The protocol is currently serialized by the single-core, non-preemptive update path. Before SMP or preemptible kernel execution, `busy` must be replaced by an IRQ-safe lock and ownership protocol. TPM/NVRAM-backed freshness is also still required to resist offline replacement of the complete data image.
