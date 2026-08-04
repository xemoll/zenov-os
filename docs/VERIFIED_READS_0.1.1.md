# ZVRT1 authenticated reads — ZenovOS 0.1.1

## Purpose

ZVRT1 is a separate signed trust domain for detecting unauthorized changes to selected persistent files before their bytes are released to a shell command, a ring-3 caller, or executable appraisal.

The verification order is intentional:

1. ZenovFS1 validates its metadata and stored file checksum.
2. ZVRT1 validates the signed file commitment.
3. ZMID1 classifies ordinary file content, or ZGDB2/ZCAP1 appraise an executable and activate its authority profile.

A ZVRT1 mismatch returns the typed filesystem status `checksum-mismatch`, clears the destination buffer, records a durable `READ-BLOCK` event in ZGAL1, and does not release the protected payload.

## Signed format

The v1 manifest uses:

- magic `ZVRT`, schema `1`;
- RSA-2048-PSS with SHA-256, MGF1-SHA-256 and a 32-byte salt;
- independent root key ID `d28215ec62269ffc`, derived from the first eight bytes of SHA-256 over the SPKI DER public key;
- an 80-byte header and 128-byte records;
- a 4 KiB leaf size;
- a complete-file SHA-256 and a Merkle root for every protected path;
- a monotonically persisted manifest floor.

Every leaf is domain-separated and binds the canonical path, complete file size, chunk index, actual chunk length and chunk bytes. Parent nodes use a different domain byte and duplicate the final node when a level has an odd count.

## Protected v1 objects

The deterministic v1 fixture protects four files and five leaves:

- `/docs/readme.txt` — one leaf;
- `/docs/release.txt` — one leaf;
- `/samples/clean.txt` — one leaf;
- `/apps/fileio.elf` — two leaves.

The executable record is deliberately larger than one leaf so the runtime test proves multichunk verification rather than only testing a trivial single-block object.

## Memory layout

ZenovOS still boots through a low-memory BIOS path and enforces a linker assertion that the kernel image must end before the VGA aperture at `0x000A0000`.

ZVRT1 therefore does not place its 4 KiB active/candidate record workspace in low BSS. Fixed supervisor-only regions are centralized in `supervisor_layout.inc`:

- heap: `0x00100000–0x002FFFFF`;
- antimalware scan workspace: `0x00300000–0x0030FFFF`;
- ZVRT records: `0x00310000–0x00310FFF`;
- PMM bitmap: `0x00311000–0x00311FFF`;
- ring-3 base: `0x40000000`.

The existing VGA linker assertion remains unchanged and is part of the strict build gate.

## Runtime failure model

A cryptographically invalid active manifest fails closed during boot before the graphical UI.

A protected file whose ZenovFS checksum has been maliciously recomputed still fails ZVRT1. The read is blocked, the caller buffer is cleared, and a persistent audit record is written before returning failure.

The QEMU gate covers:

- valid boot and two-leaf `/apps/fileio.elf` execution;
- valid single-leaf protected read;
- manifest corruption with repaired ZenovFS checksum, which must panic before UI;
- protected data corruption with repaired ZenovFS checksum, which must not disclose the original payload;
- replayable persistent ZGAL1 audit evidence after the blocked read.

## Relationship to established integrity systems

ZVRT1 follows the same broad integrity-first principles as Linux fs-verity, dm-verity and IPE: trusted metadata commits to file or block content, and access decisions are made from authenticated provenance rather than malware recognition alone.

It is not format-compatible with those Linux facilities. ZenovFS1 currently performs bounded complete-file reads, so ZVRT1 verifies the complete object after the filesystem read and before release. It does not yet provide per-page demand verification, generic Merkle paging, transparent mmap verification, a block-device verity target, hardware-backed roots, or TPM/NVRAM rollback resistance.

## Policy-state parser hardening

All version-state files now use one strict freestanding decimal parser. The format is a non-zero canonical decimal followed by exactly one newline. Overflow, leading zeroes, missing newlines, trailing bytes and `UINT32_MAX -> 0` update wrap are rejected fail-closed. Signed ZRWP and ZVRT paths also use one canonical absolute-path validator that rejects control bytes, backslashes, duplicate separators and `.` or `..` components, including terminal components.

This strengthens live-storage corruption handling. It does not create TPM/NVRAM anti-rollback: a full offline replacement of the complete data image remains outside the 0.1.1 trust boundary.
