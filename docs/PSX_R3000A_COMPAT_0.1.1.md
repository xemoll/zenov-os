# PS1 R3000A diagnostic compatibility runtime

ZenovOS 0.1.1 can execute a bounded, redistributable subset of PlayStation 1 `PS-X EXE` programs through ZenPkg. This is a real MIPS R3000A interpreter and two-MiB guest-memory environment. It is not a claim that commercial PlayStation games work.

## User interface

```text
pkg compat status
pkg compat check /samples/psx-r3000a-hello.exe
pkg compat run-psx /samples/psx-r3000a-hello.exe
pkg compat run-psx /downloads/homebrew.exe
```

Only normalized regular-file paths below `/samples/` or `/downloads/` are accepted. The input is limited to the existing 64 KiB ZenovFS snapshot and is read through the unchanged on-access security path. The ZenPkg-owned PS-X EXE preflight must accept the same bytes before the loader allocates guest RAM.

## Implemented CPU and loader subset

The shared loader verifies the eight-byte `PS-X EXE` magic, 0x800-byte header, aligned payload, entry point, load/fill ranges and stack inside the two-MiB PS1 RAM address space. KUSEG, KSEG0 and KSEG1 aliases resolve to the same isolated RAM.

The interpreter implements the R3000A integer subset needed by small freestanding diagnostics:

- fixed and variable shifts, integer logical operations, comparisons and `LUI`;
- checked and unchecked addition/subtraction;
- conditional branches, `J`, `JAL`, `JR` and `JALR` with one architectural delay slot;
- `HI`/`LO`, signed/unsigned multiply and PS1-compatible divide edge cases;
- aligned byte, half-word and word loads/stores with one-instruction load delay;
- a bounded console HLE for BIOS A0/B0 `write`, `putchar` and `puts` calls;
- `BREAK` or the sandbox syscall-zero convention as a diagnostic exit.

Unknown instructions, coprocessor/GTE access, nested branch-delay control transfer, signed overflow, misalignment, out-of-RAM access and unsupported BIOS calls stop fail-closed. Each run has a 1,000,000-instruction budget, a 16 KiB output budget and a 4 KiB maximum per HLE string/write operation.

## Guest-memory lifecycle

The runtime does not take two MiB from the general kernel heap. With no ring-3 process active, it clears and reuses the single foreground-process page table, allocates 512 physical PMM frames and maps them into a supervisor-only window at `0x00c00000`. The PTEs deliberately omit the `user` permission bit, and the interpreter never transfers control to ring 3.

After every completed or rejected interpreter run, the runtime overwrites all 2,097,152 mapped bytes, removes the page-directory entry, reloads paging and returns every frame to the PMM. QEMU executes the fixture twice in one boot so the evidence covers allocation, complete wipe, release and reuse rather than a one-shot launch.

## Deliberate limits

Not implemented:

- GPU/rendering, SPU/audio, CD-ROM/disc images, controllers, DMA, timers and interrupts;
- BIOS boot or a bundled Sony BIOS;
- GTE/coprocessor instructions, unaligned merge loads/stores or self-modifying cache behavior;
- PBP, BIN/CUE, ISO, CHD or encrypted/licensed title content;
- general homebrew or commercial game compatibility.

The in-tree fixture is generated from eight MIPS instructions and a text string. It contains no commercial game, firmware, key, copyrighted BIOS or paid component. The runtime and generator use the repository's BSD 2-Clause code and standard free build tools.

The header layout and BIOS console-call numbers were cross-checked against the public [PCSX-Redux PS-X EXE loader](https://github.com/grumpycoders/pcsx-redux/blob/main/src/supportpsx/binloader.cc) and [DuckStation R3000A core](https://github.com/stenzek/duckstation/blob/master/src/core/cpu_core.cpp). They are design references, not bundled runtime dependencies.

## Evidence

Host tests exercise the real fixture, branch and load delay semantics, overflow, alignment, out-of-range access, unsupported instructions/BIOS calls, immutable register zero and both execution budgets. The full compatibility preflight still exhaustively rejects every unsafe truncated prefix.

```text
PSX_EXE_FIXTURE_OK bytes=2104 entry=0x80010000 hle=A0:3e exit=break
PSX_R3000A_TEST_OK cases=15 cpu=integer+branch-delay+load-delay+hi-lo memory=2MiB hle=A0+B0 fail-closed=1
PACKAGE_COMPATIBILITY_PREFLIGHT_TEST_OK cases=35 truncations=4492 validators=5 runtime-ready=2 fail-closed=1
PSX_R3000A_HLE_PUTS_OK
PSX_R3000A_STOP reason=exited steps=8 output=23 memory-released=1
PSX_R3000A_EXIT code=0
PSX_GUEST_RAM_WIPED pages=512 bytes=2097152
```

The QEMU marker proves only the documented diagnostic subset. It does not prove graphics, sound, input, timing, BIOS or retail-game support.
