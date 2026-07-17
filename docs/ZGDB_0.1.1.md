# ZGDB signed policy contract for ZenovOS 0.1.1

ZGDB is the signed, deterministic policy database consumed by ZenovGuard. It updates threat and revocation policy without changing the native ZEX1/ELF32 executable-format boundary and without allowing writable storage to authorize new application paths.

## Container

The canonical binary layout is:

```text
Header (64 bytes)
├── magic: ZGDB
├── schema version
├── header size
├── monotonic policy version
├── minimum engine version
├── record count
├── payload size
├── SHA-256 of record payload
└── zero reserved bytes

Records (84 bytes each)
├── type: TRUSTED / THREAT / REVOKED
├── flags
├── name length
├── SHA-256 digest
└── zero-padded name or normalized path

Signature (256 bytes)
└── RSA-2048 RSASSA-PKCS1-v1_5 over SHA-256(header || records)
```

All integer fields use little-endian representation. Reserved and unused name bytes must be zero, so the signed representation is deterministic. The verifier rejects unknown schema versions, unsupported engine requirements, invalid sizes, malformed records, payload-digest mismatches and invalid signatures.

## Root of trust

Only the RSA-2048 public verification key is stored in the repository and kernel. The signing private key is not committed or packaged into ZenovOS.

The 0.1.1 repository contains two pre-signed policy fixtures:

- policy version 1: seven compiled trusted applications and two harmless test signatures;
- policy version 2: the same baseline plus revocation of the canonical `zenovapp.zex` digest.

The signing key used to produce those fixtures was not retained in this development environment. Therefore the current fixtures are verifiable, but a production key-custody and offline signing process is still required before future public database updates can be issued.

## Trust-baseline restriction

A ZGDB `TRUSTED` record is accepted only when its normalized path and SHA-256 already match the immutable trust baseline compiled into the kernel. The signed database can revoke an existing application or add a threat digest; it cannot silently authorize a new executable path on writable ZenovFS storage.

Adding a new executable remains a reviewed source/build action that changes the compiled baseline and requires a new verified ZenovOS image.

## Update order

`guard update <path>` performs:

1. checksum-valid ZenovFS read of the candidate;
2. structural, payload-hash and RSA signature verification;
3. minimum-engine and monotonic-version checks;
4. compiled trust-baseline validation;
5. a second final validation of the exact candidate bytes;
6. ZenovFS copy-on-write replacement of `/security/zenovguard.zgdb`;
7. update of `/security/zenovguard.version`;
8. in-memory activation.

The database is committed before the version-state file. If power is lost after the database commit but before the version-state update, boot accepts the newer signed database and advances the state. If the version state is newer than the database, boot fails closed rather than accepting a rollback.

The active database and version-state paths are protected from shell and userspace file mutation. Updates bypass those ordinary file-operation wrappers only inside the signed-policy updater.

## Anti-rollback boundary

Three version checks are enforced:

- compiled policy floor: `1` for this 0.1.1 image;
- persistent `/security/zenovguard.version` state;
- currently active in-memory policy version.

This prevents rollback during normal operation and across ordinary reboots on the same data image. It does not provide hardware-backed monotonic state. An offline attacker able to replace the entire data image can roll both the database and state back as far as the compiled policy floor. Closing that gap requires TPM/NVRAM-backed monotonic storage or a future kernel build with a raised compiled floor.

## Revocation

Policy version 2 contains a `REVOKED` record for the canonical `zenovapp.zex` digest. After activation, the application remains structurally valid and still matches the compiled trust baseline, but execution is denied before ordinary ZenovGuard trust appraisal:

```text
ZGDB_REVOCATION_BLOCKED path=/apps/zenovapp.zex rule=Revoked.ZenovApp.V2
```

The revocation persists after reboot because both the active database and policy version are stored transactionally on ZenovFS1.

## Verification contract

Host checks require:

```text
v1 SHA-256 matches the pinned artifact hash
v2 SHA-256 matches the pinned artifact hash
OpenSSL verifies both RSA signatures
OpenSSL rejects the byte-tampered candidate
all generated databases are byte-identical on rebuild
```

QEMU checks require:

```text
ZGDB_SIGNATURE_OK
ZGDB_POLICY_VERSION_OK version=1
ZGDB_TAMPER_REJECTED
ZGDB_ATOMIC_UPDATE_OK version=2
ZGDB_REVOCATION_BLOCKED
ZGDB_ROLLBACK_REJECTED
ZGDB_POLICY_VERSION_OK version=2  # after reboot
```

The recovery-image phase must still boot policy version 1 and pass the existing ZenovFS interrupted-write recovery test.

## Explicit limitations

- No network updater exists.
- No private signing key or automated signing service is stored in the repository.
- The current signature scheme is RSA-2048 PKCS#1 v1.5 with SHA-256, not key rotation or threshold signing.
- There is no TPM-backed measured boot, sealed key or monotonic counter.
- ZGDB does not authorize foreign executable formats.
- ZGDB does not replace page isolation, syscall validation, W^X admission, quarantine or the compiled application trust baseline.
