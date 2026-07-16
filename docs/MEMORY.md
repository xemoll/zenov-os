# ZenovOS 0.1.1 memory architecture

## Boot discovery

The 16-bit entry captures up to 32 BIOS E820 records before entering protected
mode. The kernel treats only type-1 ranges with a 32-bit base as allocatable.
Ranges are page-aligned and capped at 128 MiB in the 0.1.1 allocator.

## Physical frame allocator

- frame size: 4 KiB
- bitmap coverage: 128 MiB
- low reserved region: `0x00000000` through `0x007FFFFF`
- first allocatable address: `0x00800000`
- allocation policy: first free frame
- free validation: aligned, within the managed range and currently allocated

The low reservation contains BIOS structures, the boot sector, kernel image,
kernel BSS/stack, page tables, bump heap and the current application window.

At startup the allocator performs an allocate/free accounting self-test. The
kernel emits `PMM_OK` only after frame counts and byte counts return to their
original values.

## Paging

ZenovOS uses 4 KiB pages and enables `CR0.PG` together with `CR0.WP`.

| Linear range | Mapping | Privilege |
|---|---|---|
| `0x00000000`–`0x003FFFFF` | identity | supervisor read/write |
| `0x00400000`–`0x007FFFFF` | identity | user read/write |
| remaining address space | not present | none |

The application GDT segment still limits ring-3 offsets to 1 MiB, so current
applications can access only `0x00400000`–`0x004FFFFF` even though the page table
covers a larger future expansion area.

The kernel page-directory and page-table pages are 4 KiB aligned and located in
kernel BSS. `CR3`, mapping sizes and paging state are visible through `vm`.
Physical-frame statistics are visible through `pmm`.

## Current boundaries

0.1.1 establishes paging and physical-memory accounting but does not yet provide:

- per-process address spaces;
- demand paging;
- copy-on-write;
- page swapping;
- dynamic kernel mappings;
- pageable user stacks;
- preemptive process scheduling.

The next process architecture should allocate user frames through PMM and create
a dedicated page directory for each process instead of reusing the fixed window.
