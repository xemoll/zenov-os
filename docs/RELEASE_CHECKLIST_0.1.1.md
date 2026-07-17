# ZenovOS 0.1.1 release checklist

## Source and CI

- [ ] post-hardening PR merged into `main`;
- [ ] push workflow green on the exact resulting `main` commit;
- [ ] strict host and freestanding builds pass with warnings as errors;
- [ ] host ZenovFS1 fault injection passes every write boundary;
- [ ] all application, persistence and interrupted-recovery QEMU phases pass;
- [ ] boot scrub, at least seven runtime scrubs and W^X policy markers are present;
- [ ] deterministic system and release-package rebuilds are byte-identical.

## Artifacts

- [ ] `BOOT.BIN` is 512 bytes and the kernel remains within the 61,440-byte boot contract;
- [ ] boot/data image sizes match their fixed formats;
- [ ] bundled ELF files contain no RWE load segment;
- [ ] Zenov-generated ZEX1 matches the canonical cross-repository hash;
- [ ] manifest records source/compiler revisions and output hashes;
- [ ] ZIP contains both images, manifest, source revision, checksums, guide and launchers.

## Publication

- [ ] framebuffer PNG regenerated from final-main CI;
- [ ] `v0.1.1` assets rebuilt from the exact final-main commit;
- [ ] uploaded assets re-downloaded and rehashed;
- [ ] downloaded release package boots in QEMU;
- [ ] VirtualBox remains marked manual/unverified unless separately tested.
