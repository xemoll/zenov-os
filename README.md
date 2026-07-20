# ZenovOS 0.1.1

ZenovOS is a compact 32-bit x86 operating system built with Zenov, assembler and freestanding C++17. Version 0.1.1 boots into an adaptive graphical desktop on QEMU Standard VGA. The default physical mode is 1024×768; the same kernel-rendered desktop is verified across 22 VBE modes from 640×480 through 1600×1200 while retaining the text shell and COM1 serial console as diagnostic fallbacks.

The current engineering focus is a small, auditable security foundation combined with a usable native desktop rather than compatibility with foreign executable formats. ZenovOS supports native ZEX1 and validated static ELF32/i386 applications. ZenovGuard adds final-read SHA-256 appraisal, fail-closed execution, ZGDB2 RSA-PSS threat/revocation policy, quarantine and a persistent bounded hash-chained audit journal around that execution boundary.

![ZenovOS 0.1.1 graphical desktop](./docs/screenshots/zenov-os-0.1.1-graphical-desktop.png)

The screenshot is the actual 1024×768 QEMU framebuffer captured by CI from the current 0.1.1 desktop. The adaptive-display workflow also cycles all 22 supported physical modes, validates VBE read-back and persistence, captures representative framebuffers and preserves the original PPM evidence, serial logs, kernel image and data image.

## Verified 0.1.1 scope

Executable regression coverage includes:

- deterministic BIOS/FAT12 boot with a segmented kernel loader;
- QEMU Standard VGA discovery, Bochs VBE modes from `640×480×32` through `1600×1200×32`, 16 MiB supervisor-only framebuffer MMIO and mode read-back;
- a stable 800×600 logical UI canvas with aspect-preserving viewport mapping, resolution-independent mouse hit testing and a hybrid scaler that keeps exact integer modes crisp while smoothing fractional modes;
- native Terminal, Files, Settings and Applications surfaces, persistent theme/display/motion/cursor preferences and keyboard/mouse navigation;
- software backbuffer, clipping, alpha blending, bitmap text and a movable desktop window;
- PS/2 keyboard and mouse initialization, IRQ routing and packet-decoder tests;
- E820 physical-memory management, 4 KiB paging and a reusable kernel heap;
- page-granular user mappings, `CR0.WP`, read-only code and W+X ELF rejection;
- process-window scrubbing after every exit and recoverable user fault;
- guarded syscalls, stable error codes, console input and `argc/argv`;
- transactional ZenovFS1 replacement with crash-boundary fault injection;
- deterministic Zenov source to ZEX1 compilation and ring-3 execution;
- ZenovGuard integrity appraisal, detection and quarantine;
- ZGAL1 persistent SHA-256 audit chaining, boot replay and fail-closed append;
- 1,662-case audit COW fault matrix covering ordered crashes, torn sectors, garbage, dropped/duplicated/reordered writes and ring rotation;
- kernel boot verification of old-state recovery, new-state recovery and invalid-journal fail-closed behavior;
- rotated-root ZGDB2 RSA-PSS validation, key-ID enforcement, tamper rejection, sequential update, rollback rejection and revocation;
- deterministic rebuilds and release-package provenance.

Documentation starts at [`docs/INDEX.md`](docs/INDEX.md). Desktop behavior is documented in [`docs/DESKTOP_0.1.1.md`](docs/DESKTOP_0.1.1.md). Security contracts are defined in [`docs/ZENOVGUARD_0.1.1.md`](docs/ZENOVGUARD_0.1.1.md), [`docs/ZGDB_0.1.1.md`](docs/ZGDB_0.1.1.md) and [`docs/AUDIT_JOURNAL_0.1.1.md`](docs/AUDIT_JOURNAL_0.1.1.md).

## ZenovGuard, ZGDB2 and persistent audit

ZenovGuard is the local integrity and malware-prevention layer for ZenovOS 0.1.1. It is not a claim of broad commercial antivirus coverage.

Before a persistent application may enter ring 3, the final loader read is checked through this pipeline:

```text
ZenovFS checksum-valid final read
        │
        ▼
kernel SHA-256
        │
        ├── ZGDB2 key ID + RSA-PSS threat/revocation records
        ├── ZEX1 or ELF32 structural validation
        ├── W+X executable-policy rejection
        └── exact normalized path + compiled trusted SHA-256
        │
        ▼
persistent ZGAL1 EXEC record committed by ZenovFS COW
        │
        ▼
ALLOW only when trusted, not revoked and audit commit succeeded
```

The appraisal is performed on the same bytes consumed by the loader, immediately before user-page mapping. Unknown files, valid applications copied to another path, malformed containers, quarantined files, known signatures and revoked digests are denied by default.

At boot, ZenovGuard re-reads all seven bundled applications and verifies their immutable path-and-SHA-256 baseline. It validates `/security/zenovguard.zgdb`, including schema 2, root key ID `6f788074c018f5aa`, payload SHA-256 and RSA-2048 PSS/SHA-256 signature with MGF1-SHA-256 and a fixed 32-byte salt. It also replays the complete retained ZGAL1 audit chain. If persistent storage, audit state, trust baseline or signed policy is unavailable, execution stays locked.

The previous PKCS#1 v1.5 root has been removed from kernel trust. This build accepts policy version 3 or later under the rotated root and compiled floor 3. Policies 1 and 2 cannot be reintroduced merely by replacing the data image.

The database uses deterministic fixed-size records. Every signed `TRUSTED` record must correspond to one compiled trusted path and digest, exactly once. A signed policy can add threat digests or revoke an existing application, but cannot authorize a new executable path on writable storage.

The persistent audit file is `/security/zenovguard.audit`. It contains a 96-byte header and 64 fixed 128-byte records. Each record commits to the previous SHA-256 hash, monotonic sequence, action, verdict, canonical path and complete object digest. When the ring wraps, the hash of the removed oldest record becomes the anchor for the retained window.

Available commands:

```text
guard status
guard database
guard update <signed-zgdb-path>
guard capability-policy
guard capability-update <signed-zcap-path>
guard selftest
guard scan <path>
guard scan all
guard quarantine <path>
guard log
guard log verify
```

`antivirus` is an alias for `guard`.

Signed ZGDB2 and ZCAP1 updates are verified twice before activation, must be exactly version `N+1`, and are stored through ZenovFS copy-on-write replacement. Each active policy object is committed before its corresponding version file. Boot reconciles a newer valid signed policy with older version state, while an active policy older than stored state is rejected.

Every BOOT, SCAN, EXEC and QUARANTINE event replaces the complete 8,288-byte journal through the same copy-on-write mechanism. If an audit append fails, ZenovGuard restores the prior in-memory state, marks the journal unavailable and locks subsequent application execution.

The audit transaction is now exercised against two exact transitions: empty journal to one record and full 64-record ring rotation. Across 1,662 deterministic fault cases, the only accepted outcomes are the exact previous journal, the exact new journal or explicit fail-closed rejection. A synthetic journal must also pass a SHA-256 known-answer test and a fixed ZGAL1 record-hash vector before crash images are generated.

Three complete data images are booted by QEMU: a pre-commit interruption that recovers the old journal, a post-commit interruption that recovers the new journal, and a committed replacement with one missing payload sector that must panic before `ZENOVOS_UI_READY`.

The trusted applications, active ZGDB2/ZCAP1 policies, their version state and the audit journal are protected from ordinary shell and userspace write, remove, rename and copy-over operations.

Quarantine uses an atomic ZenovFS metadata rename into:

```text
/data/quarantine/q-<sha256-prefix>.qtn
```

Policy version 3 contains the SHA-256 of the official harmless EICAR anti-malware test file and a separate ZenovGuard-only safe CI vector. Policy version 4 also revokes the canonical `zenovapp.zex` digest. The EICAR payload itself is not embedded in the repository or kernel image.

Important security markers include:

```text
ZENOV_AUDIT_COW_FAULT_MATRIX_OK
ZENOV_AUDIT_COW_FAULT_INJECTION_OK total_cases=1662
ZENOV_AUDIT_COW_OLD_OR_NEW_OR_FAIL_CLOSED_ONLY
ZENOV_GUARD_AUDIT_SELFTEST_OK
ZENOV_GUARD_AUDIT_REPLAY_OK
ZENOV_GUARD_AUDIT_READY
ZENOV_GUARD_AUDIT_VERIFY_OK
ZENOV_GUARD_AUDIT_INVALID
ZENOV_GUARD_SELFTEST_OK
ZENOV_GUARD_TRUST_BASELINE_OK
ZENOV_GUARD_READY
ZENOV_GUARD_DETECTED
ZENOV_GUARD_QUARANTINE_OK
ZENOV_GUARD_UNTRUSTED_BLOCKED
ZENOV_GUARD_EXEC_ALLOWED
ZGDB_ROOT_KEY_OK id=6f788074c018f5aa
ZGDB_PSS_SIGNATURE_OK
ZGDB_POLICY_VERSION_OK version=3
ZGDB_KEY_REJECTED reason=unknown-key
ZGDB_TAMPER_REJECTED
ZGDB_ATOMIC_UPDATE_OK version=4
ZGDB_ROLLBACK_REJECTED
ZGDB_REVOCATION_BLOCKED
ZGDB_POLICY_VERSION_OK version=4
ZCAP_ROOT_KEY_OK id=9202c73fad96ad66
ZCAP_PSS_SIGNATURE_OK
ZCAP_POLICY_VERSION_OK version=1
ZCAP_KEY_REJECTED reason=unknown-key
ZCAP_TAMPER_REJECTED
ZCAP_ATOMIC_UPDATE_OK version=2
ZCAP_ROLLBACK_REJECTED
ZCAP_POLICY_VERSION_OK version=2
```

## Memory and isolation

- E820-backed management of the first 128 MiB;
- 4 KiB physical-frame allocator with stress coverage;
- 2 MiB aligned heap with split, free, coalesce and invalid-free rejection;
- supervisor-only kernel and framebuffer mappings;
- ring-3 base at `0x00400000` with a 1 MiB application window;
- only the current image and 16 KiB stack mapped into userspace;
- RX ELF text/rodata and writable data/BSS/stack pages;
- zeroing of the reused process window before first use and after exit/fault.

The current i686 paging mode does not provide a general per-page NX bit. W^X is an admission-policy and code-write protection invariant, not complete hardware non-execution of writable data.

## Applications and ABI

Supported formats are deliberately limited to:

- ZEX1 version 1;
- static little-endian ELF32/i386 with validated `PT_LOAD` segments.

Bundled verification applications:

```text
run HELLO
run FILEIO.ELF
run ARGS.ELF alpha beta
run CONSOLE.ELF
run PROTECT.ELF
run KACCESS.ELF
run ZENOVAPP.ZEX
```

Policy version 3 allows all seven. The signed version-4 CI policy revokes `ZENOVAPP.ZEX` and proves the denial persists across reboot.

Syscalls use `INT 0x80`:

```text
0 exit            5 file_stat
1 write_console   6 get_version
2 get_ticks       7 sync
3 file_read       8 read_console
4 file_write
```

A ring-3 exception terminates only the foreground application, records vector/error/EIP and page-fault CR2 data, scrubs the process window and returns to the shell. Kernel faults remain fatal.

See [`docs/ABI_0.1.1.md`](docs/ABI_0.1.1.md) and [`docs/SECURITY_MODEL_0.1.1.md`](docs/SECURITY_MODEL_0.1.1.md).

## Graphics and input

The graphical foundation uses QEMU PCI VGA `1234:1111`, BAR0 framebuffer access and Bochs VBE ports `0x01CE/0x01CF`. Up to 16 MiB of framebuffer MMIO is mapped into a supervisor-only window at `0xC0000000`; applications receive no direct framebuffer access.

The desktop renders a stable logical 800×600 scene and maps it into these verified physical modes:

```text
640x480   720x480   800x600    960x540
960x600   1024x576  1024x600   1024x768
1152x648  1152x720  1152x864   1280x720
1280x768  1280x800  1280x960   1280x1024
1360x768  1368x768  1440x900   1536x864
1600x900  1600x1200
```

Aspect ratio is preserved by a centered viewport. Exact integer scales use a crisp sampling path; fractional and downscaled modes use precomputed fixed-point bilinear interpolation. Physical mouse coordinates are translated back into logical hitboxes, so the dock, Settings controls, Files browser and title-bar dragging remain aligned at every mode.

The default mode is `1024x768`. `F9` cycles forward through supported modes. Settings provides previous/next display selection, theme choice, motion preference and terminal cursor choice. Preferences are persisted in `/data/config/ui.cfg`.

The desktop remains kernel-rendered. It is not yet a user-space display server, compositor or reusable GUI toolkit. Keyboard input uses IRQ1. Mouse support includes PS/2 auxiliary-port initialization, IRQ12 route validation, three-byte packet synchronization, bounded cursor coordinates and title-bar dragging.

## Persistent storage

The kernel drives a primary-master ATA PIO disk and mounts ZenovFS1 at `/data`.

```text
mount df fsck sync
pwd cd ls mkdir touch
write append cat stat
cp mv rm
```

ZenovFS1 retains 128 fixed metadata entries and 64 KiB file slots. Replacement writes use a compatible copy-on-write protocol: payload to a free slot, staging metadata, commit metadata and old-entry cleanup. Mount recovery discards uncommitted staging or completes committed replacement. ZGDB and ZGAL1 replacement use the same transaction mechanism.

See [`docs/ZENOVFS1_TRANSACTIONS.md`](docs/ZENOVFS1_TRANSACTIONS.md).

## CI contract

The primary workflow performs:

1. strict host and freestanding compilation with warnings as errors;
2. FAT12, ZenovFS1, ZEX1 and ELF structural checks;
3. SHA-256 known-answer, fixed-record-vector, trust-baseline, execution-policy and audit-chain self-tests;
4. deterministic ZGDB2 and ZCAP1 construction with pinned policy hashes;
5. OpenSSL RSA-PSS verification of both ZGDB2 and both ZCAP1 positive fixtures with salt length 32;
6. independent rejection of tampered and unknown-key fixtures for both policy domains;
7. validation of the canonical empty ZGAL1 factory journal;
8. 1,662-case audit COW fault matrix over ordered crashes, torn/garbage/dropped/duplicated/reordered writes and metadata permutations;
9. existing exhaustive general ZenovFS1 crash-boundary injection;
10. seven QEMU phases covering normal runtime, signed ZCAP1 update, reboot persistence, general recovery, audit old/new recovery, audit fail-closed boot and corrupt-ZCAP fail-closed boot;
11. independent host verification of the non-empty runtime ZGAL1 chain;
12. generation of a payload-tampered image with a recomputed ZenovFS checksum and mandatory audit-chain rejection;
13. a separate adaptive-display QEMU run that cycles all 22 VBE modes, verifies read-back, persistence, Settings keyboard control and actual framebuffer dimensions;
14. representative desktop, compact, widescreen, 5:4, maximum-size and Settings framebuffer capture;
15. deterministic system rebuilding, including both signed policy domains, package metadata fixtures and the empty journal seed;
16. deterministic release ZIP generation and byte comparison;
17. evidence upload with runtime, recovery, corrupt and tampered images, binaries, both signed policy domains, both public roots, package metadata, manifest and serial/monitor/framebuffer logs.

## Build

Required: GNU Make, GNU binutils, OpenSSL, a C++17 compiler, `qemu-system-i386`, `zip` and `unzip`.

```bash
make clean check
make qemu
make test
bash tools/package_release.sh build/zenov-os.img build/zenov-data.img dist package
```

## Explicit limitations

ZenovGuard and ZGDB2 0.1.1 intentionally do not provide:

- a network signature updater;
- an operational repository-hosted private signing key or signing service;
- threshold root signing;
- in-band root-metadata rotation independent of an OS build;
- TPM/NVRAM-backed monotonic policy state;
- TPM-backed measured boot, sealed audit head or remote witness;
- secret-key authentication of the audit journal against complete offline image replacement;
- authenticated ZenovFS metadata against an offline disk attacker;
- archive, document, script, PE, Mach-O or other foreign-format scanning;
- heuristic, behavioral or machine-learning malware classification.

The compiled policy floor is `3`. Normal operation and reboots on the same data image reject rollback below persistent state, but an offline attacker replacing both database and version state can roll policy back to version 3. Reintroducing schema-1 policies 1/2 would additionally require replacing the kernel image because their old root is no longer trusted.

ZGAL1 detects corruption and modification, deletion, insertion, reordering or sequence discontinuity within the retained window unless the attacker constructs and substitutes an entirely new internally consistent journal. Because the chain has no key or external witness, a complete offline data-image rewrite can recompute it. The ring retains 64 records; long-term archival requires an external collector.

The audit fault matrix models ZenovFS sector and metadata writes deterministically. It does not prove physical-device behavior under undocumented volatile caches, firmware defects, controller remapping or a drive that reports flush completion dishonestly.

The private key used to produce the checked-in v3/v4 fixtures is not committed. The fixtures are cryptographically verifiable, but future public policy issuance requires a separately provisioned offline root and key-custody procedure.

ZenovOS remains a single-foreground-process i686/BIOS system. It does not yet provide concurrent user processes, per-process page directories, a user-space compositor, SMP, networking, USB, AHCI/NVMe, dynamic linking or ZenovFS2 variable extents.

The 22 display modes are verified on QEMU Standard VGA/Bochs VBE. Physical GPUs, VirtualBox, VMware and firmware-specific VBE implementations are not covered by the current automated evidence.

These limitations are documented to avoid overstating the protection level. The current security value is the narrow, deterministic and testable trusted-execution boundary.

## Release assets

The existing `v0.1.1` release assets remain the previous installable baseline until rebuilt and republished from the exact final audit-fault-tested `main` commit.

[Open the ZenovOS 0.1.1 release](https://github.com/xemoll/zenov-os/releases/tag/v0.1.1)

## License

Original ZenovOS code is BSD-2-Clause. FAT12 loader lineage and the retained x16-PRos MIT notice are documented in `THIRD_PARTY.md`.
