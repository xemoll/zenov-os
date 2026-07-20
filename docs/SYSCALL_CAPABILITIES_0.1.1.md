# Signed per-application syscall capabilities for ZenovOS 0.1.1

ZenovOS 0.1.1 does not treat a trusted executable as implicitly authorized for the complete syscall ABI. Every ring-3 launch receives a least-privilege profile from the independently signed `ZCAP1` policy stored in ZenovFS.

Executable authorization and syscall authorization remain separate trust decisions:

- `ZGDB2` and the compiled path-plus-SHA-256 baseline decide whether the exact final-read executable may run;
- `ZCAP1` decides which syscalls and exact file scopes that trusted executable receives.

A valid signature alone cannot authorize a new executable path. The kernel requires the signed profile set to be a one-to-one match with the seven compiled trusted executable paths.

## Enforcement order

The active profile is installed only after the exact bytes consumed by the loader have passed:

1. ZenovFS checksum-valid final read;
2. ZGDB2 threat and revocation policy;
3. ZEX1 or ELF32 structural validation and ELF W^X admission;
4. immutable path-and-SHA-256 trust appraisal;
5. persistent `EXEC / TRUSTED` audit commit;
6. exact normalized-path lookup in the active, signature-valid ZCAP1 policy.

The profile is cleared before every launch attempt and after normal exit, user fault or load failure. Authority from a previous foreground application cannot survive into the next launch.

## Capability bits

```text
0x01  console-write
0x02  ticks
0x04  file-read
0x08  file-write
0x10  file-stat
0x20  version
0x40  sync
0x80  console-read
```

`exit` remains available so a process can terminate. Unknown syscall numbers preserve `ERROR_UNSUPPORTED`. A syscall denied by the capability layer returns:

```text
ERROR_DENIED = 0xFFFFFFF9
```

If the mandatory persistent denial audit cannot be committed, execution is terminated and the audit enforcement lock remains active.

## ZCAP1 binary contract

The policy is a fixed, bounded binary object:

```text
Header                         64 bytes
  magic                        "ZCAP"
  schema                       1
  header_size                  64
  policy_version               monotonic uint32
  minimum_engine               0x00000101
  record_count                 7
  payload_size                 1120
  payload_sha256               32 bytes
  root_key_id                  8 bytes

Records                      7 × 160 bytes
  path                         48-byte canonical NUL-padded field
  capability_mask              uint32
  read_scope                   48-byte canonical NUL-padded field
  write_scope                  48-byte canonical NUL-padded field
  reserved                     12 zero bytes

Signature                     256 bytes
  RSA-2048 / RSASSA-PSS
  SHA-256 / MGF1-SHA-256
  salt length                  32 bytes

Total                         1440 bytes
```

The active root key ID is:

```text
9202c73fad96ad66
```

The kernel rejects malformed sizes, unsupported schemas or engine requirements, nonzero reserved bytes, noncanonical strings, unknown key IDs, payload-digest mismatches, invalid RSA-PSS signatures, duplicate or missing trusted paths, unknown paths, invalid capability bits, inconsistent scopes, rollback and non-sequential updates.

The private signing key is not stored in the repository or OS image. The public verification key is `security/zcap-root-public.pem`.

## Policy versions included in 0.1.1

Policy v1 preserves the original seven profiles:

| Trusted application | Mask | Granted operations | Exact file scope |
|---|---:|---|---|
| `/apps/hello.zex` | `0x01` | console write | none |
| `/apps/fileio.elf` | `0x7d` | console write, file read/write/stat, version, sync | `/apps/userio.txt` for read/write/stat |
| `/apps/args.elf` | `0x05` | console write, file read | `/docs/readme.txt` |
| `/apps/console.elf` | `0x81` | console write/read | none |
| `/apps/protect.elf` | `0x01` | console write | none |
| `/apps/kaccess.elf` | `0x00` | none | none |
| `/apps/zenovapp.zex` | `0x01` | console write | none |

Policy v2 removes `console-write` from `/apps/hello.zex` while retaining the other six profiles. This supplies a real runtime revocation test without changing the executable bytes.

Pinned fixture hashes:

```text
v1  029f784f01afede5d38da066ba239213ace2279eac9381876c70574e2eb89bf1
v2  f325310b94d443fb58df94b56f012ea0b6533fe0cbb453bef2c5d82551a9e51b
```

File scopes are exact normalized ZenovFS paths. Prefix matches, sibling paths and alternate spellings do not inherit access.

## Update and rollback behavior

The shell interface is:

```text
guard capability-policy
guard capability-update <signed-zcap-path>
```

An update must be exactly `N + 1`. The candidate is parsed, hashed and signature-verified, then read and verified again immediately before the exact bytes are written through ZenovFS copy-on-write replacement. The active policy file is committed before the persistent version state. On boot, a newer valid active policy can repair an older version file; an active policy older than the persistent version or compiled floor is rejected.

Ordinary shell and userspace file operations cannot overwrite, append, remove, rename or replace:

```text
/security/syscall-capabilities.zcap
/security/syscall-capabilities.version
```

The update fixtures include valid v1/v2 policies, a payload-tampered policy and a policy carrying an unknown root key ID. The negative fixtures must fail both host OpenSSL verification and kernel validation.

## Denial audit

Every capability or path-scope denial creates a persistent ZGAL1 record before control returns to the process:

```text
action  EXEC
verdict UNTRUSTED
path    trusted application path
digest  trusted application SHA-256
```

The QEMU contract requires two independent denials:

```text
SYSCALL_CAPABILITY_DENIED app=/apps/kaccess.elf syscall=1 capability=console-write reason=missing-capability
SYSCALL_CAPABILITY_DENIED app=/apps/hello.zex syscall=1 capability=console-write reason=missing-capability
```

The first proves the baseline zero-mask profile. The second proves that signed policy v2 changed live syscall authority. An independent host verifier requires both retained records in the final runtime ZenovFS image.

## CI contract

The dedicated `ZenovOS 0.1.1 Syscall Capabilities` workflow requires:

- strict host and freestanding compilation with warnings as errors;
- deterministic ZCAP1 construction and pinned fixture hashes;
- OpenSSL RSA-PSS verification of v1 and v2;
- rejection of tampered and wrong-key fixtures;
- the existing 1,662-case audit COW fault matrix;
- seven QEMU phases, including an FNV-repaired corrupt-ZCAP boot that must panic before the UI;
- five successful boots reporting the expected root, signature and seven-profile bijection;
- seven v1 profile activations plus one post-update v2 activation in the primary phase;
- v1 → v2 activation, v2 persistence after reboot and v2 → v1 rollback rejection;
- persistent audit evidence for both `/apps/kaccess.elf` and `/apps/hello.zex` denials;
- successful `ZENOVAPP.ZEX` execution after the KACCESS denial, proving authority cleanup;
- deterministic rebuilding of the kernel, policies and storage image.

Expected summary marker:

```text
ZENOV_SYSCALL_CAPABILITY_GATE_OK profiles=7 signed_update=yes persistent_denials=2 authority_leakage=no
```

## Security boundary

ZCAP1 permits operational capability changes without rebuilding the kernel, but it is not a general object-capability system. ZenovOS 0.1.1 still has one foreground process, one active profile and path-based file scopes. It does not implement transferable handles, directory capabilities, delegation, concurrent processes, in-process revocation or a user-space policy service.

Rollback resistance survives ordinary reboot of the same data image and is bounded by the compiled policy floor. Without TPM/NVRAM or an external witness, an offline attacker who replaces the entire kernel and data image can replace both policy and version state. Root rotation also still requires a new verified kernel build.
