# Kernel process lifecycle — ZenovOS 0.1.1

This kernel-only pass turns scheduler slots into waitable process objects. It does not change applications, desktop behavior, package formats, or user-interface code.

## Process identity

A PID encodes both an object-table slot and a monotonically advanced 16-bit generation. Reusing a free slot therefore does not revive a stale PID. The kernel validates the generation on every PID and object-key lookup.

The process-object table is bounded to 16 retained objects. A process may be running or signaled. Signaled objects retain exit code, fault vector, runtime accounting, parent identity, and kernel-handle reference count.

## Parent and child ownership

Every dynamically spawned task records its parent PID. Exit or an isolated user fault signals the process object and leaves a zombie until the parent performs `waitpid`. A parent exit reparents remaining children to the kernel owner, which reaps already-signaled orphans fail-closed.

## Kernel handles

Each process has a bounded table of eight handles. A handle contains a slot generation and explicit rights. The initial process-object rights are:

- wait;
- query.

Closing a handle advances its slot generation on the next allocation. A stale, cross-generation, missing-right, or foreign-table handle is rejected. A terminated process object remains retained while any valid handle refers to it, even after parent reap.

## Syscalls

The native ZenovOS ABI adds:

| Number | Operation | Result |
|---:|---|---|
| 12 | `spawn(command, flags, handle_out)` | child PID |
| 13 | `waitpid(pid_or_zero, status, flags)` | reaped child PID |
| 14 | `wait_handle(handle, status, flags)` | signaled process PID |
| 15 | `handle_close(handle)` | zero |
| 16 | `process_query(handle, info)` | zero |

`flags=1` selects non-blocking wait. User output buffers are validated before a wait begins. When a blocked waiter is awakened, the kernel writes status through the waiter's own page table without temporarily exposing another process address space.

## Cleanup ordering

Termination follows a fixed order:

1. reparent children;
2. close owned handles;
3. signal the process object;
4. destroy user mappings;
5. defer wiping of the currently active kernel stack;
6. wake eligible waiters;
7. collect the process object only after parent reap and final handle release.

The deferred stack scrub prevents the scheduler from erasing the stack on which the terminating syscall or exception handler is still executing.

## Diagnostics

The kernel-owned `proctest` probe spawns the existing signed `/apps/hello.zex` twice. It verifies handle wait, parent wait/reap, status propagation, PID generation rollover, stale-handle rejection, complete address-space cleanup, and object collection. The probe does not add or modify any application binary.
