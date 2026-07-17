# ZenovOS 0.1.1

ZenovOS is a compact 32-bit x86 operating system built with Zenov, assembler and freestanding C++17. Version 0.1.1 now boots into a real 800×600 graphical desktop on QEMU Standard VGA while retaining the text shell and serial console as diagnostic fallbacks. The system also provides deterministic BIOS/FAT12 boot, E820 physical-memory discovery, 4 KiB paging, ring-3 ZEX1 and static ELF32 applications, a reusable kernel heap, guarded syscalls and transactional ZenovFS1 storage.

![ZenovOS 0.1.1 graphical desktop](./docs/screenshots/zenov-os-0.1.1-graphical-desktop.png)

The image above is the actual 800×600 QEMU framebuffer captured by the green 0.1.1 CI run. The workflow also preserves the original PPM framebuffer, serial logs, kernel image, data image and release package as build evidence.

## Status

The implemented 0.1.1 scope has executable regression coverage for:

- QEMU Standard VGA discovery through PCI ID `1234:1111`;
- Bochs VBE `800×600×32` mode and linear framebuffer access;
- supervisor-only framebuffer MMIO mapping at `0xC0000000`;
- heap-backed double buffering, clipping, alpha blending and bitmap text;
- graphical desktop, taskbar, launcher dock and file-manager window;
- PS/2 mouse initialization, IRQ12 route validation, packet decoding, cursor movement and title-bar dragging;
- PMM and fragmented-heap stress tests;
- page-granular application mappings and `CR0.WP`;
- read-only code pages and W^X ELF admission policy;
- complete process-window scrubbing between applications;
- recoverable ring-3 faults with decoded diagnostics;
- stable syscall errors, guarded pointers, console input and `argc/argv`;
- copy-on-write ZenovFS1 replacement with exhaustive sector-write fault injection;
- deterministic `.zv` to ZEX1 compilation and ring-3 execution;
- deterministic system rebuilds and release-package provenance.

See [`docs/INDEX.md`](docs/INDEX.md) and [`docs/ROADMAP_0.1.1.md`](docs/ROADMAP_0.1.1.md).

## Graphics and desktop

The first graphical foundation is intentionally software-rendered and hardware-specific enough to remain testable:

```text
QEMU PCI VGA 1234:1111
        │
        ├── BAR0 framebuffer
        ├── Bochs VBE ports 0x01CE / 0x01CF
        └── 800×600×32 linear mode
                 │
                 ▼
supervisor MMIO window 0xC0000000
                 │
                 ▼
heap backbuffer → software renderer → full present
```

Implemented renderer primitives include rectangle fill, clipped drawing, alpha compositing, borders, bitmap glyphs, icons and complete framebuffer presentation. The visible desktop includes a top bar, launcher dock, taskbar, status indicator and movable file-manager window.

Applications do not receive direct framebuffer access. The framebuffer mapping is supervisor-only. The current desktop is still rendered by the kernel and is not yet a separate display server or compositor.

## Input

Keyboard input continues through IRQ1 and the existing event queue. Mouse support adds:

- PS/2 auxiliary-port initialization;
- IRQ12 IDT/PIC route validation;
- three-byte packet synchronization and sign extension;
- bounded cursor coordinates;
- left-button state transitions;
- window title-bar hit testing and dragging;
- deterministic decoder and drag regression tests.

CI verifies the IRQ route and the real packet-decoder/window path separately. External host-pointer delivery can vary by headless QEMU display backend, so it is not treated as the only proof of driver correctness.

## Boot loader

The BIOS FAT12 loader now crosses 64 KiB destination boundaries by advancing the load segment when `BX` wraps. This removes the former 60 KiB kernel artifact ceiling. The host image verifier validates the FAT12 cluster chain and confirms that it covers the complete kernel payload.

## Memory and isolation

- E820-backed management of the first 128 MiB;
- 4 KiB frame allocator with a 16-frame stress cycle;
- 2 MiB aligned heap with split, free, coalesce and invalid-free rejection;
- supervisor-only low kernel mapping;
- supervisor-only high framebuffer MMIO window;
- ring-3 base at `0x00400000` with a 1 MiB application limit;
- only current image and 16 KiB stack pages marked present;
- RX ELF text/rodata and writable data/BSS/stack pages;
- rejection of ELF load segments requesting both write and execute permission;
- zeroing of the reused 1 MiB process window before first use and after every normal exit or recoverable fault.

The current i686 paging mode does not provide a general per-page NX bit. W^X is an admission policy and code-write protection invariant, not complete hardware non-execution of writable data.

## Applications and ABI

Supported formats:

- ZEX1 version 1;
- static little-endian ELF32/i386 with validated `PT_LOAD` segments.

Bundled coverage:

```text
run HELLO
run FILEIO.ELF
run ARGS.ELF alpha beta
run CONSOLE.ELF
run PROTECT.ELF
run KACCESS.ELF
run ZENOVAPP.ZEX
```

Syscalls use `INT 0x80`:

```text
0 exit            5 file_stat
1 write_console   6 get_version
2 get_ticks       7 sync
3 file_read       8 read_console
4 file_write
```

A ring-3 exception terminates only the foreground application, records vector/error/EIP and page-fault CR2 data, scrubs the process window and returns to the shell. Kernel faults remain fatal.

See [`docs/ABI_0.1.1.md`](docs/ABI_0.1.1.md) and [`docs/SECURITY_MODEL_0.1.1.md`](docs/SECURITY_MODEL_0.1.1.md).

## Zenov source target

The `zenov` repository provides a strict 0.1.1 freestanding subset:

```zenov
app("name");
say("message\n");
exit(0);
```

The compiler generates deterministic ZEX1, rejects unsupported hosted-language statements, verifies the container and compiles the same source twice for a byte-identical result. ZenovOS pins the compiler revision and canonical generated-artifact SHA-256 in its build manifest, packages the app into ZenovFS1 and runs it in ring 3.

## Persistent storage

The kernel drives a primary-master ATA PIO disk and mounts ZenovFS1 at `/data`.

```text
mount df fsck sync
pwd cd ls mkdir touch
write append cat stat
cp mv rm
```

ZenovFS1 retains 128 fixed metadata entries and 64 KiB file slots. Replacement writes use a compatible copy-on-write protocol: complete payload to a free slot, staging metadata, commit metadata, then old-entry cleanup. Mount recovery discards uncommitted staging or completes committed replacement. Host tests evaluate every sector-write prefix, and QEMU boots an intentionally interrupted committed image.

See [`docs/ZENOVFS1_TRANSACTIONS.md`](docs/ZENOVFS1_TRANSACTIONS.md).

## Zenov-owned configuration and shell

`kernel/main.zv` is a composition root for modules under `kernel/config/`. Guarded relative `include(...)` directives reject cycles, absolute paths, traversal outside the source root and nesting deeper than 16 levels.

The shell remains available through VGA text memory and COM1 for diagnostics and application testing. It provides a 512-byte input buffer with 511 usable characters, 128 history entries, a 1024-event keyboard IRQ queue, horizontal scrolling and standard cursor/history editing. It is no longer the visible primary OS surface when graphics initialization succeeds.

## CI contract

The primary workflow performs:

1. strict host and freestanding compilation with warnings as errors;
2. FAT12, ZenovFS1, ZEX1 and ELF structural checks;
3. graphics, PMM, heap, process-scrub and loader-policy self-tests;
4. exhaustive host ZenovFS1 crash-boundary injection;
5. three QEMU phases for desktop boot, applications, persistence and interrupted recovery;
6. framebuffer screenshot capture;
7. deterministic system rebuilding;
8. deterministic release ZIP generation and byte comparison;
9. evidence upload with images, applications, manifest and serial logs.

Important graphical markers include:

```text
GRAPHICS_PCI_OK
FRAMEBUFFER_MAPPED_OK
GRAPHICS_MODE_OK 800x600x32
BACKBUFFER_PRESENT_OK
CLIPPING_OK
ALPHA_BLEND_OK
FONT_RENDER_OK
DESKTOP_SCENE_OK
GRAPHICAL_DESKTOP_READY
PS2_MOUSE_OK
PS2_MOUSE_IRQ_ROUTE_OK
PS2_MOUSE_DECODER_OK
MOUSE_PACKET_OK
WINDOW_DRAG_OK
```

## Build

Required: GNU Make, GNU binutils, a C++17 compiler, `qemu-system-i386`, `zip` and `unzip`.

```bash
make clean check
make qemu
make test
bash tools/package_release.sh build/zenov-os.img build/zenov-data.img dist package
```

## Release assets

The existing `v0.1.1` release assets remain the previous installable baseline until the release is rebuilt from the exact final graphical `main` commit. The final package must include both images, manifest, source revision, launchers, installation guide and checksums, then be downloaded and QEMU-verified again.

[Open the ZenovOS 0.1.1 release](https://github.com/xemoll/zenov-os/releases/tag/v0.1.1)

## Limitations

ZenovOS remains a single-foreground-process i686/BIOS system. It does not yet provide concurrent user processes, per-process page directories, a user-space display server, a compositor, a reusable GUI toolkit, GPU acceleration, SMP, networking, USB, AHCI/NVMe, a physical-disk installer, dynamic linking, PE/DOS/Win32 compatibility or ZenovFS2 variable extents.

The automated hardware target is QEMU i386 with BIOS, floppy, IDE and QEMU Standard VGA. VirtualBox is documented but is not CI-verified for the new graphical path.

## License

Original ZenovOS code is BSD-2-Clause. FAT12 loader lineage and the retained x16-PRos MIT notice are documented in `THIRD_PARTY.md`.
