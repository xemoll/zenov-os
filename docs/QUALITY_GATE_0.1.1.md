# ZenovOS 0.1.1 quality gate

A candidate is rejected when any of the following is true:

- host or freestanding compilation emits a warning treated as an error;
- a required host/QEMU marker is absent;
- kernel size exceeds the bootloader contract;
- an ELF application contains an RWE load segment;
- fewer runtime process-window scrubs are observed than application transitions;
- either deliberate protection fault halts the kernel or fails to return to the shell;
- any interrupted ZenovFS1 write prefix exposes partial content;
- persistence fails across an independent QEMU process;
- Zenov-generated output differs from the canonical contract;
- independent system or release-package builds differ;
- manifest, source revision or checksums do not match the packaged bytes.

The gate must be fixed by correcting implementation or test accuracy, never by removing the failing assertion without a documented contract change.
