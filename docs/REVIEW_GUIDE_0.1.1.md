# ZenovOS 0.1.1 review guide

Review high-risk changes in this order:

1. `kernel/user.S` and `kernel/interrupts.S` — stack ownership and ring transition correctness;
2. `kernel/parts/memory.inc` and `user_window.inc` — mapping, write protection, allocator invariants and cleanup;
3. `kernel/parts/process.inc` and `process_policy.inc` — untrusted executable parsing, range overflow and permissions;
4. `kernel/parts/storage.inc` — transaction commit point and recovery ordering;
5. `tools/zenovfs_fault_test.cpp` — whether the harness accurately mirrors kernel write order;
6. `tests/qemu_smoke.sh` and CI — whether markers are checked only after the underlying operation;
7. build manifest/package scripts — provenance and deterministic comparisons.

Do not approve by marker names alone. Confirm that negative applications reach the intended CPU exception, that the common cleanup path runs, that host fault injection includes every write prefix, and that release artifacts come from the reviewed commit.
