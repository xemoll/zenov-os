# zenpkg

`zenpkg` is the host-side ZenovOS package builder and verifier. It has no third-party dependencies and builds with a C++17 compiler.

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Wpedantic \
  tools/zenpkg/main.cpp -o build/zenpkg
bash tests/zenpkg_test.sh
```

Core commands:

```text
pack            validate, canonicalize and create a deterministic .zpk
verify          validate header, sizes, hashes and canonical manifest
inspect         print package metadata and digests
extract         recover canonical manifest and payload
resolve         check exact target and required host capabilities
index           verify a directory and write a deterministic index
manifest-check  validate a source manifest without packing
hash            compute SHA-256 for test and tooling use
```

The tool returns exit code `3` for a valid but incompatible package resolution. Invalid arguments, malformed packages and integrity failures return `2`.

See `docs/ZENPKG_FORMAT_1.md` for the binary specification and `docs/PACKAGE_COMPATIBILITY_ARCHITECTURE.md` for the runtime plan.
