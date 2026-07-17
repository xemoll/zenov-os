# ZenovOS 0.1.1 implementation changelog

## Source and shell scale

- split the Zenov-owned system definition into guarded include modules;
- raised explicit generated-command/document budgets;
- expanded interactive input to 511 characters, 128 history entries and a 1024-event keyboard queue;
- added horizontal command-line viewport editing and long-input QEMU coverage.

## Memory and process runtime

- added E820 PMM stress verification;
- replaced the monotonic heap with a reusable split/coalesce allocator;
- changed the user mapping from a pre-writable broad window to page-granular mappings;
- enabled supervisor write protection and RX page enforcement;
- added W^X ELF admission policy;
- scrubbed the reused process window before first use and after every exit/fault;
- added recoverable ring-3 fault handling and decoded diagnostics;
- added stable syscall errors, bounded console input and `argc/argv`.

## Storage

- retained the ZenovFS1 disk layout;
- added copy-on-write file replacement using a staging slot;
- added mount-time interrupted-transaction recovery;
- added exhaustive sector-write-boundary fault injection;
- added a QEMU boot from an intentionally interrupted committed image.

## Zenov integration

- added a strict freestanding `app`/`say`/`exit` target in `zenov`;
- generated deterministic ZEX1 from `.zv` source;
- packaged and executed the result in ring 3;
- pinned compiler revision, ABI and canonical output hash in the build manifest.

## Release engineering

- expanded deterministic rebuild comparisons to every bundled application;
- embedded build manifest and source revision in the release ZIP;
- added explicit ABI, security, transaction, test-matrix and release-checklist documentation;
- replaced embedded-image README handling with a normal repository PNG.
