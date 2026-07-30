# Runtime ABI v2 staging

Temporary bounded transport only. The staging gate verifies the concatenated payload SHA-256, exact 13-file allowlist, strict GCC/Clang sanitizer tests, deterministic rebuild, and negative runtime cases before committing production files. This directory is deleted by the gate and must not appear in the final PR tree.
