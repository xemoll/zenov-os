# Linux/i386 minimal compatibility runtime

ZenovOS 0.1.1 contains a deliberately small, built-in Linux/i386 execution slice. It is a real ring-3 runtime path, not a format-classification claim, but it is not a general Linux or POSIX environment.

## User interface

The runtime is exposed through ZenPkg:

```text
pkg compat status
pkg compat check /samples/linux-i386-hello.elf
pkg compat run-linux /samples/linux-i386-hello.elf
pkg compat run-linux /downloads/program.elf argument1 argument2
```

Only normalized regular-file paths under `/samples/` and `/downloads/` are accepted. ZenPkg payloads, repository state, installed applications, security policy, cache and quarantine paths cannot enter this unsigned sandbox path.

## Accepted ELF contract

The loader accepts only:

- little-endian ELF32 for `EM_386`;
- System V or Linux OSABI version 0;
- `ET_EXEC` with bounded `PT_LOAD` segments;
- an executable entry point inside a load segment;
- a total rebased image smaller than the existing ring-3 process window;
- separated writable and executable pages.

`PT_INTERP`, `PT_DYNAMIC`, W+X segments, overlapping segments, incompatible page permissions, malformed offsets, oversized images and other architectures are rejected before entry. Conventional i386 virtual addresses such as `0x08048000` are mapped through dedicated user GDT descriptors into the isolated ZenovOS process window; kernel pages remain supervisor-only.

## Implemented syscall ABI

The runtime accepts the Linux i386 `int 0x80` register convention for this fixed subset:

| EAX | Call | Implemented behavior |
| ---: | --- | --- |
| 1 | `exit(status)` | End the foreground guest process |
| 4 | `write(fd, buffer, size)` | Write only to fd 1 or 2 through the ZenovOS console |
| 252 | `exit_group(status)` | Same foreground-process termination boundary |

`write` to any other descriptor returns `-EBADF`. Every other syscall returns `-ENOSYS`; it is not forwarded to the native ZenovOS syscall table. The active capability mask contains only `console-write`, with no file, network, clock, input, synchronization or package-management capability.

The initial stack contains `argc`, `argv[]`, a null environment vector and an `AT_NULL` auxiliary-vector terminator. There is no dynamic linker, libc, TLS, signal delivery, `brk`, `mmap`, threads, processes or filesystem ABI.

## Security lifecycle

Before execution the runtime:

1. normalizes and checks the input path;
2. performs a bounded ZenovFS read through verified-read and on-access security mediation;
3. requires the package-manager-owned exact static ELF preflight and W^X policy;
4. computes SHA-256 over the exact bytes used by the loader;
5. activates a digest-bound compiled console-only capability profile;
6. maps the image and stack into the existing ring-3 paging window;
7. clears mappings, the ABI selector and capability profile on return or fault.

ZenovGuard remains unchanged. Its existing on-access mediation is used as-is; ZenPkg owns the foreign-format structural decision.

This unsigned sandbox does not weaken the signed ZenRepo installation and native execution path.

## Verification

`make zenpkg-foreign-check` runs the shared syscall translator tests plus the exact kernel ELF policy against a conventional `0x08048000` fixture. Negative vectors cover a W+X segment, x86-64 machine ID, dynamic segment and invalid entry point.

`make zenpkg-foreign-qemu` additionally boots ZenovOS and requires evidence that the guest receives `-ENOSYS` for `open`, `-EBADF` for `write` to fd 3, successfully writes to stdout and exits through `exit_group(0)`:

```text
ZENPKG_COMPAT_READY validators=5 runtimes=1 fail-closed=1 antivirus-unchanged=1
ZENPKG_COMPAT_PREFLIGHT_OK format=elf structural=1 runtime=1 trust=0 verdict=runnable-sandbox
LINUX_I386_SYSCALL_ENOSYS number=5
LINUX_I386_SYSCALL_DENIED syscall=write reason=fd
LINUX_I386_COMPAT_WRITE_OK
LINUX_I386_COMPAT_EXIT code=0
```

## Cost and licensing boundary

The compatibility layer is in-tree under the repository's BSD 2-Clause license. It has no subscription, cloud service, telemetry, paid runtime or proprietary SDK dependency.

Future Windows, macOS and console providers should prefer redistributable open-source components. Encrypted or licensed game content, firmware and platform keys remain user-owned or vendor-provided inputs; ZenovOS must not bundle them or bypass platform protections.
