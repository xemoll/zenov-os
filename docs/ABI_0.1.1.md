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
| 4 | `file_write` | `EBX=path`, `ECX=data`, `EDX=length`, `ESI=append` | bytes written |
| 5 | `file_stat` | `EBX=path`, `ECX=UserFileInfo*` | zero |
| 6 | `get_version` | `EBX=output`, `ECX=capacity` | string length |
| 7 | `sync` | none | zero |
| 8 | `read_console` | `EBX=output`, `ECX=capacity` | bytes read |

Stable errors:

```text
0xFFFFFFFF invalid argument
0xFFFFFFFE not found
0xFFFFFFFD insufficient capacity
0xFFFFFFFC I/O failure
0xFFFFFFFB invalid/unmapped/non-writable user pointer
0xFFFFFFFA unsupported operation
```

All user ranges are checked for overflow, mapping presence and required write permission.

## Executable formats

ZEX1 uses the documented packed 32-byte version-1 header, FNV-1a image checksum and a maximum 16 KiB stack declaration.

The ELF loader accepts static little-endian ELF32/i386 with a valid entry in an executable `PT_LOAD`. It rejects malformed bounds/alignment, `p_filesz > p_memsz`, ranges outside the process window, overlapping segments, page-level write-permission conflicts, W+X load segments and a writable entry page. Dynamic linking, interpreters and relocations are unsupported.

## Fault contract

A ring-3 exception terminates the foreground application, records its identity, vector, error code and EIP, and returns to the shell. Page faults additionally record CR2 and present/write/user bits. Kernel exceptions remain fatal. The common exit/fault path scrubs the reused process window before returning.
