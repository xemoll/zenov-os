# Per-application syscall capabilities for ZenovOS 0.1.1

ZenovOS 0.1.1 applies a compiled least-privilege profile to every trusted ring-3 application. A valid ZEX1 or ELF32 container and a trusted path-plus-SHA-256 appraisal are necessary for launch, but they no longer grant the complete syscall ABI.

## Enforcement order

The capability profile is installed only after the exact bytes consumed by the loader have passed:

1. ZenovFS checksum-valid final read;
2. ZGDB2 threat and revocation policy;
3. executable structural and W^X validation;
4. immutable path-and-SHA-256 trust appraisal;
5. persistent `EXEC / TRUSTED` audit commit;
6. exact trusted-path capability-profile lookup.

The profile is cleared before a launch attempt and again after the application exits, faults or fails to load. A previous application cannot lend authority to the next foreground process.

Unknown and untrusted executable paths still reach ZenovGuard appraisal and receive the normal untrusted verdict. Capability lookup is not used to bypass or replace the trust decision.

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

`exit` is always available so a process can terminate. Unknown syscall numbers preserve the existing `ERROR_UNSUPPORTED` behavior.

A syscall denied by the capability layer returns:

```text
ERROR_DENIED = 0xFFFFFFF9
```

If the mandatory persistent denial audit cannot be committed, the application is terminated with the audit enforcement lock active.

## Immutable profiles

| Trusted application | Mask | Granted operations | File scope |
|---|---:|---|---|
| `/apps/hello.zex` | `0x01` | console write | none |
| `/apps/fileio.elf` | `0x7d` | console write, file read/write/stat, version, sync | exact `/apps/userio.txt` for read/write/stat |
| `/apps/args.elf` | `0x05` | console write, file read | exact `/docs/readme.txt` |
| `/apps/console.elf` | `0x81` | console write/read | none |
| `/apps/protect.elf` | `0x01` | console write | none |
| `/apps/kaccess.elf` | `0x00` | none | none |
| `/apps/zenovapp.zex` | `0x01` | console write | none |

File scopes are exact normalized ZenovFS paths. A prefix, sibling file, alternate spelling or second file is not authorized merely because the application has a file capability bit.

At boot the kernel requires a one-to-one mapping between these seven profiles and the seven compiled trusted path-and-digest records. Duplicate, missing or unknown profile paths make the capability policy unavailable and stop normal boot.

## Denial audit

Every capability or path-scope denial creates a persistent ZGAL1 record before control returns to the process:

```text
action  EXEC
verdict UNTRUSTED
path    trusted application path
digest  trusted application SHA-256
```

Serial evidence includes the syscall number, required capability and denial reason:

```text
SYSCALL_CAPABILITY_DENIED app=/apps/kaccess.elf syscall=1 capability=console-write reason=missing-capability
```

The independent host verifier reads the final runtime ZenovFS image and requires the corresponding retained `EXEC / UNTRUSTED / /apps/kaccess.elf` record. This proves persistence independently of the current-session serial output.

## CI contract

The dedicated `ZenovOS 0.1.1 Syscall Capabilities` workflow requires:

- strict host and freestanding compilation with warnings as errors;
- the existing 1,662-case audit COW fault matrix;
- all six QEMU normal/recovery/fail-closed phases;
- five successful boots reporting a valid seven-profile policy;
- exactly seven expected profile activations in the primary application phase;
- the real ring-3 denial from `KACCESS.ELF`;
- a later successful `ZENOVAPP.ZEX` activation proving authority did not leak or remain locked;
- independent verification of the persistent denial record;
- absence of policy, activation, profile-lookup and denial-audit failure markers;
- deterministic rebuilding on the same source revision.

Expected summary marker:

```text
ZENOV_SYSCALL_CAPABILITY_GATE_OK profiles=7 persistent_denial=yes authority_leakage=no
```

## Security boundary

The capability policy is immutable kernel data. It is validated against the signed trusted path-and-digest set, but it is not itself a separately signed or dynamically updateable ZGDB record type in 0.1.1. Changing a profile requires a new verified ZenovOS build.

The current model has one foreground process, one active profile and exact path scopes. It does not yet implement transferable object capabilities, per-handle rights, directory capabilities, delegation, capability revocation during execution, concurrent processes or a user-space policy service.

The policy reduces the blast radius of a compromised trusted application. It does not make trusted application code bug-free and does not authenticate writable ZenovFS state against a complete offline image replacement.
