# Known limits of ZenovOS 0.1.1

- One foreground application executes at a time.
- Applications share one fixed physical userspace window, which is scrubbed between executions; there are no separate process page directories.
- There is no scheduler, process spawning or IPC model.
- The current i686 paging mode has no general hardware NX bit; W^X is a loader policy and code-write protection invariant.
- ZEX1 is a flat immutable-image format; independently writable initialized data requires static ELF32 or a future ZEX revision.
- ELF support is static only: no interpreter, dynamic section processing, relocation loader or shared libraries.
- ZenovFS1 uses 128 fixed entries and 64 KiB slots. Its copy-on-write protocol covers single-file replacement, not arbitrary multi-object transactions.
- ATA access is primary-master PIO only.
- Boot is legacy BIOS from a FAT12 floppy image.
- The automated hardware target is QEMU i386; VirtualBox is documented but not CI-verified.
- No networking, USB, graphics, SMP, AHCI/NVMe, secure boot, executable signatures, PE/DOS/Win32 compatibility or physical-disk installer is included.

These limits are part of the 0.1.1 contract and should not be hidden by broad claims such as "fully secure", "general-purpose" or "production operating system".
