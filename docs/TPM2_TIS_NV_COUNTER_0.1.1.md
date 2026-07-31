# TPM 2.0 TIS and NV counter substrate — ZenovOS 0.1.1

## Scope

This layer adds a real supervisor-only TPM 2.0 FIFO transport for the PC Client TIS MMIO interface and an explicitly provisioned monotonic NV counter. It is the hardware substrate required for a later ZPTJ1 anti-rollback anchor; it is not presented as completed signed-policy freshness.

The implementation follows the TPM 2.0 command framing and uses QEMU's `tpm-tis` frontend with a persistent `swtpm` backend for executable verification.

## Hardware boundary

ZenovOS maps the TIS locality range `0xFED40000-0xFED44fff` through a dedicated page table:

- supervisor-only;
- writable for the device register protocol;
- page cache disabled;
- page write-through enabled;
- no user mapping and no syscall exposure.

The driver uses locality 0 and bounded polling for `ACCESS`, `STS`, burst count and `DATA_FIFO`. Command and response lengths are checked before every buffer access. Responses larger than 4096 bytes or smaller than the ten-byte TPM header are rejected.

## Supported commands

The transport implements canonical big-endian codecs for:

- `TPM2_Startup`;
- `TPM2_NV_ReadPublic`;
- `TPM2_NV_DefineSpace`;
- `TPM2_NV_Read`;
- `TPM2_NV_Increment`.

The dedicated owner NV counter is:

```text
index       0x015A4F53
nameAlg     SHA-256
size        8 bytes
attributes  counter | owner/auth read | owner/auth write | no-DA
```

Every boot validates the complete public area before accepting the counter. A pre-existing index with a different algorithm, type, attributes, policy or size is rejected rather than reused.

## Provisioning policy

Ordinary boot never creates or increments TPM state automatically.

The privileged console surface is explicit:

```text
tpm status
tpm provision
tpm increment
tpm selftest
```

`provision` uses an empty owner password session and therefore succeeds only on a TPM whose owner hierarchy still permits that authorization. It defines the index at zero, validates its public area, increments it to generation one and verifies the readback. Existing canonical counters are never redefined.

`increment` reads the current 64-bit value, performs one TPM increment and requires the readback to equal the exact successor. Overflow and ambiguous results are rejected.

## Verification

The dedicated workflow runs:

- strict GCC protocol tests;
- Clang ASan, UBSan, unsigned-overflow and implicit-conversion sanitizers;
- freestanding kernel build with warnings as errors;
- a persistent `swtpm` state across three QEMU boots;
- explicit provisioning to generation 1;
- increments to generations 2 and 3;
- reboot persistence checks;
- a fourth boot without a TPM to prove compatibility;
- deterministic kernel, boot image and data image rebuilds;
- retained commands, binaries, TPM state, serial logs and QEMU evidence.

Expected runtime marker:

```text
TPM2_NV_QEMU_OK tis=mmio locality=0 provision=explicit counter=1-2-3 reboot=persistent absent=compatible
```

## Security boundary

The current layer does not yet bind ZPTJ1 commits or signed-policy generations to the NV counter. Consequently, it does not yet stop an offline attacker from restoring an older complete ZenovFS image.

That protection requires a second protocol layer:

1. hash the complete accepted policy generation;
2. prepare ZPTJ1 with old and target anchor state;
3. durably commit policy, floor, audit and anchor bytes;
4. increment the TPM counter exactly once;
5. recover by comparing the hot journal's old/target generations with the hardware counter;
6. roll back before the increment or complete forward after it;
7. fail closed on every other state.

Until that protocol is implemented and crash-tested, the TPM counter is a verified hardware primitive, not a completed policy anti-rollback claim.
