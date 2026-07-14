# Validation contract

Every pull request must pass the `ZenovOS CI` workflow.

The workflow must:

1. run the deterministic stage0 compiler tests;
2. assemble a 512-byte boot sector with the `0xAA55` signature;
3. compile `kernel/main.zv` into a non-empty bounded kernel;
4. create a deterministic 1.44 MiB FAT12 disk image;
5. boot the image in QEMU and observe `ZENOVOS_BOOT_OK` on COM1;
6. capture a non-empty framebuffer screendump;
7. rebuild and compare `build-manifest.json` byte-for-byte;
8. upload the image, kernel, boot sector, serial log, screenshot and manifest.

The current locally verified standalone tree produced:

```text
BOOT.BIN:     512 bytes
after kernel path normalization, KERNEL.BIN: 951 bytes
zenov-os.img: 1,474,560 bytes
```

Local checks do not replace the GitHub Actions result; both are recorded separately.
