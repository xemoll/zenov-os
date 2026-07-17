# Finalization notes for ZenovOS 0.1.1

The implementation phase is complete when post-merge hardening is green. The remaining publication sequence is mechanical:

1. merge the hardening PR;
2. require a green push workflow on the resulting `main` commit;
3. download that workflow's evidence artifact;
4. replace the README framebuffer PNG with the final artifact capture;
5. rebuild release assets from the exact final commit;
6. upload assets and re-download them for checksum and QEMU verification;
7. mark every applicable item in `RELEASE_CHECKLIST_0.1.1.md`.

No new subsystem should be introduced during this sequence. Any functional change resets the final CI and artifact-verification cycle.
