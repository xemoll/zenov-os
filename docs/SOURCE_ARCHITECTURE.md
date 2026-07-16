# ZenovOS 0.1.1 source architecture

## Product configuration

`kernel/main.zv` is now a composition root rather than the entire product definition. It includes:

- `kernel/config/system.zv` — identity, version, prompt, theme and shell capacities;
- `kernel/config/boot.zv` — serial boot diagnostics;
- `kernel/config/shell.zv` — static shell commands;
- `kernel/config/files.zv` — generated read-only system documents.

Stage0 expands exact line-form `include("relative/path.zv");` directives before parsing. Includes are resolved relative to the including file, constrained to the root containing `kernel/main.zv`, cycle-checked and limited to 16 levels. Absolute paths and traversal outside the root are rejected.

## Scale contracts

The stage0 self-test compiles 200 static command declarations and verifies deterministic generation. Release 0.1.1 accepts up to 256 boot messages, 256 static commands and 256 generated VFS documents. Command responses are limited to 4 KiB, individual generated documents to 16 KiB and aggregate generated command/document text to 48 KiB. These are explicit kernel-image safety budgets, not accidental tiny-file limits.

The interactive shell uses capacities emitted by Zenov configuration. The release profile sets a 512-byte line buffer (511 usable characters plus the terminator) and 128 history entries. Editing uses a horizontal viewport, so the command is not capped by the remaining columns of the 80-column VGA row.

## Verification

`make check` verifies generated capacities and runs the 200-declaration/include self-test. `tests/qemu_smoke.sh` enters an `echo` command longer than the legacy 80-byte buffer and requires a suffix marker located beyond that old boundary. The same run then verifies paging, ZenovFS, both application formats, file syscalls and persistence across a second QEMU boot.

The build manifest hashes the composition root and every `kernel/config/*.zv` module and records:

```json
{
  "configuration": "modular Zenov includes",
  "shell_line_capacity": 512,
  "shell_history_capacity": 128
}
```

## Remaining storage constraints

This change fixes source/configuration structure and interactive execution input. It does not redesign the on-disk `ZenovFS1` format: that format still has 128 metadata entries and fixed 64 KiB file slots in version 0.1.1. A variable-extent or journaled ZenovFS revision requires an explicit disk-format migration plan rather than silently changing existing 0.1.1 data images.
