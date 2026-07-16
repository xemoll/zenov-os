# ZenovOS 0.1.1 Revision 2 architecture

ZenovOS is a compact BIOS-booted i686 operating system. Revision 2 keeps the
product version at 0.1.1 and hardens the boundary between the kernel, native
applications and persistent storage.

## Boot path

1. A 512-byte BIOS boot sector locates `KERNEL.BIN` in a deterministic FAT12
   image and loads it at physical address `0x00010000`.
2. The 16-bit assembly entry collects up to 32 E820 records, enables A20, loads
   the GDT and enters 32-bit protected mode.
3. The 32-bit entry clears `.bss`, installs the dedicated kernel stack and calls
   the freestanding C++ kernel.
4. The kernel installs the IDT, initializes the E820 physical-frame allocator,
   enables 4 KiB paging with `CR0.WP`, probes ATA0 and mounts ZenovFS1.
5. Every mounted ZenovFS entry and file checksum is verified before runtime
   configuration or applications may use the volume.
6. `/data/config/system.ini` is parsed and the persistent console theme is
   applied. Invalid or unavailable storage falls back to built-in defaults.
7. The TSS, syscall gate, PIC, PIT and PS/2 keyboard are initialized before the
   system console becomes interactive.

## Privilege and memory model

- Kernel code and data execute in ring 0.
- Native ZEX1 and static ELF32/i386 applications execute in ring 3.
- The low 4 MiB identity map is supervisor-only.
- The 1 MiB application window is mapped at linear address `0x00400000` and is
  user-accessible.
- GDT user descriptors constrain segment-relative addresses to the same 1 MiB
  window.
- A dedicated 16 KiB TSS transition stack receives syscalls and ring-3 CPU
  exceptions. It is separate from the suspended kernel C call stack.
- `user_enter` saves the exact kernel callee-saved context. Normal exit and
  contained user exceptions share one trusted restoration path.

Revision 2 still runs one foreground application at a time. It does not yet
provide per-process page directories, a scheduler, `fork`, `spawn`, `wait`,
signals or independent address spaces.

## Exception policy

CPU exceptions are classified by the interrupted privilege level:

- an exception from ring 0 is a kernel fault and enters the panic path;
- an exception from ring 3 terminates only the active application when a saved
  kernel return frame is present;
- the kernel records vector, error code, user EIP and CR2 for page faults;
- the application receives the conventional exit status `128 + vector`;
- the console and storage remain available after restoration.

`FAULT.ELF` executes `UD2` to generate vector 6. CI requires
`APP_FAULT_RECOVERED vector=6`, `KERNEL_SURVIVED_USER_FAULT`, a working `status`
command afterwards, and the absence of `ZENOVOS KERNEL PANIC`.

## Storage model

The immutable boot path and writable data path are deliberately separate:

```text
FAT12 floppy image
└── BOOT.BIN + KERNEL.BIN        BIOS-loaded, read-only at runtime

ATA primary master
└── ZenovFS1 mounted at /data    persistent read-write files and applications
```

ZenovFS1 uses a fixed superblock, 128 fixed metadata entries and one 64 KiB slot
per entry. Complete reads verify FNV-1a checksums. Revision 2 performs a full
boot-time integrity pass; a payload checksum failure disables `/data` before
`system.ini` is read and starts the console in degraded mode.

This is a bounded development/recovery filesystem. It has no journal,
permissions, ownership, timestamps, dynamic extents or atomic multi-entry
transactions.

## Application and syscall boundary

The kernel accepts:

- ZEX1 containers with a validated 32-byte header and FNV-1a payload checksum;
- static little-endian `ET_EXEC` ELF32/i386 images with validated `PT_LOAD`
  ranges inside the user window.

Applications enter kernel services through `INT 0x80`. Revision 2 retains the
0.1.1 syscall numbers for exit, console output, ticks, file read/write/stat,
version and metadata sync. User pointers are segment-relative and every complete
range is checked before access.

Windows PE and DOS MZ `.exe` files are not compatible with this ABI.

## Language split

- **Zenov (`kernel/main.zv`)**: canonical product version, boot messages, static
  commands and read-only system documents.
- **Assembler**: BIOS loader, mode transition, GDT, ISR/IRQ/syscall entry,
  userspace entry and trusted return paths.
- **Freestanding C++17**: PMM, paging, IDT/PIC/PIT, ATA, ZenovFS, settings,
  application loaders, syscalls, console and diagnostics.
- **Native host C++17 tools**: Zenov stage0 compiler, FAT12 builder/verifier,
  ZenovFS builder/verifier and ZEX packer.

No Python source or runtime is part of the build, tests, images or kernel.

## Verification model

GitHub Actions performs:

1. strict `-Werror -Wpedantic` native builds;
2. host verification of valid and deliberately corrupted images;
3. a normal QEMU boot with ZEX1, ELF32, file syscalls and a deliberate ring-3
   invalid opcode;
4. a second QEMU process using the same disk to prove persistence;
5. a third QEMU process with a valid filesystem structure but corrupted payload,
   proving boot-time quarantine and degraded console operation;
6. byte-identical system, data, application and release-package rebuilds.
