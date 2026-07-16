# ZenovOS ZEX1 application ABI

ZEX1 is the first native executable format for ZenovOS 0.1.0. It is intentionally
small, deterministic and independent from Windows PE and DOS MZ executables.

## Execution model

- CPU: i686, 32-bit protected mode
- privilege: ring 3
- code selector: `0x1B`
- data and stack selector: `0x23`
- linear user window: `0x00400000` through `0x004FFFFF`
- segment-relative application address: `0x00000000` through `0x000FFFFF`
- initial stack pointer: `0x000FF000`
- system-call gate: `INT 0x80`
- one foreground process at a time in 0.1.0

The user code and data descriptors have a 1 MiB byte-granular limit. Application
pointers passed to the kernel are segment-relative offsets, not kernel linear
addresses.

## Container header

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

## System calls

System calls use `EAX` for the call number. Return values are written to `EAX`.

| EAX | Name | Inputs | Result |
|---:|---|---|---|
| 0 | `exit` | `EBX` = exit code | returns to the kernel shell |
| 1 | `write_console` | `EBX` = buffer offset, `ECX` = byte count | bytes written or `0xFFFFFFFF` |
| 2 | `get_ticks` | none | PIT ticks since boot |

`write_console` validates the complete user buffer against the 1 MiB user
segment before reading it.

## Application build

The reference application is assembled and linked independently from the
kernel:

```bash
as --32 user/hello.S -o build/hello-user.o
ld -m elf_i386 -T user/linker.ld -o build/hello-user.elf build/hello-user.o
objcopy -O binary build/hello-user.elf build/hello-user.bin
build/zex-pack build/hello-user.bin build/HELLO.ZEX
```

The deterministic ZenovFS builder installs it as `/data/apps/hello.zex`.
Inside ZenovOS it is launched with:

```text
run HELLO
```

## Current limitations

ZEX1 in version 0.1.0 does not yet provide paging, multiple simultaneous
processes, signals, dynamic linking, shared libraries, file descriptors or
process spawning. Those features require a later ABI revision rather than an
undocumented change to ZEX1.

## `.exe` compatibility

A filename ending in `.exe` does not make a program compatible. ZenovOS does
not currently implement either:

- the DOS MZ execution environment and DOS interrupt services; or
- the Windows PE loader and Win32/NT APIs.

Such files are rejected rather than executed in ring 0.
