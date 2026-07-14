# ZenovOS

ZenovOS is the operating-system implementation track for the **Zenov** language.
The language name is always `Zenov`, source files retain the `.zv` extension,
and the command-line tool remains `zenov`.

This repository contains a verified first vertical slice, not a claim that the
full operating system is complete. The slice compiles `kernel/main.zv` through
a deterministic stage0 bare-metal compiler, links a BIOS kernel, creates a
FAT12 disk image, and boots it through a FAT12 loader derived from x16-PRos.

## Current verified surface

- Zenov `.zv` source controls the boot banner, shell prompt and command table.
- The stage0 compiler has parser, validation and determinism tests.
- GNU binutils produce a 16-bit flat kernel and 512-byte boot sector.
- A pure-Python builder creates the 1.44 MiB FAT12 image deterministically.
- QEMU CI checks the serial marker `ZENOVOS_BOOT_OK` and captures a framebuffer
  screenshot.
- The shell currently implements generated text commands plus clear, reboot and
  halt builtins.

The handwritten BIOS runtime remains stage0 scaffolding. It is intentionally
kept separate from the `.zv` program so it can be replaced incrementally by the
production `x86_64-zenov-none` backend and Zenov-owned runtime.

## Build

Required host tools:

```text
python3
GNU as
GNU ld
```

Build the image:

```bash
git clone https://github.com/xemoll/zenov-os.git
cd zenov-os
./build.sh --clean
```

Outputs:

```text
build/BOOT.BIN
build/KERNEL.BIN
build/kernel.generated.asm
build/zenov-os.img
build/build-manifest.json
```

Run all checks, including QEMU when it is installed:

```bash
./build.sh --clean --test
```

## Source contract

The current stage0 subset accepts exactly one `fn main()` and these calls:

```text
console_clear()
console_set_color(integer)
console_print(string)
console_println(string)
shell_prompt(string)
shell_command(name, response)
shell_builtin(name, id)
shell_run()
halt()
```

`say "text";` is accepted as `console_println("text")`.

Builtin shell IDs are:

```text
1 clear screen
2 reboot
3 halt
```

Unsupported syntax fails compilation. It is not silently ignored.

## Migration path

1. Keep this BIOS/FAT12 slice as a regression target and educational analogue
   to x16-PRos.
2. Integrate `TargetSpec` into the normal Zenov compiler driver.
3. Implement `--target=x86_64-zenov-none --emit=obj` with ELF relocations and no
   Linux syscalls or red zone.
4. Replace the generated assembly stage with native Zenov object generation.
5. Introduce Limine, paging, interrupts, allocator, framebuffer, VFS and ELF
   userland.
6. Port shell and applications from the 16-bit compatibility profile to the
   64-bit kernel profile.

## Licensing

ZenovOS is licensed under MIT; see `LICENSE`. The FAT12
loader is derived from MIT-licensed x16-PRos; see `THIRD_PARTY.md`.


## Repository separation

ZenovOS is maintained independently from the Zenov compiler repository. See `REPOSITORY.md`.
