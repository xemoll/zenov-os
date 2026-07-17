# ZenovOS 0.1.1 support matrix

| Component | Supported |
|---|---|
| CPU mode | 32-bit protected mode, i686-compatible |
| Firmware | legacy BIOS |
| Boot media | 1.44 MiB FAT12 floppy image |
| Automated VM | QEMU `qemu-system-i386` |
| Documented VM | VirtualBox with floppy + IDE data disk |
| Console | VGA text + COM1 serial |
| Input | PS/2 keyboard |
| Timer | PIT 100 Hz |
| Persistent disk | ATA primary-master PIO |
| Filesystem | ZenovFS1 fixed-slot volume |
| Application formats | ZEX1 v1, static ELF32/i386 |
| Application concurrency | one foreground application |
| Language target | strict Zenov `app`/`say`/`exit` subset |

Anything outside this table is unsupported unless a later version adds an explicit contract and executable test.
