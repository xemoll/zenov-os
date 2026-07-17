# Post-merge hardening delta

This document explains why the 0.1.1 completion work has a follow-up hardening change after the main P0 merge.

The initial P0 implementation passed strict compilation, host crash injection, all three QEMU phases, deterministic rebuild and deterministic packaging. A subsequent audit identified two security properties that deserved explicit code and evidence rather than inference:

1. the fixed physical userspace window must be scrubbed between applications so bytes outside the next image's declared ranges cannot survive from a previous process;
2. the ELF admission policy must reject any `PT_LOAD` segment requesting both write and execute permission, even though the bundled linker already emits separate RX and RW segments.

The follow-up implements both properties, adds boot self-tests and requires runtime evidence that the common exit/fault path scrubbed the window after every bundled application transition.

This delta does not change the on-disk ZenovFS1 format, syscall numbers, ZEX1 header or canonical Zenov application artifact. It strengthens the implementation behind the already documented 0.1.1 ABI.
