# Post-merge hardening merge criteria

Merge only when the hardening head passes the full existing workflow and additionally proves:

```text
USER_WINDOW_SCRUB_OK
USER_WINDOW_RUNTIME_SCRUB_OK  (at least seven occurrences)
ELF_WX_POLICY_OK
```

The workflow must still pass host ZenovFS fault injection, all QEMU phases, deterministic system rebuild and deterministic release-package comparison.
