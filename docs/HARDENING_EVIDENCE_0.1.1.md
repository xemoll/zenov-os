# Hardening evidence required

The post-merge hardening evidence is intentionally small and additive:

- the complete process window is zeroed and unmapped at boot;
- every bundled exit/fault transition executes the same scrub routine;
- the loader rejects W+X ELF segments in a direct self-test;
- all previously green P0, filesystem, QEMU, determinism and provenance checks remain green.
