# ZenovOS 0.1.1 build provenance

The build manifest and release package connect the resulting images to their source inputs.

`BUILD-MANIFEST.json` records:

- product, version and target;
- memory, storage and application ABI descriptions;
- Zenov application ABI version;
- merged `zenov` compiler repository revision;
- canonical Zenov-generated application SHA-256;
- OS-side compiler source SHA-256;
- modular Zenov configuration source SHA-256;
- output sizes and SHA-256 hashes.

`SOURCE-REVISION.txt` records the exact ZenovOS commit used by the package generator.

`IMAGE-SHA256SUMS.txt` verifies both extracted images, manifest and source revision inside the ZIP. The external `SHA256SUMS.txt` verifies the public boot image, ZIP, manifest and source revision.

Release CI generates the package twice with fixed timestamps and stripped ZIP metadata, then performs byte-for-byte comparisons. This proves reproducibility for the same toolchain environment and source commit; it is not a claim of cross-toolchain reproducibility across arbitrary compiler/binutils versions.
