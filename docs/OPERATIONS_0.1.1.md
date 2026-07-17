# ZenovOS 0.1.1 operator commands

```text
help / F1       command reference
home / F4       home dashboard
info status     system and service state
pmm vm mem      memory diagnostics
memmap          BIOS E820 map
mount df fsck   storage state and integrity
sync            commit metadata generation
apps            list bundled applications
run <app> ...   execute foreground ZEX1 or ELF32
history         session command history
```

For filesystem writes use `write`, `append`, `touch`, `mkdir`, `cp`, `mv` and `rm`. ZenovFS1 paths are rooted under `/data`; application names without paths are resolved under `/data/apps` with `.zex` then `.elf` fallback.

The serial log is the authoritative automated evidence channel. The VGA console is intended for interactive inspection and screenshots.
