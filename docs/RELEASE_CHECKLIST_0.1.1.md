# ZenovOS 0.1.1 release checklist

## Source state

- [ ] post-hardening pull request merged into `main`;
- [ ] final `main` commit recorded;
- [ ] matching Zenov compiler revision exists in `xemoll/zenov` main;
- [ ] working tree contains no generated or untracked release inputs.

## Automated verification

- [ ] strict host tools build with `-Wall -Wextra -Werror -Wpedantic`;
- [ ] freestanding i686 kernel build with warnings as errors;
- [ ] no undefined kernel/application symbols;
- [ ] FAT12 image verifier passes;
- [ ] ZenovFS1 verifier passes;
- [ ] ZenovFS1 fault-injection test passes every write boundary;
- [ ] QEMU application/persistence phase passes;
- [ ] QEMU reboot persistence phase passes;
- [ ] QEMU interrupted-transaction recovery phase passes;
- [ ] user-window boot scrub and at least seven runtime scrubs are observed;
- [ ] W^X loader-policy self-test is observed;
- [ ] deterministic full-system rebuild is byte-identical;
- [ ] two independently generated release ZIPs are byte-identical.

## Artifact verification

- [ ] `BOOT.BIN` is exactly 512 bytes and ends in `55 AA`;
- [ ] `KERNEL.BIN` does not exceed 61,440 bytes;
- [ ] boot image is exactly 1,474,560 bytes;
- [ ] data image is exactly 16,777,216 bytes;
- [ ] all bundled ELF files are ELF32/i386 and contain no RWE segment;
- [ ] `ZENOVAPP.ZEX` matches the canonical cross-repository SHA-256;
- [ ] manifest records ZenovOS version, source hash, compiler revision and output hashes;
- [ ] ZIP includes both images, manifest, source revision, checksums, guide and launchers;
- [ ] public SHA-256 file verifies every published release asset.

## Documentation and presentation

- [ ] README matches the final runtime and limitations;
- [ ] ABI, security model and ZenovFS transaction protocol are published;
- [ ] framebuffer PNG is regenerated from the final `main` CI artifact;
- [ ] release notes list verified evidence and explicit non-goals;
- [ ] VirtualBox instructions are marked manual/unverified unless separately tested.

## Publication

- [ ] tag/release points to the exact final `main` commit;
- [ ] old `v0.1.1` assets are replaced atomically with the final package set;
- [ ] downloaded public assets are rehashed after upload;
- [ ] release page image and links render without embedded data URIs;
- [ ] post-publication QEMU boot uses the downloaded assets, not local build files.
