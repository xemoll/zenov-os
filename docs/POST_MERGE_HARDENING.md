# Post-merge hardening

The main P0 completion passed strict compilation, host fault injection, three QEMU phases, deterministic rebuilding and deterministic packaging. A subsequent audit identified two security properties that needed explicit implementation and evidence:

1. the fixed physical userspace window must be scrubbed between applications so bytes outside the next image's declared ranges cannot survive from a previous execution;
2. the ELF admission policy must explicitly reject any `PT_LOAD` requesting both write and execute permission, even though bundled linkers already emit separate RX and RW segments.

The follow-up adds both properties, a boot scrub self-test, a W^X policy self-test and a CI requirement for at least seven runtime scrub transitions covering normal exits and recoverable faults.

It does not change syscall numbers, the ZEX1 header, the ZenovFS1 disk format or the canonical Zenov-generated application artifact.
