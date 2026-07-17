# ZenovOS 0.1.1 test matrix

| Area | Host/static evidence | QEMU evidence |
|---|---|---|
| FAT12 boot | image structure and signature verifier | `ZENOVOS_BOOT_OK` |
| PMM | strict kernel build | `PMM_STRESS_OK`, `PMM_OK` |
| Heap | allocator boot self-test code | reuse, coalesce, invalid-free and stress markers |
| Paging | ELF layout inspection, no RWE bundled segment | page-protection and W^X policy markers |
| Process cleanup | assembly cleanup path linked | boot scrub plus at least seven runtime scrub markers |
| Syscall ABI | ARGS/CONSOLE ELF structural checks | argv, errors, pointer guards and console-input markers |
| User faults | deliberate fault application binaries | decoded page fault, supervisor block and shell return |
| ZenovFS1 base | host image verifier | mount and `fsck` |
| ZenovFS1 durability | every sector-write prefix | interrupted committed-image recovery boot |
| Zenov source app | deterministic compiler contract and canonical hash | Zenov-generated ZEX1 ring-3 markers |
| Persistence | data image and checksum validation | same shell/userspace payloads after a second QEMU process |
| Reproducibility | independent system and package rebuilds | not applicable |
| Provenance | manifest/source-revision/checksum contents | application ABI revision markers |

The workflow must fail when any required evidence is absent. A passing build alone is not sufficient for a release candidate.
