# ZCAP1 signed syscall-policy format

This document is the storage and cryptographic reference for the signed capability policy used by ZenovOS 0.1.1. The runtime semantics and profile table are described in [`SYSCALL_CAPABILITIES_0.1.1.md`](SYSCALL_CAPABILITIES_0.1.1.md).

## Trust separation

`ZGDB2` authorizes executable identities, threat rules and revocations. `ZCAP1` authorizes syscall masks and exact file scopes only for that already fixed trusted path set. The ZCAP parser rejects any record set that is not an exact one-to-one mapping to the seven compiled trusted executable paths.

The ZCAP root is independent from the ZGDB root. A compromise or operational replacement of one signing role does not automatically grant the other role.

## Canonical layout

All integer fields are little-endian. The policy contains no variable-length record table.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | magic `ZCAP` |
| 4 | 2 | schema = 1 |
| 6 | 2 | header size = 64 |
| 8 | 4 | policy version |
| 12 | 4 | minimum engine version |
| 16 | 4 | record count = 7 |
| 20 | 4 | payload size = 1120 |
| 24 | 32 | SHA-256 over the record payload |
| 56 | 8 | root key ID |
| 64 | 1120 | seven 160-byte records |
| 1184 | 256 | RSA-PSS signature over bytes 0..1183 |

Each record is:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 48 | absolute canonical application path |
| 48 | 4 | capability mask |
| 52 | 48 | optional exact read/stat scope |
| 100 | 48 | optional exact write scope |
| 148 | 12 | reserved, all zero |

Every string must contain a NUL terminator inside its field and all bytes after the terminator must be zero. Nonempty strings must be absolute normalized paths.

## Signature profile

```text
RSA modulus        2048 bits
encoding           RSASSA-PSS
message hash       SHA-256
mask generation    MGF1-SHA-256
salt length        32 bytes
trailer field      0xBC
root key ID        9202c73fad96ad66
```

The root key ID is the first eight bytes of SHA-256 over the public SubjectPublicKeyInfo DER.

## Monotonic state

The kernel trusts the maximum of:

- compiled floor `1`;
- persistent `/security/syscall-capabilities.version`.

The active policy must meet that minimum. Interactive updates must be exactly one version higher than the current active policy. A valid jump greater than one is rejected as non-sequential; an equal or lower version is rejected as rollback.

ZenovFS copy-on-write protects each individual file replacement from partial-sector exposure. The active policy is committed before the version file. If power is lost between those commits, boot accepts the newer correctly signed active policy and repairs the older version state. If the policy file is corrupt, carrying an unknown root key ID or older than the stored version, boot fails closed before `ZENOVOS_UI_READY`.

## Required validation markers

```text
ZCAP_ROOT_KEY_OK id=9202c73fad96ad66
ZCAP_PSS_SIGNATURE_OK
ZCAP_POLICY_VERSION_OK version=1
ZCAP_READY
ZCAP_KEY_REJECTED reason=unknown-key
ZCAP_TAMPER_REJECTED reason=payload-digest
ZCAP_ATOMIC_UPDATE_OK version=2
ZCAP_ROLLBACK_REJECTED reason=rollback
ZCAP_POLICY_VERSION_OK version=2
ZCAP_INIT_FAILED reason=payload-digest
```

The corrupt boot image has a recomputed valid ZenovFS checksum. Its rejection therefore proves ZCAP payload-digest enforcement rather than ordinary filesystem corruption detection.

## Explicit limits

- The policy root is single-key, not threshold-signed.
- Root rotation requires a verified kernel update.
- There is no TPM/NVRAM monotonic counter.
- Full offline replacement of both kernel and data image is outside the current trust boundary.
- Policy expiry, timestamp metadata and online repositories are not implemented.
