# ZenovOS 0.1.1

ZenovOS is a compact 32-bit x86 operating system built with Zenov, assembler and freestanding C++17. Version 0.1.1 boots into an 800×600 graphical desktop on QEMU Standard VGA while retaining the text shell and COM1 serial console as diagnostic fallbacks.

The current engineering focus is a small, auditable security foundation rather than additional visual design or compatibility with foreign executable formats. ZenovOS supports native ZEX1 and validated static ELF32/i386 applications. ZenovGuard adds final-read SHA-256 appraisal, fail-closed execution, signed ZGDB threat/revocation policy, quarantine and a bounded audit log around that existing execution boundary.

![ZenovOS 0.1.1 graphical desktop](./docs/screenshots/zenov-os-0.1.1-graphical-desktop.png)

The screenshot is the actual 800×600 QEMU framebuffer captured by CI. The workflow also preserves the original PPM framebuffer, serial logs, kernel image, data image, signed policy fixtures and deterministic release package as evidence.

## Verified 0.1.1 scope

Executable regression coverage includes:

- deterministic BIOS/FAT12 boot with a segmented kernel loader;
- QEMU Standard VGA discovery, Bochs VBE `800×600×32` and supervisor-only framebuffer MMIO;
- software backbuffer, clipping, alpha blending, bitmap text and a movable desktop window;
- PS/2 keyboard and mouse initialization, IRQ routing and packet-decoder tests;
- E820 physical-memory management, 4 KiB paging and a reusable kernel heap;
- page-granular user mappings, `CR0.WP`, read-only code and W+X ELF rejection;
- process-window scrubbing after every exit and recoverable user fault;
- guarded syscalls, stable error codes, console input and `argc/argv`;
- transactional ZenovFS1 replacement with crash-boundary fault injection;
- deterministic Zenov source to ZEX1 compilation and ring-3 execution;
- ZenovGuard integrity appraisal, detection, quarantine and audit decisions;
- RSA-signed ZGDB policy validation, tamper rejection, monotonic update, rollback rejection and revocation;
- deterministic rebuilds and release-package provenance.

Documentation starts at [`docs/INDEX.md`](docs/INDEX.md). Security contracts are defined in [`docs/ZENOVGUARD_0.1.1.md`](docs/ZENOVGUARD_0.1.1.md) and [`docs/ZGDB_0.1.1.md`](docs/ZGDB_0.1.1.md).

## ZenovGuard and signed ZGDB policy

ZenovGuard is the local integrity and malware-prevention layer for ZenovOS 0.1.1. It is not a claim of broad commercial antivirus coverage.

Before a persistent application may enter ring 3, the final loader read is checked through this pipeline:

```text
ZenovFS checksum-valid final read
        │
        ▼
kernel SHA-256
        │
        ├── signed ZGDB threat and revocation records
        ├── ZEX1 or ELF32 structural validation
        ├── W+X executable-policy rejection
        └── exact normalized path + compiled trusted SHA-256
        │
        ▼
ALLOW only when trusted and not revoked
```

The appraisal is performed on the same bytes consumed by the loader, immediately before user-page mapping. Unknown files, valid applications copied to another path, malformed containers, quarantined files, known signatures and revoked digests are denied by default.

At boot, ZenovGuard re-reads all seven bundled applications and verifies their immutable path-and-SHA-256 baseline. It then validates `/security/zenovguard.zgdb`, including payload SHA-256 and its RSA-2048 PKCS#1 v1.5 SHA-256 signature. If the persistent volume, trust baseline or signed policy is unavailable, application execution stays locked.

The database uses deterministic fixed-size records. Every signed `TRUSTED` record must correspond to one compiled trusted path and digest, exactly once. A signed policy can add threat digests or revoke an existing application, but cannot authorize a new executable path on writable storage.

Available commands:

```text
guard status
guard database
guard update <signed-zgdb-path>
guard selftest
guard scan <path>
guard scan all
guard quarantine <path>
guard log
```

`antivirus` is an alias for `guard`.

Signed policy updates are verified twice before activation, then stored through ZenovFS copy-on-write replacement. The active database is committed before `/security/zenovguard.version`; boot reconciles a newer signed database with older version state, while an older database than the stored state is rejected. Active policy and version-state paths are protected from shell and userspace mutation.

Quarantine uses an atomic ZenovFS metadata rename into:

```text
/data/quarantine/q-<sha256-prefix>.qtn
```

The initial policy contains the SHA-256 of the official harmless EICAR anti-malware test file and a separate ZenovGuard-only safe CI vector. Policy version 2 also revokes the canonical `zenovapp.zex` digest. The EICAR payload itself is not embedded in the repository or kernel image.

Important security markers include:

```text
ZENOV_GUARD_SELFTEST_OK
ZENOV_GUARD_TRUST_BASELINE_OK
ZENOV_GUARD_READY
ZENOV_GUARD_DETECTED
ZENOV_GUARD_QUARANTINE_OK
ZENOV_GUARD_UNTRUSTED_BLOCKED
ZENOV_GUARD_EXEC_ALLOWED
ZGDB_SIGNATURE_OK
ZGDB_POLICY_VERSION_OK version=1
ZGDB_TAMPER_REJECTED
ZGDB_ATOMIC_UPDATE_OK version=2
ZGDB_ROLLBACK_REJECTED
ZGDB_REVOCATION_BLOCKED
ZGDB_POLICY_VERSION_OK version=2
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

Policy version 1 allows all seven. The signed version-2 CI policy revokes `ZENOVAPP.ZEX` and proves the denial persists across reboot.

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

The graphical foundation uses QEMU PCI VGA `1234:1111`, BAR0 framebuffer access and Bochs VBE ports `0x01CE/0x01CF`. The framebuffer is mapped into a supervisor-only MMIO window at `0xC0000000`; applications receive no direct framebuffer access.

The desktop remains kernel-rendered. It is not yet a user-space display server, compositor or reusable GUI toolkit. Keyboard input uses IRQ1. Mouse support includes PS/2 auxiliary-port initialization, IRQ12 route validation, three-byte packet synchronization, bounded cursor coordinates and title-bar dragging.

## Persistent storage

The kernel drives a primary-master ATA PIO disk and mounts ZenovFS1 at `/data`.

```text
mount df fsck sync
pwd cd ls mkdir touch
write append cat stat
cp mv rm
```

ZenovFS1 retains 128 fixed metadata entries and 64 KiB file slots. Replacement writes use a compatible copy-on-write protocol: payload to a free slot, staging metadata, commit metadata and old-entry cleanup. Mount recovery discards uncommitted staging or completes committed replacement. ZGDB active-policy replacement uses the same transaction mechanism.

See [`docs/ZENOVFS1_TRANSACTIONS.md`](docs/ZENOVFS1_TRANSACTIONS.md).

## CI contract

The primary workflow performs:

1. strict host and freestanding compilation with warnings as errors;
2. FAT12, ZenovFS1, ZEX1 and ELF structural checks;
3. SHA-256 known-answer, trust-baseline and execution-policy self-tests;
4. deterministic ZGDB construction and pinned artifact hashes;
5. OpenSSL verification of valid v1/v2 signatures and rejection of a tampered candidate;
6. exhaustive host ZenovFS1 crash-boundary injection;
7. three QEMU phases covering desktop boot, policy update, revocation, applications, persistence and interrupted recovery;
8. framebuffer screenshot capture;
9. deterministic system rebuilding, including all signed policy fixtures;
10. deterministic release ZIP generation and byte comparison;
11. evidence upload with images, binaries, signed policy, manifest and serial logs.

## Build

Required: GNU Make, GNU binutils, OpenSSL, a C++17 compiler, `qemu-system-i386`, `zip` and `unzip`.

```bash
make clean check
make qemu
make test
bash tools/package_release.sh build/zenov-os.img build/zenov-data.img dist package
```

## Explicit limitations

ZenovGuard and ZGDB 0.1.1 intentionally do not provide:

- a network signature updater;
- an operational repository-hosted private signing key or signing service;
- TPM/NVRAM-backed monotonic policy state;
- TPM-backed measured boot or remote attestation;
- a persistent cryptographically chained audit log;
- authenticated ZenovFS metadata against an offline disk attacker;
- archive, document, script, PE, Mach-O or other foreign-format scanning;
- heuristic, behavioral or machine-learning malware classification.

The compiled policy floor is `1`. Normal operation and reboots on the same data image reject rollback below persistent state, but an offline attacker replacing both database and version state can roll back as far as the compiled floor. A stronger cross-image guarantee requires hardware monotonic state or a future kernel with a raised floor.

The private key used to produce the checked-in v1/v2 fixtures is not committed and was not retained in this development environment. The fixtures are cryptographically verifiable, but future public policy issuance requires a separately provisioned offline root and key-custody procedure.

ZenovOS remains a single-foreground-process i686/BIOS system. It does not yet provide concurrent user processes, per-process page directories, a user-space compositor, SMP, networking, USB, AHCI/NVMe, dynamic linking or ZenovFS2 variable extents.

These limitations are documented to avoid overstating the protection level. The current security value is the narrow, deterministic and testable trusted-execution boundary.

## Release assets

The existing `v0.1.1` release assets remain the previous installable baseline until rebuilt and republished from the exact final signed-policy `main` commit.

[Open the ZenovOS 0.1.1 release](https://github.com/xemoll/zenov-os/releases/tag/v0.1.1)

## License

Original ZenovOS code is BSD-2-Clause. FAT12 loader lineage and the retained x16-PRos MIT notice are documented in `THIRD_PARTY.md`.
