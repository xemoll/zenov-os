# ZenovOS 0.1.1 verification markers

These serial and host-tool markers are machine-checked contracts. They are not decorative log messages.

## Boot and memory

```text
ZENOVOS_BOOT_OK
PMM_STRESS_OK
PMM_OK
PAGING_OK
USER_WINDOW_SCRUB_OK
ELF_WX_POLICY_OK
HEAP_REUSE_OK
HEAP_COALESCE_OK
HEAP_INVALID_FREE_BLOCKED
HEAP_STRESS_OK
PROCESS_ABI_0_1_1_OK
ZENOVOS_UI_READY
```

## Application transitions

```text
USER_WINDOW_RUNTIME_SCRUB_OK
APP_START_ZEX
APP_START_ELF
APP_EXIT code=0
```

The CI evidence inspection requires at least seven runtime scrub markers, covering all bundled normal-exit and recoverable-fault transitions.

## Syscall and process ABI

```text
PROCESS_ARGV_LAYOUT_STAGE_OK
SYSCALL_UNSUPPORTED_STAGE_OK
SYSCALL_UNMAPPED_POINTER_STAGE_OK
SYSCALL_READONLY_POINTER_STAGE_OK
PROCESS_ARGV_OK
SYSCALL_ERRORS_OK
SYSCALL_POINTER_GUARD_OK
CONSOLE_READ_READY
CONSOLE_READ_SYSCALL_OK
```

## Protection faults

```text
PAGE_PROTECTION_OK
USER_FAULT app=...
PAGE_FAULT_DIAGNOSTICS_OK
USER_WRITE_TO_TEXT_BLOCKED
USER_KERNEL_ACCESS_BLOCKED
USER_FAULT_RETURNED_TO_SHELL
```

## Storage durability

```text
ZENOVFS_MOUNT_OK
ZENOVFS_FSCK_OK
ZENOVFS_FAULT_INJECTION_OK
ZENOVFS_OLD_OR_NEW_CONTENT_ONLY
ZENOVFS_INTERRUPTED_WRITE_RECOVERED
ZENOVFS_RECOVERY_IMAGE_OK
```

## Zenov compiler integration

```text
ZENOV_SOURCE_APP_BUILD_OK
ZENOV_OS_APP_ARTIFACT_OK
ZENOV_OS_APP_COMPILER_CONTRACT_OK
ZENOV_SOURCE_APP_RING3_OK
ZENOV_COMPILER_ABI_MATCH_OK
```

A release claim is valid only when the matching workflow or host test checks the marker and the underlying operation. Merely printing a marker without exercising its contract is a test defect.
