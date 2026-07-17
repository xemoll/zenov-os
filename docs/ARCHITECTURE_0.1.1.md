# ZenovOS 0.1.1 architecture summary

```text
BIOS
  -> FAT12 boot sector
  -> protected-mode kernel
       -> E820 PMM
       -> 4 KiB paging + CR0.WP
       -> reusable 2 MiB heap
       -> IDT/PIC/PIT/PS2/serial/VGA
       -> ATA PIO
       -> ZenovFS1 copy-on-write volume
       -> foreground ZEX1/static-ELF32 ring-3 runtime
       -> interactive shell generated/configured by Zenov
```

The kernel remains a deliberately small, statically linked i686 system. It uses one supervisor mapping and one reusable application window rather than independent address spaces. Application transitions use a TSS ring-change stack, `INT 0x80` syscalls and a common exit/fault cleanup path.

The build is self-contained: native C++ host tools create and verify the boot image, data image, ZEX1 containers, Zenov-generated application and release package. QEMU is used for executable acceptance; deterministic checks compare independently rebuilt artifacts byte-for-byte.

For details, use [`INDEX.md`](INDEX.md).
