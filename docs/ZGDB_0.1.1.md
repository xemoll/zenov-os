# ZGDB2 signed policy contract for ZenovOS 0.1.1

ZGDB2 is the deterministic signed policy database consumed by ZenovGuard. It updates threat and revocation policy without changing the native ZEX1/ELF32 executable-format boundary and without allowing writable storage to authorize new application paths.

## Container

The canonical binary layout is:

```text
Header (64 bytes)
├── magic: ZGDB
├── schema version: 2
├── header size: 64
├── monotonic policy version
├── minimum engine version
├── record count
├── payload size
├── SHA-256 of record payload
└── root key ID: first 8 bytes of SHA-256(SPKI DER)

Records (84 bytes each)
├── type: TRUSTED / THREAT / REVOKED
├── flags
├── name length
├── SHA-256 digest
└── zero-padded name or normalized path

Signature (256 bytes)
└── RSA-2048 PSS / SHA-256 / MGF1-SHA-256 / 32-byte salt
```

All integer fields use little-endian representation. Unused name bytes must be zero, so the signed representation is deterministic. Schema 2 fixes the signature algorithm and PSS parameters rather than accepting algorithm identifiers selected by untrusted input.

The verifier rejects unknown schema versions, unsupported engine requirements, invalid sizes, unknown key IDs, malformed records, duplicate or missing trusted records, payload-digest mismatches, invalid PSS encoding and invalid signatures.

## Rotated root of trust

The schema-2 verification root has key ID:

```text
6f788074c018f5aa
```

Only its RSA-2048 public key is stored in the repository and kernel. The previous PKCS#1 v1.5 root was removed from kernel trust; policy versions 1 and 2 are therefore no longer accepted by this build. The compiled policy floor was raised to `3`.

The repository contains two pre-signed positive fixtures:

- policy version 3: seven compiled trusted applications and two harmless test signatures;
- policy version 4: the same baseline plus revocation of the canonical `zenovapp.zex` digest.

It also contains generated negative fixtures:

- a byte-tampered version-4 candidate;
- a candidate whose signed header carries an unknown key ID.

The private key used to produce versions 3 and 4 is not committed or packaged. Future public database updates require a separately provisioned offline key-custody and signing process.

## Trust-baseline restriction

A `TRUSTED` record is accepted only when its normalized path and SHA-256 already match the immutable trust baseline compiled into the kernel. Each of the seven compiled trusted applications must occur exactly once. Duplicate records, omitted records and additional trusted paths are rejected.

The signed database can revoke an existing application or add a threat digest; it cannot silently authorize a new executable path on writable ZenovFS storage. Adding a new executable remains a reviewed source/build action requiring a new verified ZenovOS image.

## RSA-PSS verification

The freestanding verifier performs:

1. RSA public operation with exponent 65537;
2. 2048-bit encoded-message recovery;
3. trailer-byte check for `0xbc`;
4. high-bit constraint for `emBits = 2047`;
5. MGF1-SHA-256 reconstruction of the data block;
6. zero-padding and `0x01` separator validation;
7. extraction of the fixed 32-byte salt;
8. SHA-256 comparison of `0x00 × 8 || messageHash || salt`.

The kernel does not call libc or OpenSSL. OpenSSL is used only by host CI as an independent verifier for the fixed signed fixtures.

## Update order

`guard update <path>` performs:

1. checksum-valid ZenovFS read of the candidate;
2. structural, payload-hash, key-ID and RSA-PSS verification;
3. minimum-engine and version checks;
4. exact compiled trust-baseline validation;
5. enforcement that the candidate version is exactly `active + 1`;
6. a second validation of the exact candidate bytes;
7. ZenovFS copy-on-write replacement of `/security/zenovguard.zgdb`;
8. update of `/security/zenovguard.version`;
9. in-memory activation.

The database is committed before the version-state file. If power is lost after database commit but before state update, boot accepts the newer signed database and advances the state. If the version state is newer than the database, boot fails closed rather than accepting rollback.

The active database and version-state paths are protected from shell and userspace file mutation. Only the verified internal updater bypasses those wrappers.

## Anti-rollback boundary

Three checks are enforced:

- compiled policy floor: `3`;
- persistent `/security/zenovguard.version` state;
- currently active in-memory policy version.

Interactive updates must be exactly sequential (`N → N+1`). This prevents skipped transition states as well as ordinary rollback.

The design does not provide hardware-backed monotonic state. An offline attacker able to replace the entire data image can roll both database and state back to version 3, but not to the removed schema-1 root or policy versions 1/2 unless the kernel image is also replaced. Closing the remaining cross-image gap requires TPM/NVRAM-backed monotonic storage or a later kernel with a higher compiled floor.

## Revocation

Policy version 4 contains a `REVOKED` record for the canonical `zenovapp.zex` digest. The application remains structurally valid and matches the compiled trust baseline, but execution is denied before ordinary ZenovGuard trust appraisal:

```text
ZGDB_REVOCATION_BLOCKED path=/apps/zenovapp.zex rule=Revoked.ZenovApp.V4
```

The revocation persists after reboot because the active database and policy version are stored transactionally on ZenovFS1.

## Verification contract

Host checks require:

```text
v3 SHA-256 matches the pinned artifact hash
v4 SHA-256 matches the pinned artifact hash
OpenSSL verifies both RSA-PSS signatures with salt length 32
OpenSSL rejects the tampered candidate
OpenSSL rejects the unknown-key-ID candidate
all generated databases are byte-identical on rebuild
```

QEMU checks require:

```text
ZGDB_ROOT_KEY_OK id=6f788074c018f5aa
ZGDB_PSS_SIGNATURE_OK
ZGDB_POLICY_VERSION_OK version=3
ZGDB_KEY_REJECTED reason=unknown-key
ZGDB_TAMPER_REJECTED
ZGDB_ATOMIC_UPDATE_OK version=4
ZGDB_REVOCATION_BLOCKED
ZGDB_ROLLBACK_REJECTED
ZGDB_POLICY_VERSION_OK version=4  # after reboot
```

The recovery-image phase must still boot policy version 3 and pass the existing ZenovFS interrupted-write recovery test.

## Explicit limitations

- No network updater exists.
- No private signing key or automated signing service is stored in the repository.
- This build has one active root key, not threshold signing.
- In-band root metadata rotation is not implemented; replacing the root currently requires a verified OS build.
- There is no TPM-backed measured boot, sealed key or monotonic counter.
- ZGDB2 does not authorize foreign executable formats.
- ZGDB2 does not replace page isolation, syscall validation, W^X admission, quarantine or the compiled application trust baseline.
