# ZenovOS 0.1.1 compatibility contract

## Compatible inputs

- ZenovOS ZEX1 version 1 containers respecting the 16 KiB stack limit;
- static little-endian ELF32/i386 executables built for the documented 1 MiB segment-relative window;
- Zenov source accepted by the strict `zenov-os-app --abi 0.1.1` subset compiler;
- existing ZenovFS1 images using the original superblock and 64-byte entry layout.

## Intentionally incompatible inputs

- arbitrary Linux ELF executables requiring a system interpreter, relocations, shared libraries or Linux syscalls;
- Windows PE/PE32+ and DOS MZ executables;
- ELF load segments requesting simultaneous write and execute permission;
- ELF images whose load ranges collide, exceed the application window or share a page with conflicting write permissions;
- ZEX1 containers declaring a stack larger than 16 KiB;
- hosted Zenov language constructs outside the documented freestanding app subset.

## Cross-repository pin

The ZenovOS build manifest records the exact merged `zenov` repository commit used for the 0.1.1 app target and the canonical SHA-256 of the generated `ZENOVAPP.ZEX`. A compiler change that alters this artifact requires an explicit ABI/contract update in both repositories.
