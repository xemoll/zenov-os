# Final hardening delta

Implementation: process-window scrub plus W^X ELF policy.

Verification: boot scrub, at least seven exit/fault scrubs, W^X policy self-test, and the complete pre-existing P0 workflow.

Publication: merge only after green CI, then rerun on `main` and rebuild release assets from that commit.
