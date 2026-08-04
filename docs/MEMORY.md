# ZenovOS 0.1.1 memory architecture

## Boot discovery

The 16-bit entry captures up to 32 BIOS E820 records before entering protected
mode. The kernel treats only type-1 ranges with a 32-bit base as allocatable.
Ranges are page-aligned and capped at 128 MiB in the 0.1.1 allocator.

## Physical frame allocator

- frame size: 4 KiB
- bitmap coverage: 128 MiB
- permanently reserved low region: `0x00000000` through `0x007FFFFF`
- first allocatable frame: `0x00800000`
- allocation policy: first free frame
- free validation: aligned, managed and currently allocated
- release policy: user pages and page-table frames are zeroed before reuse

The low reservation contains BIOS structures, the boot image, kernel image,
linker-owned kernel state, security workspaces, eight guarded kernel stacks and
a 1 MiB compatibility window for single foreground applications.

## Kernel virtual memory

ZenovOS uses 4 KiB pages and enables both `CR0.PG` and `CR0.WP`. The first
128 MiB are identity mapped supervisor-only using 32 page tables. This gives the
kernel direct access to every PMM-managed frame while preventing ring-3 access
to physical RAM.

| Linear range | Mapping | Privilege |
|---|---|---|
| `0x00000000`–`0x07FFFFFF` | physical identity | supervisor read/write |
| `0x40000000`–`0x400FFFFF` | active task page table | user, per-page permissions |
| `0xC0000000` graphics window | selected framebuffer pages | supervisor |
| TPM TIS MMIO window | device pages | supervisor, uncached |

## Per-task address spaces

Every scheduled task owns a separate PMM-backed page table and separately
allocated image, BSS and stack frames. A context switch changes the user PDE and
reloads CR3; kernel mappings remain shared and supervisor-only. The application
GDT still exposes a 1 MiB segment-relative ABI, so application pointers remain
offsets from zero even though their linear base is now `0x40000000`.

The compatibility foreground launcher uses reserved physical pages at
`0x00600000–0x006FFFFF`. Scheduled batches do not share those pages.

## Kernel stacks

Eight task slots have independent 16 KiB ring-0 stacks. Each stack is preceded
by an unmapped 4 KiB guard page. TSS `ESP0` is updated on every task switch so an
interrupt or syscall enters the kernel on the stack owned by the interrupted
task.

## Remaining boundaries

This pass provides physical-frame-backed process isolation and preemptive
single-core scheduling. It still does not implement demand paging, copy-on-write,
swapping, ASLR, SMP scheduling, pageable kernel memory or a user-space pager.
