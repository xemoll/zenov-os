# ZenovOS 0.1.1 native application ABI

ZenovOS 0.1.1 executes validated ZEX1 containers and static ELF32/i386 files in
ring 3. Neither format provides Windows PE or DOS MZ compatibility.

## Execution model

- CPU: i686, 32-bit protected mode
- privilege: ring 3
- code selector: `0x1B`
- data and stack selector: `0x23`
- linear user window: `0x00400000` through `0x004FFFFF`
- segment-relative address range: `0x00000000` through `0x000FFFFF`
- initial stack pointer: `0x000FF000`
- system-call gate: `INT 0x80`
- memory pages: present, writable and user-accessible within the mapped window
- scheduling: one foreground application at a time

Application pointers passed in registers are segment-relative offsets. The kernel
validates each complete range before translating it to the linear user window.

## ZEX1 container

All fields are little-endian. The header is exactly 32 bytes.

```c
struct ZexHeader {
    char magic[4];          // "ZEX1"
    uint32_t version;       // 1
    uint32_t header_size;   // 32
    uint32_t image_size;    // flat image bytes following the header
    uint32_t entry_offset;  // initial EIP within the user segment
    uint32_t bss_size;      // zero-filled bytes after the image
    uint32_t stack_size;    // requested stack budget
    uint32_t checksum;      // FNV-1a over the flat image
};
```

The loader rejects an incorrect magic, unsupported version, oversized image,
out-of-range entry point, invalid stack request or checksum mismatch.

## ELF32 profile

The 0.1.1 ELF loader accepts a deliberately narrow profile:

- ELF class 32;
- little-endian;
- `ET_EXEC`;
- `EM_386`;
- static image with at least one `PT_LOAD` segment;
- segment virtual addresses relative to the 1 MiB user window;
- no interpreter, dynamic linking or relocations at runtime;
- every program-header and segment range validated before copying.

The loader zeroes each `PT_LOAD` region between `p_filesz` and `p_memsz`. Files
outside this profile are rejected.

## System calls

System calls use `EAX` for the call number. Return values are written to `EAX`.
`0xFFFFFFFF` represents failure unless stated otherwise.

| EAX | Name | Inputs | Result |
|---:|---|---|---|
| 0 | `exit` | `EBX` = exit code | returns to the kernel shell |
| 1 | `write_console` | `EBX` = buffer offset, `ECX` = byte count | bytes written |
| 2 | `get_ticks` | none | PIT ticks since boot |
| 3 | `file_read` | `EBX` = path offset, `ECX` = output offset, `EDX` = capacity | bytes read |
| 4 | `file_write` | `EBX` = path, `ECX` = data, `EDX` = size, `ESI` = append flag | bytes written |
| 5 | `file_stat` | `EBX` = path, `ECX` = `UserFileInfo` output | `0` on success |
| 6 | `system_version` | `EBX` = output, `ECX` = capacity | version string length |
| 7 | `sync` | none | `0` after metadata flush |

`file_stat` writes:

```c
struct UserFileInfo {
    uint32_t type;       // 1 file, 2 directory
    uint32_t size;
    uint32_t checksum;
};
```

Paths are null-terminated strings of at most 95 bytes. File calls operate on the
mounted `/data` ZenovFS volume. The application never receives raw ATA access.

## Reference applications

`HELLO.ZEX` validates the compact container and console syscall:

```text
run HELLO
```

`FILEIO.ELF` validates the ELF loader plus version, write, stat, read and sync
calls. It creates `/data/apps/userio.txt`, verifies the payload and exits cleanly:

```text
run FILEIO.ELF
```

## Stability and limitations

The syscall numbers above form the 0.1.1 ABI. Future incompatible changes require
an explicit ABI revision.

Version 0.1.1 still lacks preemptive multitasking, per-process page directories,
file descriptors, process spawning, signals, shared libraries and dynamic
linking. Paging now enforces kernel/user page permissions, while the GDT retains
the 1 MiB application boundary.

## `.exe` compatibility

A filename ending in `.exe` does not make a program compatible. ZenovOS does not
implement the DOS MZ environment or the Windows PE/Win32 environment. Unsupported
files are rejected rather than executed in kernel mode.
