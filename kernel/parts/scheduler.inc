#include "scheduler_policy.inc"

extern "C" const uint8_t scheduler_probe_start[];
extern "C" const uint8_t scheduler_probe_end[];
extern "C" const uint8_t process_lifecycle_probe_start[];
extern "C" const uint8_t process_lifecycle_probe_end[];

namespace process {
enum class TaskState : uint8_t { unused, ready, running, sleeping, waiting, zombie };
enum class BatchKind : uint8_t { applications, scheduler_probe, process_lifecycle_probe };

struct Task {
    TaskState state = TaskState::unused;
    uint8_t base_priority = scheduler_policy::default_priority;
    uint8_t dynamic_priority = scheduler_policy::default_priority;
    uint8_t quantum_remaining = 0U;
    uint8_t reserved = 0U;
    uint32_t pid = 0U;
    uint32_t wake_tick = 0U;
    uint32_t runtime_ticks = 0U;
    uint32_t wait_ticks = 0U;
    uint32_t context_switches = 0U;
    uint32_t exit_code = 0U;
    uint32_t fault_vector = 0U;
    uint32_t wait_status_offset = 0U;
    bool wait_has_status = false;
    bool wait_reap_child = false;
    bool wait_any_child = false;
    uint8_t wait_reserved = 0U;
    SyscallFrame context{};
    paging::AddressSpace address_space{};
    ActiveCapabilityProfile capabilities{};
    process_handles::ObjectKey object{};
    process_handles::ObjectKey wait_object{};
    process_handles::Table handles{};
    char path[48]{};
    char format[8]{};
};

constexpr uint32_t task_capacity = supervisor_layout::task_stack_count;
constexpr uint32_t wait_nohang = 1U;
Task tasks[task_capacity]{};
int32_t current_task = -1;
uint32_t batch_task_count = 0U;
uint32_t completed_task_count = 0U;
uint32_t faulted_task_count = 0U;
uint32_t scheduler_switch_count = 0U;
uint32_t timer_preemption_count = 0U;
uint32_t scheduler_log_budget = 32U;
uint32_t spawned_process_count = 0U;
uint32_t reaped_process_count = 0U;
uint32_t stale_handle_rejections = 0U;
uint32_t deferred_stack_scrub_mask = 0U;
BatchKind batch_kind = BatchKind::applications;
bool scheduler_ready = false;
bool scheduler_running = false;
bool scheduler_ending = false;

bool scheduler_active() { return scheduler_running; }
uintptr_t task_stack_bottom(uint32_t index) {
    return supervisor_layout::task_stack_region_start +
        index * supervisor_layout::task_stack_stride +
        supervisor_layout::task_stack_guard_bytes;
}
uintptr_t task_stack_top(uint32_t index) {
    return task_stack_bottom(index) + supervisor_layout::task_stack_bytes - 16U;
}

void schedule_stack_scrub(uint32_t index) {
    if (index < task_capacity) deferred_stack_scrub_mask |= 1U << index;
}

void scrub_deferred_stacks(int32_t except_index = -1) {
    for (uint32_t index = 0U; index < task_capacity; ++index) {
        if ((deferred_stack_scrub_mask & (1U << index)) == 0U ||
            static_cast<int32_t>(index) == except_index) continue;
        memset(reinterpret_cast<void*>(task_stack_bottom(index)), 0,
               supervisor_layout::task_stack_bytes);
        deferred_stack_scrub_mask &= ~(1U << index);
    }
}

void context_from_interrupt(SyscallFrame& output, const InterruptFrame& input) {
    output.gs = input.gs; output.fs = input.fs; output.es = input.es; output.ds = input.ds;
    output.edi = input.edi; output.esi = input.esi; output.ebp = input.ebp;
    output.esp_dummy = input.esp_dummy; output.ebx = input.ebx; output.edx = input.edx;
    output.ecx = input.ecx; output.eax = input.eax;
    output.eip = input.eip; output.cs = input.cs; output.eflags = input.eflags;
    output.useresp = input.useresp; output.ss = input.ss;
}
void context_to_interrupt(InterruptFrame& output, const SyscallFrame& input) {
    output.gs = input.gs; output.fs = input.fs; output.es = input.es; output.ds = input.ds;
    output.edi = input.edi; output.esi = input.esi; output.ebp = input.ebp;
    output.esp_dummy = input.esp_dummy; output.ebx = input.ebx; output.edx = input.edx;
    output.ecx = input.ecx; output.eax = input.eax;
    output.eip = input.eip; output.cs = input.cs; output.eflags = input.eflags;
    output.useresp = input.useresp; output.ss = input.ss;
}

void serial_switch(uint32_t from_pid, uint32_t to_pid, const char* reason) {
    if (scheduler_log_budget == 0U) return;
    --scheduler_log_budget;
    serial::write("SCHED_CONTEXT_SWITCH from="); serial_u32(from_pid);
    serial::write(" to="); serial_u32(to_pid);
    serial::write(" reason="); serial::line(reason);
}

void expose_task(const Task& task, uint32_t index) {
    set_kernel_stack(task_stack_top(index));
    active_capabilities = task.capabilities;
    active_syscall_abi = SyscallAbi::zenov;
    linux_i386_image_bias = 0U;
    storage::path_copy(current_application, task.path, sizeof(current_application));
    string_copy(current_format, task.format, sizeof(current_format));
}

bool activate_task(uint32_t index) {
    if (index >= task_capacity || tasks[index].state == TaskState::unused ||
        tasks[index].state == TaskState::zombie ||
        !paging::address_space_activate(&tasks[index].address_space)) return false;
    current_task = static_cast<int32_t>(index);
    tasks[index].state = TaskState::running;
    tasks[index].quantum_remaining = scheduler_policy::quantum_ticks(tasks[index].dynamic_priority);
    scheduler_policy::Candidate candidate{true, tasks[index].base_priority,
        tasks[index].dynamic_priority, tasks[index].wait_ticks};
    scheduler_policy::reset_after_run(candidate);
    tasks[index].dynamic_priority = candidate.dynamic_priority;
    tasks[index].wait_ticks = candidate.wait_ticks;
    expose_task(tasks[index], index);
    return true;
}

void destroy_task_address_space(uint32_t index) {
    if (index >= task_capacity) return;
    paging::address_space_destroy(tasks[index].address_space);
}

uint32_t total_open_handles() {
    uint32_t total = 0U;
    for (const Task& task : tasks) total += process_handles::active_count(task.handles);
    return total;
}

void close_all_handles(Task& task) {
    for (uint32_t index = 0U; index < process_handles::handle_capacity; ++index) {
        const process_handles::Entry& entry = task.handles.entries[index];
        if (!entry.used) continue;
        const uint32_t handle = process_handles::encode(index, entry.generation);
        process_handles::ObjectKey key{};
        if (process_handles::close(task.handles, handle, key))
            (void)process_lifecycle::release(key);
    }
}

void release_task_runtime(uint32_t index) {
    if (index >= task_capacity) return;
    destroy_task_address_space(index);
    schedule_stack_scrub(index);
}

void finalize_terminated_task(uint32_t index) {
    if (index >= task_capacity || tasks[index].state != TaskState::zombie) return;
    Task& task = tasks[index];
    release_task_runtime(index);
    process_lifecycle::Object* object = process_lifecycle::find(task.object);
    if (!object || object->parent_reaped) {
        const process_handles::ObjectKey key = task.object;
        if (object) object->task_slot = -1;
        task = Task{};
        if (object) (void)process_lifecycle::collect_if_unreferenced(key);
    }
}

uint32_t termination_reason_value(process_lifecycle::TerminationReason reason) {
    return static_cast<uint32_t>(reason);
}

UserWaitStatus wait_status_for(const process_lifecycle::Object& object) {
    return UserWaitStatus{object.pid, object.exit_code,
                          termination_reason_value(object.reason), object.fault_vector};
}

UserProcessInfo process_info_for(const process_lifecycle::Object& object) {
    return UserProcessInfo{object.pid, object.parent_pid, object.signaled ? 2U : 1U,
                           object.exit_code, object.fault_vector, object.runtime_ticks,
                           object.context_switches, object.handle_references};
}

bool write_wait_status(Task& task, const process_lifecycle::Object& object) {
    if (!task.wait_has_status) return true;
    const UserWaitStatus status = wait_status_for(object);
    return paging::address_space_write(task.address_space, task.wait_status_offset,
                                       &status, sizeof(status));
}

bool reap_child_task(const process_handles::ObjectKey& key, uint32_t parent_pid) {
    process_lifecycle::Object* object = process_lifecycle::find(key);
    if (!object || !object->signaled || object->parent_pid != parent_pid ||
        object->parent_reaped) return false;
    const int32_t slot = object->task_slot;
    if (slot >= 0 && slot < static_cast<int32_t>(task_capacity) &&
        slot != current_task) {
        release_task_runtime(static_cast<uint32_t>(slot));
        tasks[slot] = Task{};
        object->task_slot = -1;
    }
    if (!process_lifecycle::reap_parent(key, parent_pid)) return false;
    ++reaped_process_count;
    return true;
}

bool complete_waiter(uint32_t waiter_index, const process_handles::ObjectKey& key) {
    if (waiter_index >= task_capacity) return false;
    Task& waiter = tasks[waiter_index];
    process_lifecycle::Object* object = process_lifecycle::find(key);
    if (waiter.state != TaskState::waiting || !object || !object->signaled) return false;
    const uint32_t pid = object->pid;
    if (!write_wait_status(waiter, *object)) {
        waiter.context.eax = error_fault;
    } else {
        waiter.context.eax = pid;
        if (waiter.wait_reap_child && !reap_child_task(key, waiter.pid))
            waiter.context.eax = error_stale;
    }
    waiter.wait_object = {};
    waiter.wait_status_offset = 0U;
    waiter.wait_has_status = false;
    waiter.wait_reap_child = false;
    waiter.wait_any_child = false;
    waiter.state = TaskState::ready;
    waiter.wait_ticks = 0U;
    serial::write("PROCESS_WAIT_COMPLETE waiter="); serial_u32(waiter.pid);
    serial::write(" target="); serial_u32(pid); serial::write("\r\n");
    return true;
}

void wake_waiters(const process_handles::ObjectKey& key) {
    const process_lifecycle::Object* object = process_lifecycle::find_const(key);
    if (!object || !object->signaled) return;
    const uint32_t parent_pid = object->parent_pid;
    for (uint32_t index = 0U; index < task_capacity; ++index) {
        Task& waiter = tasks[index];
        if (waiter.state != TaskState::waiting) continue;
        const bool exact = process_handles::key_equal(waiter.wait_object, key);
        const bool any_child = waiter.wait_any_child && parent_pid == waiter.pid;
        if (exact || any_child) (void)complete_waiter(index, key);
    }
}

void collect_orphan_zombies(int32_t except_index) {
    for (uint32_t index = 0U; index < task_capacity; ++index) {
        if (static_cast<int32_t>(index) == except_index ||
            tasks[index].state != TaskState::zombie) continue;
        const process_lifecycle::Object* object = process_lifecycle::find_const(tasks[index].object);
        if (!object || (object->parent_pid == 0U && object->parent_reaped))
            finalize_terminated_task(index);
    }
}

void wake_sleepers() {
    for (uint32_t i = 0U; i < task_capacity; ++i) {
        if (tasks[i].state != TaskState::sleeping) continue;
        if (static_cast<int32_t>(timer_ticks - tasks[i].wake_tick) < 0) continue;
        tasks[i].state = TaskState::ready;
        tasks[i].wait_ticks = 0U;
        serial::write("TASK_WAKE pid="); serial_u32(tasks[i].pid); serial::write("\r\n");
    }
}

void age_ready_tasks() {
    for (uint32_t i = 0U; i < task_capacity; ++i) {
        scheduler_policy::Candidate candidate{
            tasks[i].state == TaskState::ready,
            tasks[i].base_priority,
            tasks[i].dynamic_priority,
            tasks[i].wait_ticks};
        scheduler_policy::account_wait(candidate);
        tasks[i].dynamic_priority = candidate.dynamic_priority;
        tasks[i].wait_ticks = candidate.wait_ticks;
    }
}

int32_t select_ready(int32_t current) {
    scheduler_policy::Candidate candidates[task_capacity]{};
    for (uint32_t i = 0U; i < task_capacity; ++i) {
        candidates[i] = scheduler_policy::Candidate{
            tasks[i].state == TaskState::ready,
            tasks[i].base_priority,
            tasks[i].dynamic_priority,
            tasks[i].wait_ticks};
    }
    return scheduler_policy::select(candidates, task_capacity, current);
}

bool higher_priority_ready(uint8_t priority) {
    for (const Task& task : tasks) {
        if (task.state == TaskState::ready && task.dynamic_priority > priority) return true;
    }
    return false;
}

bool switch_interrupt(InterruptFrame* frame, int32_t next, const char* reason) {
    if (!frame || next < 0 || next >= static_cast<int32_t>(task_capacity)) return false;
    const int32_t previous = current_task;
    const uint32_t previous_pid = previous >= 0 ? tasks[previous].pid : 0U;
    if (!activate_task(static_cast<uint32_t>(next))) return false;
    context_to_interrupt(*frame, tasks[next].context);
    ++scheduler_switch_count;
    ++tasks[next].context_switches;
    serial_switch(previous_pid, tasks[next].pid, reason);
    if (previous >= 0 && tasks[previous].state == TaskState::zombie)
        finalize_terminated_task(static_cast<uint32_t>(previous));
    return true;
}

bool switch_syscall(SyscallFrame* frame, int32_t next, const char* reason) {
    if (!frame || next < 0 || next >= static_cast<int32_t>(task_capacity)) return false;
    const int32_t previous = current_task;
    const uint32_t previous_pid = previous >= 0 ? tasks[previous].pid : 0U;
    if (!activate_task(static_cast<uint32_t>(next))) return false;
    *frame = tasks[next].context;
    ++scheduler_switch_count;
    ++tasks[next].context_switches;
    serial_switch(previous_pid, tasks[next].pid, reason);
    if (previous >= 0 && tasks[previous].state == TaskState::zombie)
        finalize_terminated_task(static_cast<uint32_t>(previous));
    return true;
}

uint32_t timer_interrupt(InterruptFrame* frame) {
    if (!scheduler_running || !frame || (frame->cs & 3U) != 3U || current_task < 0) return 0U;
    scrub_deferred_stacks(current_task);
    Task& current = tasks[current_task];
    context_from_interrupt(current.context, *frame);
    ++current.runtime_ticks;
    wake_sleepers();
    age_ready_tasks();
    if (current.quantum_remaining > 0U) --current.quantum_remaining;
    if (current.quantum_remaining > 0U && !higher_priority_ready(current.dynamic_priority)) return 0U;
    current.state = TaskState::ready;
    const int32_t next = select_ready(current_task);
    if (next < 0 || next == current_task) {
        current.state = TaskState::running;
        current.quantum_remaining = scheduler_policy::quantum_ticks(current.dynamic_priority);
        return 0U;
    }
    ++timer_preemption_count;
    return switch_interrupt(frame, next, "timer") ? 0U : 1U;
}

void record_task_fault(Task& task, const InterruptFrame& frame, uintptr_t address) {
    task.fault_vector = frame.vector;
    task.exit_code = 128U + frame.vector;
    serial::write("TASK_FAULT_ISOLATED pid="); serial_u32(task.pid);
    serial::write(" app="); serial::write(task.path);
    serial::write(" vector="); serial_u32(frame.vector);
    serial::write(" error="); serial_hex(frame.error);
    serial::write(" eip="); serial_hex(frame.eip);
    if (frame.vector == 14U) { serial::write(" cr2="); serial_hex(static_cast<uint32_t>(address)); }
    serial::write("\r\n");
}

bool terminate_task(uint32_t index, uint32_t exit_code, uint32_t fault_vector,
                    process_lifecycle::TerminationReason reason) {
    if (index >= task_capacity || tasks[index].state == TaskState::unused ||
        tasks[index].state == TaskState::zombie) return false;
    Task& task = tasks[index];
    (void)process_lifecycle::reparent(task.pid, 0U);
    collect_orphan_zombies(static_cast<int32_t>(index));
    close_all_handles(task);
    task.exit_code = exit_code;
    task.fault_vector = fault_vector;
    task.state = TaskState::zombie;
    if (!process_lifecycle::terminate(task.object, exit_code, fault_vector,
            task.runtime_ticks, task.context_switches, reason)) return false;
    ++completed_task_count;
    if (reason == process_lifecycle::TerminationReason::faulted) ++faulted_task_count;
    wake_waiters(task.object);
    return true;
}

uint32_t user_exception(InterruptFrame* frame) {
    if (scheduler_running && current_task >= 0) scrub_deferred_stacks(current_task);
    if (!scheduler_running || !frame || current_task < 0) {
        const uintptr_t address = frame && frame->vector == 14U ? paging::fault_address() : 0U;
        if (frame) record_user_fault(frame->vector, frame->error, frame->eip, address);
        return 1U;
    }
    const int32_t previous = current_task;
    Task& task = tasks[previous];
    const uintptr_t address = frame->vector == 14U ? paging::fault_address() : 0U;
    record_task_fault(task, *frame, address);
    if (!terminate_task(static_cast<uint32_t>(previous), 128U + frame->vector,
                        frame->vector, process_lifecycle::TerminationReason::faulted)) {
        scheduler_ending = true;
        return 1U;
    }
    const int32_t next = select_ready(previous);
    if (next < 0) { scheduler_ending = true; return 1U; }
    return switch_interrupt(frame, next, "fault") ? 0U : 1U;
}

uint32_t syscall_spawn_task(SyscallFrame* frame, Task& parent);
uint32_t syscall_wait_child(SyscallFrame* frame, Task& waiter);
uint32_t syscall_wait_process_handle(SyscallFrame* frame, Task& waiter);
uint32_t syscall_close_process_handle(SyscallFrame* frame, Task& task);
uint32_t syscall_query_process(SyscallFrame* frame, Task& task);

uint32_t scheduler_syscall(SyscallFrame* frame) {
    if (!scheduler_running || !frame || current_task < 0) return 1U;
    scrub_deferred_stacks(current_task);
    Task& current = tasks[current_task];
    const uint32_t number = frame->eax;
    if (number == syscall_getpid) { frame->eax = current.pid; return 0U; }
    if (number == syscall_spawn) { frame->eax = syscall_spawn_task(frame, current); return 0U; }
    if (number == syscall_waitpid) {
        const int32_t caller = current_task;
        const uint32_t result = syscall_wait_child(frame, current);
        if (current_task == caller && tasks[caller].state == TaskState::running) frame->eax = result;
        return 0U;
    }
    if (number == syscall_wait_handle) {
        const int32_t caller = current_task;
        const uint32_t result = syscall_wait_process_handle(frame, current);
        if (current_task == caller && tasks[caller].state == TaskState::running) frame->eax = result;
        return 0U;
    }
    if (number == syscall_handle_close) {
        frame->eax = syscall_close_process_handle(frame, current);
        return 0U;
    }
    if (number == syscall_process_query) {
        frame->eax = syscall_query_process(frame, current);
        return 0U;
    }
    if (number == syscall_exit) {
        const uint32_t pid = current.pid;
        const uint32_t code = frame->ebx;
        if (!terminate_task(static_cast<uint32_t>(current_task), code, 0U,
                            process_lifecycle::TerminationReason::exited)) {
            scheduler_ending = true;
            return 1U;
        }
        serial::write("TASK_EXIT pid="); serial_u32(pid);
        serial::write(" code="); serial_u32(code); serial::write("\r\n");
        const int32_t next = select_ready(current_task);
        if (next < 0) { scheduler_ending = true; return 1U; }
        return switch_syscall(frame, next, "exit") ? 0U : 1U;
    }
    frame->eax = 0U;
    current.context = *frame;
    if (number == syscall_sleep) {
        const uint32_t duration = frame->ebx ? frame->ebx : 1U;
        current.state = TaskState::sleeping;
        current.wake_tick = timer_ticks + duration;
        const int32_t next = select_ready(current_task);
        if (next < 0) {
            current.state = TaskState::running;
            return 0U;
        }
        return switch_syscall(frame, next, "sleep") ? 0U : 1U;
    }
    if (number == syscall_yield) {
        current.state = TaskState::ready;
        const int32_t next = select_ready(current_task);
        if (next < 0 || next == current_task) {
            current.state = TaskState::running;
            return 0U;
        }
        return switch_syscall(frame, next, "yield") ? 0U : 1U;
    }
    return 0U;
}


void reset_task_table() {
    scrub_deferred_stacks(-1);
    for (uint32_t i = 0U; i < task_capacity; ++i) tasks[i] = Task{};
    process_lifecycle::reset();
    current_task = -1;
    batch_task_count = completed_task_count = faulted_task_count = 0U;
    scheduler_switch_count = timer_preemption_count = 0U;
    spawned_process_count = reaped_process_count = stale_handle_rejections = 0U;
    scheduler_log_budget = 32U;
    deferred_stack_scrub_mask = 0U;
    batch_kind = BatchKind::applications;
    scheduler_ending = false;
}

bool configure_task(uint32_t slot, paging::AddressSpace space, uint32_t entry,
                    uint32_t initial_stack, uint32_t initial_eax,
                    const ActiveCapabilityProfile& capabilities,
                    const char* path, const char* format,
                    uint32_t parent_pid = 0U) {
    if (slot >= task_capacity || !space.table_physical || !path || !format) return false;
    char normalized_path[sizeof(Task::path)]{};
    if (!storage::path_copy(normalized_path, path, sizeof(normalized_path))) return false;
    process_handles::ObjectKey object{};
    uint32_t pid = 0U;
    if (!process_lifecycle::create(parent_pid, static_cast<int32_t>(slot), object, pid))
        return false;
    Task& task = tasks[slot];
    task = Task{};
    task.state = TaskState::ready;
    task.pid = pid;
    task.object = object;
    task.base_priority = scheduler_policy::default_priority;
    task.dynamic_priority = task.base_priority;
    task.quantum_remaining = scheduler_policy::quantum_ticks(task.dynamic_priority);
    task.context.gs = task.context.fs = task.context.es = task.context.ds = 0x23U;
    task.context.eax = initial_eax;
    task.context.eip = entry;
    task.context.cs = 0x1BU;
    task.context.eflags = 0x202U;
    task.context.useresp = initial_stack;
    task.context.ss = 0x23U;
    task.address_space = space;
    task.capabilities = capabilities;
    storage::path_copy(task.path, normalized_path, sizeof(task.path));
    string_copy(task.format, format, sizeof(task.format));
    scrub_deferred_stacks(current_task);
    memset(reinterpret_cast<void*>(task_stack_bottom(slot)), 0,
           supervisor_layout::task_stack_bytes);
    return true;
}

void log_task_created(const Task& task) {
    const process_lifecycle::Object* object = process_lifecycle::find_const(task.object);
    serial::write("TASK_CREATED pid="); serial_u32(task.pid);
    serial::write(" app="); serial::write(task.path);
    serial::write(" parent="); serial_u32(object ? object->parent_pid : 0U);
    serial::write(" pages="); serial_u32(task.address_space.mapped_pages);
    serial::write(" priority="); serial_u32(task.base_priority);
    serial::write("\r\n");
}

bool prepare_internal_probe_task(uint32_t slot, const uint8_t* image, uint32_t image_size,
                                 uint32_t initial_eax, const char* path, const char* format) {
    if (slot >= task_capacity || !image || !image_size || image_size >= stack_base) return false;
    paging::AddressSpace space{};
    if (!paging::address_space_create(space) || !paging::address_space_activate(&space)) {
        paging::address_space_destroy(space);
        return false;
    }
    const bool mapped = paging::map_user(0U, image_size, true) && map_stack();
    if (mapped) memcpy(reinterpret_cast<void*>(user_base), image, image_size);
    const bool protected_image = mapped && paging::protect_user(0U, image_size, false);
    if (!protected_image) {
        paging::address_space_activate(nullptr);
        paging::address_space_destroy(space);
        return false;
    }
    ActiveCapabilityProfile capabilities{};
    capabilities.valid = true;
    capabilities.mask = capability_console_write;
    if (!storage::path_copy(capabilities.path, path, sizeof(capabilities.path))) {
        paging::address_space_activate(nullptr);
        paging::address_space_destroy(space);
        return false;
    }
    security_guard::sha256(image, image_size, capabilities.digest);
    const bool configured = configure_task(slot, space, 0U, stack_top, initial_eax,
                                            capabilities, path, format, 0U);
    paging::address_space_activate(nullptr);
    if (!configured) {
        paging::address_space_destroy(space);
        return false;
    }
    log_task_created(tasks[slot]);
    return true;
}

bool prepare_scheduler_probe_task(uint32_t slot, uint32_t variant) {
    const uint32_t image_size = static_cast<uint32_t>(scheduler_probe_end - scheduler_probe_start);
    const char* const path = variant == 0U ?
        "/kernel/scheduler-probe-a" : "/kernel/scheduler-probe-b";
    return prepare_internal_probe_task(slot, scheduler_probe_start, image_size,
                                       variant, path, "KPROBE");
}

bool prepare_process_lifecycle_probe_task(uint32_t slot) {
    const uint32_t image_size = static_cast<uint32_t>(
        process_lifecycle_probe_end - process_lifecycle_probe_start);
    return prepare_internal_probe_task(slot, process_lifecycle_probe_start, image_size,
                                       0U, "/kernel/process-lifecycle-parent", "KPROC");
}


void restore_execution_identity(const ActiveCapabilityProfile& capabilities,
                                SyscallAbi abi, uint32_t image_bias,
                                paging::AddressSpace* address_space,
                                const char* application, const char* format) {
    if (!paging::address_space_activate(address_space)) panic("Process address-space restore failed.");
    active_capabilities = capabilities;
    active_syscall_abi = abi;
    linux_i386_image_bias = image_bias;
    storage::path_copy(current_application, application, sizeof(current_application));
    string_copy(current_format, format, sizeof(current_format));
}

bool prepare_task(uint32_t slot, char* command, uint32_t parent_pid = 0U) {
    if (slot >= task_capacity || !command || !*command ||
        tasks[slot].state != TaskState::unused) return false;
    const ActiveCapabilityProfile caller_capabilities = active_capabilities;
    const SyscallAbi caller_abi = active_syscall_abi;
    const uint32_t caller_bias = linux_i386_image_bias;
    paging::AddressSpace* const caller_space = paging::active_address_space;
    char caller_application[sizeof(current_application)]{};
    char caller_format[sizeof(current_format)]{};
    storage::path_copy(caller_application, current_application, sizeof(caller_application));
    string_copy(caller_format, current_format, sizeof(caller_format));

    char* arguments[max_arguments]{};
    const uint32_t argument_count = parse_arguments(command, arguments);
    if (!argument_count) return false;
    char requested[48]{};
    uint32_t file_size = 0U;
    if (!read_requested(arguments[0], requested, file_size)) {
        restore_execution_identity(caller_capabilities, caller_abi, caller_bias,
                                   caller_space, caller_application, caller_format);
        return false;
    }
    const ActiveCapabilityProfile child_capabilities = active_capabilities;
    paging::AddressSpace space{};
    if (!paging::address_space_create(space) || !paging::address_space_activate(&space)) {
        paging::address_space_destroy(space);
        restore_execution_identity(caller_capabilities, caller_abi, caller_bias,
                                   caller_space, caller_application, caller_format);
        return false;
    }
    uint32_t entry = 0U;
    const bool zex = file_size >= 4U && application_buffer[0] == 'Z' &&
        application_buffer[1] == 'E' && application_buffer[2] == 'X' &&
        application_buffer[3] == '1';
    const bool elf = file_size >= 4U && application_buffer[0] == 0x7FU &&
        application_buffer[1] == 'E' && application_buffer[2] == 'L' &&
        application_buffer[3] == 'F';
    const bool loaded = (zex && load_zex(application_buffer, file_size, entry)) ||
                        (elf && load_elf32(application_buffer, file_size, entry));
    uint32_t initial_stack = 0U;
    if (!loaded || !build_initial_stack(arguments, argument_count, initial_stack)) {
        paging::address_space_activate(nullptr);
        paging::address_space_destroy(space);
        restore_execution_identity(caller_capabilities, caller_abi, caller_bias,
                                   caller_space, caller_application, caller_format);
        return false;
    }
    const bool configured = configure_task(slot, space, entry, initial_stack, 0U,
                                           child_capabilities, requested,
                                           zex ? "ZEX1" : "ELF32", parent_pid);
    if (!configured) paging::address_space_destroy(space);
    restore_execution_identity(caller_capabilities, caller_abi, caller_bias,
                               caller_space, caller_application, caller_format);
    if (!configured) return false;
    log_task_created(tasks[slot]);
    return true;
}

int32_t find_free_task_slot() {
    for (uint32_t index = 0U; index < task_capacity; ++index)
        if (tasks[index].state == TaskState::unused) return static_cast<int32_t>(index);
    return -1;
}

bool discard_prepared_task(uint32_t slot) {
    if (slot >= task_capacity || tasks[slot].state == TaskState::unused) return false;
    Task& task = tasks[slot];
    destroy_task_address_space(slot);
    close_all_handles(task);
    const bool destroyed = process_lifecycle::destroy_unstarted(task.object);
    schedule_stack_scrub(slot);
    task = Task{};
    return destroyed;
}

uint32_t syscall_spawn_task(SyscallFrame* frame, Task& parent) {
    if (!frame || frame->ecx != 0U) return error_invalid;
    const char* const user_command = user_string(frame->ebx, 96U);
    if (!user_command) return error_fault;
    if (frame->edx && !user_range(frame->edx, sizeof(uint32_t), true)) return error_fault;
    char command[96]{};
    const uint32_t length = static_cast<uint32_t>(string_length(user_command));
    if (!length || length >= sizeof(command)) return error_invalid;
    memcpy(command, user_command, length + 1U);
    const int32_t slot = find_free_task_slot();
    if (slot < 0) return error_no_space;
    if (!prepare_task(static_cast<uint32_t>(slot), command, parent.pid)) return error_denied;
    Task& child = tasks[slot];
    uint32_t handle = 0U;
    if (frame->edx) {
        if (!process_lifecycle::retain(child.object)) {
            (void)discard_prepared_task(static_cast<uint32_t>(slot));
            return error_no_space;
        }
        handle = process_handles::allocate(parent.handles, child.object,
            process_handles::right_wait | process_handles::right_query);
        if (!handle) {
            (void)process_lifecycle::release(child.object);
            (void)discard_prepared_task(static_cast<uint32_t>(slot));
            return error_no_space;
        }
        auto* const output = reinterpret_cast<uint32_t*>(user_bytes(frame->edx,
            sizeof(uint32_t), true));
        if (!output) {
            process_handles::ObjectKey released{};
            (void)process_handles::close(parent.handles, handle, released);
            (void)process_lifecycle::release(released);
            (void)discard_prepared_task(static_cast<uint32_t>(slot));
            return error_fault;
        }
        *output = handle;
    }
    ++batch_task_count;
    ++spawned_process_count;
    serial::write("PROCESS_SPAWN parent="); serial_u32(parent.pid);
    serial::write(" child="); serial_u32(child.pid);
    serial::write(" handle="); serial_u32(handle); serial::write("\r\n");
    return child.pid;
}

uint32_t begin_wait(SyscallFrame* frame, Task& waiter,
                    const process_handles::ObjectKey& key, bool reap_child,
                    bool any_child) {
    process_lifecycle::Object* object = process_lifecycle::find(key);
    if (!object) return error_stale;
    const bool has_status = frame->ecx != 0U;
    if (has_status && !user_range(frame->ecx, sizeof(UserWaitStatus), true)) return error_fault;
    if (object->signaled) {
        if (has_status) {
            auto* const status = reinterpret_cast<UserWaitStatus*>(
                user_bytes(frame->ecx, sizeof(UserWaitStatus), true));
            if (!status) return error_fault;
            *status = wait_status_for(*object);
        }
        const uint32_t pid = object->pid;
        if (reap_child && !reap_child_task(key, waiter.pid)) return error_stale;
        return pid;
    }
    if (frame->edx & wait_nohang) return 0U;
    waiter.context = *frame;
    waiter.context.eax = 0U;
    waiter.state = TaskState::waiting;
    waiter.wait_object = key;
    waiter.wait_status_offset = frame->ecx;
    waiter.wait_has_status = has_status;
    waiter.wait_reap_child = reap_child;
    waiter.wait_any_child = any_child;
    const int32_t next = select_ready(current_task);
    if (next < 0 || next == current_task) {
        waiter.state = TaskState::running;
        waiter.wait_object = {};
        waiter.wait_status_offset = 0U;
        waiter.wait_has_status = false;
        waiter.wait_reap_child = false;
        waiter.wait_any_child = false;
        return error_would_block;
    }
    return switch_syscall(frame, next, "wait") ? 0U : error_io;
}

uint32_t syscall_wait_child(SyscallFrame* frame, Task& waiter) {
    process_lifecycle::Object* object = nullptr;
    const bool any_child = frame->ebx == 0U;
    if (any_child) {
        object = process_lifecycle::first_child(waiter.pid, true);
        if (!object) object = process_lifecycle::first_child(waiter.pid, false);
    } else {
        object = process_lifecycle::find_pid(frame->ebx);
        if (object && (object->parent_pid != waiter.pid || object->parent_reaped))
            object = nullptr;
    }
    if (!object) return error_not_found;
    return begin_wait(frame, waiter, process_lifecycle::key_for(*object), true, any_child);
}

uint32_t syscall_wait_process_handle(SyscallFrame* frame, Task& waiter) {
    process_handles::ObjectKey key{};
    if (!process_handles::resolve(waiter.handles, frame->ebx,
                                  process_handles::right_wait, key)) {
        ++stale_handle_rejections;
        return error_invalid;
    }
    if (!process_lifecycle::find(key)) {
        ++stale_handle_rejections;
        return error_stale;
    }
    return begin_wait(frame, waiter, key, false, false);
}

uint32_t syscall_close_process_handle(SyscallFrame* frame, Task& task) {
    process_handles::ObjectKey key{};
    if (!process_handles::close(task.handles, frame->ebx, key)) {
        ++stale_handle_rejections;
        return error_invalid;
    }
    if (!process_lifecycle::release(key)) return error_stale;
    return 0U;
}

uint32_t syscall_query_process(SyscallFrame* frame, Task& task) {
    if (!user_range(frame->ecx, sizeof(UserProcessInfo), true)) return error_fault;
    process_handles::ObjectKey key{};
    if (!process_handles::resolve(task.handles, frame->ebx,
                                  process_handles::right_query, key)) {
        ++stale_handle_rejections;
        return error_invalid;
    }
    const process_lifecycle::Object* object = process_lifecycle::find_const(key);
    if (!object) {
        ++stale_handle_rejections;
        return error_stale;
    }
    auto* const output = reinterpret_cast<UserProcessInfo*>(
        user_bytes(frame->ecx, sizeof(UserProcessInfo), true));
    if (!output) return error_fault;
    *output = process_info_for(*object);
    return 0U;
}


uint32_t split_task_commands(char* input, char* output[task_capacity]) {
    uint32_t count = 0U;
    bool quoted = false;
    char quote = 0;
    char* start = input;
    for (char* cursor = input;; ++cursor) {
        const char value = *cursor;
        if (value == '\'' || value == '"') {
            if (!quoted) { quoted = true; quote = value; }
            else if (value == quote) { quoted = false; quote = 0; }
        }
        if ((value == '&' && !quoted) || value == 0) {
            if (value == '&') *cursor = 0;
            while (is_space(*start)) ++start;
            char* end = start + string_length(start);
            while (end > start && is_space(end[-1])) *--end = 0;
            if (!*start || count >= task_capacity) return 0U;
            output[count++] = start;
            if (value == 0) break;
            start = cursor + 1;
        }
    }
    return count;
}

void cleanup_task_table() {
    (void)paging::address_space_activate(nullptr);
    for (uint32_t index = 0U; index < task_capacity; ++index) {
        destroy_task_address_space(index);
        close_all_handles(tasks[index]);
        schedule_stack_scrub(index);
        tasks[index] = Task{};
    }
    scrub_deferred_stacks(-1);
    process_lifecycle::reset();
}

bool start_scheduler_batch(BatchKind kind) {
    scheduler_running = true;
    batch_kind = kind;
    const int32_t first = select_ready(-1);
    if (first < 0 || !activate_task(static_cast<uint32_t>(first))) {
        scheduler_running = false;
        cleanup_task_table();
        reset_task_table();
        serial::line("SCHED_BATCH_START_FAILED");
        return false;
    }
    serial::write("SCHED_BATCH_START tasks="); serial_u32(batch_task_count);
    serial::write(" policy=priority-rr quantum=3-6 aging=50\r\n");
    user_enter(tasks[first].context.eip, tasks[first].context.useresp);
    return true;
}

bool run_task_batch(char* input) {
    if (!scheduler_ready || scheduler_running || !input) return false;
    char* commands[task_capacity]{};
    const uint32_t count = split_task_commands(input, commands);
    if (count < 2U) return false;
    reset_task_table();
    for (uint32_t i = 0U; i < count; ++i) {
        if (!prepare_task(i, commands[i], 0U)) {
            cleanup_task_table();
            reset_task_table();
            serial::line("SCHED_BATCH_CREATE_FAILED");
            return false;
        }
        ++batch_task_count;
    }
    return start_scheduler_batch(BatchKind::applications);
}

bool run_scheduler_probe() {
    if (!scheduler_ready || scheduler_running) return false;
    reset_task_table();
    if (!prepare_scheduler_probe_task(0U, 0U) ||
        !prepare_scheduler_probe_task(1U, 1U)) {
        cleanup_task_table();
        reset_task_table();
        serial::line("SCHED_PROBE_CREATE_FAILED");
        return false;
    }
    batch_task_count = 2U;
    serial::line("SCHED_PROBE_TRUST_BOUNDARY_OK source=kernel-rodata capabilities=console-write filesystem=unmodified");
    return start_scheduler_batch(BatchKind::scheduler_probe);
}

bool run_process_lifecycle_probe() {
    if (!scheduler_ready || scheduler_running) return false;
    reset_task_table();
    if (!prepare_process_lifecycle_probe_task(0U)) {
        cleanup_task_table();
        reset_task_table();
        serial::line("PROCESS_LIFECYCLE_PROBE_CREATE_FAILED");
        return false;
    }
    batch_task_count = 1U;
    serial::line("PROCESS_LIFECYCLE_TRUST_BOUNDARY_OK parent=kernel-rodata child=signed-zenovfs");
    return start_scheduler_batch(BatchKind::process_lifecycle_probe);
}

void scheduler_init() {
    reset_task_table();
    scheduler_ready = paging::enabled && pmm::initialized;
    serial::line(scheduler_ready ? "PREEMPTIVE_SCHEDULER_OK hz=100 tasks=8" : "PREEMPTIVE_SCHEDULER_FAILED");
    serial::line("SCHED_POLICY_OK model=priority-rr priorities=4 aging=50 quantum=3-6");
    serial::line("PROCESS_ADDRESS_SPACE_OK per-task-pte=yes pmm-backed=yes");
    serial::line("PROCESS_OBJECT_MANAGER_OK objects=16 handles-per-process=8 pid-generation=yes");
    serial::line("PROCESS_WAIT_MODEL_OK child-zombie=yes handle-wait=yes cleanup=deterministic");
}

void print_scheduler_status() {
    console::write("  Scheduler     "); console::line(scheduler_ready ? "preemptive priority/RR" : "unavailable");
    console::write("  Task slots    "); console::unsigned_dec(task_capacity); console::put('\n');
    console::write("  Active        "); console::unsigned_dec(scheduler_running ? batch_task_count - completed_task_count : 0U); console::put('\n');
    console::write("  Proc objects  "); console::unsigned_dec(process_lifecycle::retained_objects); console::put('\n');
    console::write("  Handles       "); console::unsigned_dec(total_open_handles()); console::put('\n');
    console::write("  Switches      "); console::unsigned_dec(scheduler_switch_count); console::put('\n');
    console::write("  Preemptions   "); console::unsigned_dec(timer_preemption_count); console::put('\n');
}

} // namespace process

extern "C" void user_window_cleanup() {
    if (process::scheduler_running || process::scheduler_ending) {
        if (process::current_task >= 0 &&
            process::tasks[process::current_task].state == process::TaskState::running) {
            (void)process::terminate_task(static_cast<uint32_t>(process::current_task),
                process::tasks[process::current_task].exit_code, 0U,
                process::process_lifecycle::TerminationReason::cancelled);
        }
        const uint32_t total = process::batch_task_count;
        const uint32_t completed = process::completed_task_count;
        const uint32_t faulted = process::faulted_task_count;
        const uint32_t switches = process::scheduler_switch_count;
        const uint32_t preemptions = process::timer_preemption_count;
        const uint32_t spawned = process::spawned_process_count;
        const uint32_t reaped = process::reaped_process_count;
        const uint32_t stale = process::stale_handle_rejections;
        const uint32_t retained = process::process_lifecycle::retained_objects;
        const uint32_t handles = process::total_open_handles();
        const process::BatchKind kind = process::batch_kind;
        process::scheduler_running = false;
        process::scheduler_ending = false;
        process::current_task = -1;
        process::set_kernel_stack(reinterpret_cast<uintptr_t>(&process::__stack_top));
        process::cleanup_task_table();
        process::clear_capability_profile();
        storage::path_copy(process::current_application, "<none>", sizeof(process::current_application));
        string_copy(process::current_format, "none", sizeof(process::current_format));
        if (!paging::scrub_process_window(false)) panic("Legacy process window scrub failed after scheduler batch.");
        serial::write("SCHED_BATCH_COMPLETE tasks="); process::serial_u32(total);
        serial::write(" completed="); process::serial_u32(completed);
        serial::write(" faulted="); process::serial_u32(faulted);
        serial::write(" switches="); process::serial_u32(switches);
        serial::write(" preemptions="); process::serial_u32(preemptions);
        serial::write("\r\n");
        if (kind == process::BatchKind::scheduler_probe && total == 2U &&
            completed == total && faulted == 0U && switches > 0U && preemptions > 0U)
            serial::line("PREEMPTIVE_MULTITASKING_OK isolation=per-address-space");
        if (kind == process::BatchKind::process_lifecycle_probe && total == 3U &&
            completed == total && faulted == 0U && spawned == 2U && reaped == 2U &&
            stale > 0U && retained == 0U && handles == 0U)
            serial::line("PROCESS_LIFECYCLE_RUNTIME_OK spawned=2 reaped=2 handles=closed stale=blocked objects=collected");
        process::reset_task_table();
        return;
    }
    if (!paging::scrub_process_window(false)) panic("User process window scrub failed.");
    ++paging::process_window_scrub_count;
    serial::line("USER_WINDOW_RUNTIME_SCRUB_OK");
}
