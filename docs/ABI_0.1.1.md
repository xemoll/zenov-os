# ZenovOS application ABI 0.1.1

## Address model

Applications execute in ring 3 with 32-bit code and data segments whose linear base is `0x00400000`. Application-visible pointers are 32-bit offsets inside a 1 MiB window:

```text
segment offset 0x00000000 -> linear 0x00400000
segment offset 0x000FFFFF -> linear 0x004FFFFF
```

Only pages needed by the current executable and its stack are present. The stack occupies the top 16 KiB of the window. The complete physical window is scrubbed before first use and after every application exit or recoverable fault.

## Initial stack

At `_start`, `ESP` is a segment-relative offset into the writable stack mapping:

```text
[esp + 0]  uint32_t argc
[esp + 4]  uint32_t argv_offset
```

`argv_offset` points to a null-terminated vector of segment-relative string offsets:

```text
argv[0] ... argv[argc - 1]
argv[argc] = 0
```

The shell passes at most eight arguments, including the requested application name. Each encoded argument is limited to 63 bytes plus the terminator. Quoted single- or double-quoted arguments are accepted by the shell parser; escape processing is not part of ABI 0.1.1.

## Syscall entry

Applications invoke syscalls with `INT 0x80`.

```text
EAX  syscall number
EBX  argument 1
ECX  argument 2
EDX  argument 3
ESI  argument 4
EAX  return value
```

### Syscall table

| Number | Name | Arguments | Result |
|---:|---|---|---|
| 0 | `exit` | `EBX=exit_code` | does not return to userspace |
| 1 | `write_console` | `EBX=buffer`, `ECX=length` | bytes written |
| 2 | `get_ticks` | none | PIT tick count |
| 3 | `file_read` | `EBX=path`, `ECX=output`, `EDX=capacity` | bytes read |
| 4 | `file_write` | `EBX=path`, `ECX=data`, `EDX=length`, `ESI=append` | bytes written |
| 5 | `file_stat` | `EBX=path`, `ECX=UserFileInfo*` | zero on success |
| 6 | `get_version` | `EBX=output`, `ECX=capacity` | string length |
| 7 | `sync` | none | zero on success |
| 8 | `read_console` | `EBX=output`, `ECX=capacity` | bytes read |

`UserFileInfo` is three little-endian `uint32_t` values: type, size and checksum.

All user ranges are checked for overflow, mapping presence and the permission required by the operation. Kernel writes require writable user pages; a pointer into RX code is rejected without causing a kernel page fault.

### Stable error values

| Value | Meaning |
|---|---|
| `0xFFFFFFFF` | invalid argument |
| `0xFFFFFFFE` | path or object not found |
| `0xFFFFFFFD` | destination capacity is insufficient |
| `0xFFFFFFFC` | storage or synchronization I/O failure |
| `0xFFFFFFFB` | invalid, unmapped or permission-incompatible user pointer |
| `0xFFFFFFFA` | unsupported syscall or operation |

Applications should compare exact 32-bit values rather than interpreting them as host `errno` numbers.

## ZEX1

ZEX1 begins with this packed 32-byte little-endian header:

```text
char     magic[4]       = "ZEX1"
uint32_t version        = 1
uint32_t header_size    = 32
uint32_t image_size
uint32_t entry_offset
uint32_t bss_size
uint32_t stack_size     <= 16384
uint32_t checksum       = FNV-1a-32(image)
```

The flat image is initially mapped writable for loading, then protected read-only. ZEX1 0.1.1 therefore targets code plus immutable embedded data. A future container revision is required for independently writable data/BSS unless `image_size` is page-aligned and the declared BSS occupies separate pages.

## ELF32/i386

The loader accepts static little-endian ELF32 executables for machine `EM_386` with a valid entry inside an executable `PT_LOAD` segment. It rejects:

- malformed program-header bounds or alignment;
- `p_filesz > p_memsz`;
- ranges outside the 1 MiB application window or colliding with the stack;
- byte-overlapping load segments;
- segments with conflicting write permissions on the same 4 KiB page;
- any load segment requesting both `PF_W` and `PF_X`;
- an entry page that remains writable.

Zero-sized `PT_LOAD` entries are ignored only when `p_filesz` is also zero. Dynamic linking, interpreters, relocations and shared libraries are not supported.

## Fault return contract

A ring-3 exception terminates the foreground application, records its application path, vector, error code and EIP, and returns to the shell. Page faults additionally record CR2 and decode present/write/user bits. Kernel-mode exceptions remain fatal.

The cleanup path scrubs the reused user-memory window before returning to shell code.

## Required regression evidence

```text
PROCESS_ABI_0_1_1_OK
PROCESS_ARGV_OK
SYSCALL_ERRORS_OK
SYSCALL_POINTER_GUARD_OK
CONSOLE_READ_SYSCALL_OK
ELF_WX_POLICY_OK
PAGE_PROTECTION_OK
PAGE_FAULT_DIAGNOSTICS_OK
USER_FAULT_RETURNED_TO_SHELL
USER_WINDOW_RUNTIME_SCRUB_OK
```
