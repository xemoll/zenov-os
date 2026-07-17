# Do not regress these 0.1.1 contracts

- Do not remap the full user table present/writable as a shortcut.
- Do not remove `CR0.WP` to work around kernel copies into RX pages.
- Do not bypass `user_access` for syscall buffers or strings.
- Do not return from a user exception to the faulting instruction.
- Do not skip process-window scrubbing on either `exit` or fault return.
- Do not accept W+X ELF load segments.
- Do not replace stable syscall errors with one generic result.
- Do not write replacement metadata before the complete staged payload.
- Do not change ZenovFS1 on-disk sizes without a versioned migration.
- Do not update the Zenov-generated app hash without synchronized compiler and OS changes.
- Do not weaken `-Werror`, negative QEMU tests, fault injection or deterministic comparisons to make CI green.
- Do not publish release assets built from a commit other than the tagged final `main` revision.
