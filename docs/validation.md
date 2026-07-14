# ZenovOS 0.1.0 validation contract

Every deep-update commit must prove that the repository contains no Python
source, native host tools compile with strict C++17 warnings, the freestanding
32-bit ELF has no undefined symbols, the FAT12 image validates, and QEMU reaches
the interactive `zenov> ` prompt through the protected-mode kernel.

Required CI evidence:

1. Zenov stage0 positive and negative self-tests;
2. 512-byte boot sector with `0xAA55` signature;
3. bounded `KERNEL.BIN` and inspectable `kernel.elf`;
4. deterministic 1.44 MiB FAT12 image;
5. COM1 markers `ZENOVOS_BOOT_OK`, `Kernel online`, and `zenov> `;
6. non-empty QEMU screendump;
7. byte-identical rebuild manifest;
8. published image, ELF, map, manifest, serial log and screenshot artifacts.
