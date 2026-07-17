# ZenovOS 0.1.1 memory invariants

The following conditions are enforced by code and regression evidence:

1. PMM never returns low reserved frames below `0x00800000`.
2. PMM allocation returns page-aligned, unique frames and exact counters are restored after release.
3. Heap block metadata forms one contiguous doubly linked chain over the complete 2 MiB heap range.
4. An allocated heap pointer is aligned as requested and can be released exactly once.
5. The low 4 MiB mapping is supervisor-only.
6. A user page is accessible only when its PTE is present and user-enabled.
7. Kernel writes into user memory require a writable PTE because `CR0.WP` is set.
8. ELF text and rodata pages are not writable after loading.
9. Writable and executable ELF permissions may not coexist in one admitted load segment.
10. The application stack is writable and remains outside executable load ranges.
11. The complete reused 1 MiB userspace physical window is zeroed before first execution and after each exit or recoverable fault.
12. A user exception cannot resume the faulting program; it returns through the common cleanup path.

Any change to paging, loading, heap metadata or user transition assembly must preserve these invariants and their associated markers.
