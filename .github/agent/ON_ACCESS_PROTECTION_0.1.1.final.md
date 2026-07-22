# ZenovOS 0.1.1 on-access read-protection contract

ZenovOS applies the active RSA-PSS-verified ZMID1 policy synchronously after ZenovFS checksum verification and before user-visible bytes are released by shell `cat` or a ring-3 file-read syscall.

`CLEAN` is returned normally. `SUSPICIOUS` requires a durable ZGAL1 `READ-AUDIT` before release. `INFECTED` requires a durable `READ-BLOCK`, zeros the destination bytes, resets the returned size, and fails with the stable access-denied ABI result.

Ordinary read bytes are first copied into the dedicated supervisor-only 64 KiB scan workspace. Audit persistence may then reuse ZenovFS scratch buffers without corrupting the pending result. Allowed bytes are copied back only after the decision completes; blocked bytes are never restored.

Internal `/security`, `/repo`, and `/var/lib/zenpkg` reads use dedicated raw parsers to avoid recursive policy appraisal. Matching is path-boundary aware. `/quarantine/*` is deliberately not excluded: shell and ring-3 reads always receive a durable `READ-BLOCK` with `Quarantine.ReadDenied`, independently of the current malware-signature database. Trusted quarantine management and rollback use narrow raw-read paths.

This is bounded complete-file mediation for ZenovFS1 objects up to 64 KiB. It is not fs-verity/dm-verity, a Merkle-tree authenticated paging system, a ClamAV-compatible daemon, archive emulation, a general YARA interpreter, cloud reputation, memory scanning, or process-tree EDR.
