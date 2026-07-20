# ZenRepo signed offline repository for ZenovOS 0.1.1

ZenRepo is the package-authorization layer used by ZenovOS 0.1.1. It is independent from the package container: package hashes establish byte integrity, while repository metadata determines which exact bytes, versions, entrypoints and syscall authorities are trusted.

## Stable interface

Runtime filenames and normal commands use stable role names:

```text
root-bootstrap.zrm
root.zrm
targets.zrm
native-apps.zrm
snapshot.zrm
timestamp.zrm
```

Normal status output deliberately does not expose internal role revision counters. Monotonic counters remain inside signed headers and persistent anti-rollback state because removing them would make rollback detection impossible.

## Trust roles

The bounded `ZRM1` envelope separates:

- root key trust and root rotation;
- top-level target authorization;
- a terminating `native/` delegated namespace;
- repository consistency through snapshot references;
- freshness through timestamp metadata.

Root replacement requires authorization by the previous root and the replacement root. Referenced metadata must match exact signed length, digest and internal monotonic counter.

## Target authorization

Every delegated target pins:

- repository path;
- package name and semantic version;
- immutable installed entrypoint;
- exact package byte length;
- payload type;
- full-package SHA-256;
- installed payload SHA-256;
- syscall capability mask;
- exact read and write scopes when file capabilities are present.

The initial repository authorizes two `hello-native` releases. Both receive only `console-write`. A structurally valid newer fixture is present in the image but absent from signed targets and is rejected.

## Boot verification

```text
compiled bootstrap public key
  -> verify bootstrap root
  -> verify rotated root with previous and replacement authority
  -> verify timestamp role
  -> verify referenced snapshot
  -> verify referenced top-level targets
  -> verify terminating native delegation
  -> verify delegated package targets and syscall scopes
  -> enforce persistent anti-rollback floors
  -> atomically persist repository state
  -> expose authorized targets to ZenPkg
```

Signature, threshold, expiration, reference, delegation, target or state failure keeps the repository unavailable and prevents package-manager initialization.

## Anti-rollback state

`/data/var/lib/zenpkg/repo.v1` stores the highest accepted internal counters for each trust role. Its checksum detects corruption but is not authentication; every boot still verifies all signatures and cross-role references. Shell and userspace cannot modify `/data/repo/*` or the repository state.

## Expiry boundary

The host verifier accepts a caller-provided trusted Unix epoch. The kernel checks expiry against a compiled release trust floor. ZenovOS 0.1.1 does not have a trusted RTC or hardware monotonic counter, so it cannot claim complete resistance to a long-term freeze attack after replacement of the entire data image. This is an explicit boundary.

## Host tooling

```text
zenrepo verify --metadata <directory> --time <epoch> [--state <file>]
zenrepo inspect <metadata.zrm>
```

Regression rejects expired metadata, bad signatures, duplicate signers, insufficient threshold, snapshot mix-and-match, rollback below persisted floors, corrupt state, malformed delegation and invalid syscall scopes. Private signing keys are not committed.

## Current limits

- Offline ZenovFS repository only; no mirror or HTTP transport.
- One terminating delegated namespace.
- Bounded key, role, signature and target tables.
- RSA-2048/PSS/SHA-256 only.
- No transparency log, secure RTC or hardware monotonic counter.
- System version remains exactly `0.1.1`.
