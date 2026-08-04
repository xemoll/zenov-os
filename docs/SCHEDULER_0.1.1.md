# ZenovOS 0.1.1 preemptive scheduler

## Policy

The scheduler is single-core and timer driven at 100 Hz. It uses four priority
levels, round-robin selection among equal priorities, 30–60 ms quanta and aging
after 50 waiting ticks. A ready task at a higher dynamic priority preempts the
running task; expiration of a quantum rotates equal-priority tasks.

This is intentionally smaller than Linux EEVDF or the Windows dispatcher, but
it adopts the same essential kernel properties: timer preemption, explicit task
states, bounded quanta, priority-sensitive selection and starvation control.

## Task states

- `ready`: eligible to run;
- `running`: current ring-3 task;
- `sleeping`: blocked until a PIT tick deadline;
- `zombie`: exited or faulted and awaiting resource teardown;
- `unused`: free task-table slot.

The task table contains eight slots. Every task stores a complete i686 user
context, PID, scheduling statistics, signed syscall capability snapshot,
per-task page table and fault/exit status.

## Context switching

IRQ0 saves the interrupted user register frame. When scheduling is required the
kernel:

1. records the current context;
2. chooses the next ready task;
3. installs that task's PMM-backed user page table;
4. reloads CR3;
5. updates TSS `ESP0` and the active signed capability profile;
6. rewrites the interrupt frame and returns with `IRETD`.

Syscall-triggered `yield`, `sleep` and `exit` use the same task context format.

## Syscalls

- `9 yield()` — voluntarily rotate to another ready task;
- `10 sleep(ticks)` — block until the requested PIT deadline when another task
  can run;
- `11 getpid()` — return the current task ID.

## Isolation and faults

A task fault terminates only that task. Its executable identity, vector, error,
EIP and CR2 are recorded, its address-space pages are zeroed and released, and
the next ready task resumes. Kernel faults remain fatal.

The QEMU scheduler gate runs two CPU-bound instances of the signed `HELLO.ZEX`
image and requires the second task to start before the first completes. The gate
also requires non-zero timer preemptions, context switches, clean exits and
separate address-space creation markers.

## Current limitations

The scheduler is uniprocessor. Console input remains a kernel-blocking operation,
there is no process tree, signals, handles, user-space services, SMP balancing,
demand paging or persistent process state. The shell launches a foreground batch
with `run APP args & APP args`; it regains control after all tasks exit or fault.

## Trust-preserving preemption diagnostic

The `schedtest` command does not modify or replace a signed ZenovFS application.
It copies a bounded position-independent workload from kernel read-only storage
into two fresh PMM-backed ring-3 address spaces. Each task receives only the
`console-write` capability. The diagnostic therefore proves timer preemption and
per-task mappings without expanding the signed application trust set.

Required runtime evidence:

- `SCHED_PROBE_TRUST_BOUNDARY_OK ... filesystem=unmodified`
- two `/kernel/scheduler-probe-*` task creation records
- task B starts before task A completes
- at least one `reason=timer` context switch
- a successful batch with non-zero preemptions
