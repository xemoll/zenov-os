# Important files in ZenovOS 0.1.1

| Path | Responsibility |
|---|---|
| `kernel/kernel.cpp` | kernel composition and boot order |
| `kernel/parts/memory.inc` | PMM, heap and paging primitives |
| `kernel/parts/user_window.inc` | process-window zeroing invariant |
| `kernel/parts/process.inc` | ZEX1/ELF loading, syscalls, stack and fault state |
| `kernel/parts/process_policy.inc` | W^X ELF admission wrapper and self-test |
| `kernel/parts/storage.inc` | ATA PIO and ZenovFS1 runtime |
| `kernel/parts/storage_tools.inc` | integrity checks and metadata sync |
| `kernel/user.S` | ring-3 entry, syscall entry and common cleanup |
| `kernel/interrupts.S` | exception/IRQ stubs and user-fault redirect |
| `tools/zenovfs_fault_test.cpp` | exhaustive interrupted-write test |
| `tools/zenov_app_compiler.cpp` | OS-side deterministic Zenov subset compiler |
| `tests/qemu_smoke.sh` | three-phase runtime acceptance |
| `Makefile` | reproducible build, static checks and deterministic comparisons |
| `.github/workflows/ci.yml` | hosted build, QEMU and evidence enforcement |

Large changes should be reviewed against [`MEMORY_INVARIANTS_0.1.1.md`](MEMORY_INVARIANTS_0.1.1.md), [`ABI_0.1.1.md`](ABI_0.1.1.md) and [`ZENOVFS1_TRANSACTIONS.md`](ZENOVFS1_TRANSACTIONS.md).
