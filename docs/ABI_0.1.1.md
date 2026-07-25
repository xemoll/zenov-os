# ZenovOS application ABI 0.1.1

## Address model

Applications execute in ring 3 with 32-bit code and data segments whose linear base is `0x00400000`. Application-visible pointers are offsets inside a 1 MiB window. Only pages required by the current executable and its 16 KiB stack are present. The complete physical window is scrubbed before first use and after every application exit or recoverable fault.

## Initial stack

At `_start`:

```text
[esp + 0]  uint32_t argc
[esp + 4]  uint32_t argv_offset
```

`argv_offset` points to a null-terminated vector of segment-relative string offsets. The shell passes at most eight arguments, including the application name; each encoded argument is limited to 63 bytes plus the terminator.

## Syscalls

Applications use `INT 0x80`; `EAX` contains the number and result, while `EBX`, `ECX`, `EDX` and `ESI` contain arguments.

| Number | Name | Arguments | Result |
|---:|---|---|---|
| 0 | `exit` | `EBX=code` | returns to shell |
| 1 | `write_console` | `EBX=buffer`, `ECX=length` | bytes written |
| 2 | `get_ticks` | none | PIT ticks |
| 3 | `file_read` | `EBX=path`, `ECX=output`, `EDX=capacity` | bytes read |
| 4 | `file_write` | `EBX=path`, `ECX=data`, `EDX=length`, `ESI=append` | bytes accepted |
| 5 | `file_stat` | `EBX=path`, `ECX=UserFileInfo*` | zero |
| 6 | `get_version` | `EBX=output`, `ECX=capacity` | string length |
| 7 | `sync` | none | zero |
| 8 | `read_console` | `EBX=output`, `ECX=capacity` | bytes read |

The syscall table is an ABI surface, not an automatic grant. After final-read trust appraisal, each bundled application receives a per-application capability mask from the active RSA-PSS-signed ZCAP1 policy. File operations additionally require an exact normalized path scope. See [`SYSCALL_CAPABILITIES_0.1.1.md`](SYSCALL_CAPABILITIES_0.1.1.md).

`exit` remains available to every process. Unknown syscall numbers preserve the unsupported-operation result. A known syscall denied by the active profile returns `ERROR_DENIED` and creates a mandatory persistent security record.

Stable errors:

```text
0xFFFFFFFF ERROR_INVALID       invalid argument, path or object type
0xFFFFFFFE ERROR_NOT_FOUND     path does not exist
0xFFFFFFFD ERROR_NO_SPACE      output capacity or persistent capacity is insufficient
0xFFFFFFFC ERROR_IO            device unavailable, timeout or other transport I/O failure
0xFFFFFFFB ERROR_FAULT         invalid/unmapped/non-writable user pointer
0xFFFFFFFA ERROR_UNSUPPORTED   unsupported syscall or operation
0xFFFFFFF9 ERROR_DENIED        capability, path-scope, protected-path or security-policy denial
0xFFFFFFF8 ERROR_CORRUPT       checksum mismatch or structurally invalid filesystem metadata
```

The typed ZenovFS result boundary preserves the distinction between lookup failure, capacity failure, policy denial, corruption and transport I/O. `file_read` clears bytes copied before a final transport failure and clears the complete returned payload after checksum failure. A security appraisal denial also clears the output and returns `ERROR_DENIED`; it is never reported as `ERROR_NOT_FOUND`.

A transactional write can be committed while post-commit cleanup still requires recovery. This state is treated as a successful write for the application-visible byte count, is recorded internally as `recovery-pending`, and is finalized by the normal ZenovFS recovery pass before the volume is exposed again.

All user ranges are checked for overflow, mapping presence and required write permission. Capability checks do not replace pointer validation: an authorized operation with an invalid pointer still returns the pointer fault error.

## Capability lifetime

The active syscall profile is cleared before every launch attempt. A profile is installed only after ZGDB2, executable-policy, path-and-SHA-256 trust and persistent execution-audit checks succeed on the bytes consumed by the loader. It is cleared again after normal exit, recoverable fault or load failure.

There is one active profile because ZenovOS 0.1.1 has one foreground process. Authority is not inherited from the preceding process and is not derived from executable format alone.

## Executable formats

ZEX1 uses the documented packed 32-byte version-1 header, FNV-1a image checksum and a maximum 16 KiB stack declaration.

The ELF loader accepts static little-endian ELF32/i386 with a valid entry in an executable `PT_LOAD`. It rejects malformed bounds/alignment, `p_filesz > p_memsz`, ranges outside the process window, overlapping segments, page-level write-permission conflicts, W+X load segments and a writable entry page. Dynamic linking, interpreters and relocations are unsupported.

## Fault contract

A ring-3 exception terminates the foreground application, records its identity, vector, error code and EIP, and returns to the shell. Page faults additionally record CR2 and present/write/user bits. Kernel exceptions remain fatal. The common exit/fault path scrubs the reused process window and clears syscall authority before returning.
