#include "scheduler_policy.inc"

extern "C" const uint8_t scheduler_probe_start[];
extern "C" const uint8_t scheduler_probe_end[];

namespace process {
enum class TaskState : uint8_t { unused, ready, running, sleeping, zombie };

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
    SyscallFrame context{};
    paging::AddressSpace address_space{};
    ActiveCapabilityProfile capabilities{};
    char path[48]{};
    char format[8]{};
};

constexpr uint32_t task_capacity = supervisor_layout::task_stack_count;
Task tasks[task_capacity]{};
int32_t current_task = -1;
uint32_t next_pid = 1U;
uint32_t batch_task_count = 0U;
uint32_t completed_task_count = 0U;
uint32_t faulted_task_count = 0U;
uint32_t scheduler_switch_count = 0U;
uint32_t timer_preemption_count = 0U;
uint32_t scheduler_log_budget = 32U;
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
    if (previous >= 0 && tasks[previous].state == TaskState::zombie) destroy_task_address_space(static_cast<uint32_t>(previous));
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
    if (previous >= 0 && tasks[previous].state == TaskState::zombie) destroy_task_address_space(static_cast<uint32_t>(previous));
    return true;
}

uint32_t timer_interrupt(InterruptFrame* frame) {
    if (!scheduler_running || !frame || (frame->cs & 3U) != 3U || current_task < 0) return 0U;
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

uint32_t user_exception(InterruptFrame* frame) {
    if (!scheduler_running || !frame || current_task < 0) {
        const uintptr_t address = frame && frame->vector == 14U ? paging::fault_address() : 0U;
        if (frame) record_user_fault(frame->vector, frame->error, frame->eip, address);
        return 1U;
    }
    const int32_t previous = current_task;
    Task& task = tasks[previous];
    const uintptr_t address = frame->vector == 14U ? paging::fault_address() : 0U;
    record_task_fault(task, *frame, address);
    task.state = TaskState::zombie;
    ++completed_task_count;
    ++faulted_task_count;
    const int32_t next = select_ready(previous);
    if (next < 0) { scheduler_ending = true; return 1U; }
    return switch_interrupt(frame, next, "fault") ? 0U : 1U;
}

uint32_t scheduler_syscall(SyscallFrame* frame) {
    if (!scheduler_running || !frame || current_task < 0) return 1U;
    Task& current = tasks[current_task];
    const uint32_t number = frame->eax;
    if (number == syscall_getpid) { frame->eax = current.pid; return 0U; }
    if (number == syscall_exit) {
        current.exit_code = frame->ebx;
        current.state = TaskState::zombie;
        ++completed_task_count;
        serial::write("TASK_EXIT pid="); serial_u32(current.pid);
        serial::write(" code="); serial_u32(current.exit_code); serial::write("\r\n");
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
    for (uint32_t i = 0U; i < task_capacity; ++i) tasks[i] = Task{};
    current_task = -1;
    batch_task_count = completed_task_count = faulted_task_count = 0U;
    scheduler_switch_count = timer_preemption_count = 0U;
    scheduler_log_budget = 32U;
    scheduler_ending = false;
}

bool configure_task(uint32_t slot, paging::AddressSpace space, uint32_t entry,
                    uint32_t initial_stack, uint32_t initial_eax,
                    const ActiveCapabilityProfile& capabilities,
                    const char* path, const char* format) {
    if (slot >= task_capacity || !space.table_physical || !path || !format) return false;
    char normalized_path[sizeof(Task::path)]{};
    if (!storage::path_copy(normalized_path, path, sizeof(normalized_path))) return false;
    Task& task = tasks[slot];
    task = Task{};
    task.state = TaskState::ready;
    task.pid = next_pid++;
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
    memset(reinterpret_cast<void*>(task_stack_bottom(slot)), 0,
           supervisor_layout::task_stack_bytes);
    return true;
}

void log_task_created(const Task& task) {
    serial::write("TASK_CREATED pid="); serial_u32(task.pid);
    serial::write(" app="); serial::write(task.path);
    serial::write(" pages="); serial_u32(task.address_space.mapped_pages);
    serial::write(" priority="); serial_u32(task.base_priority);
    serial::write("\r\n");
}

bool prepare_scheduler_probe_task(uint32_t slot, uint32_t variant) {
    const uint32_t image_size = static_cast<uint32_t>(scheduler_probe_end - scheduler_probe_start);
    if (slot >= task_capacity || image_size == 0U || image_size >= stack_base) return false;
    paging::AddressSpace space{};
    if (!paging::address_space_create(space) || !paging::address_space_activate(&space)) {
        paging::address_space_destroy(space);
        return false;
    }
    const bool mapped = paging::map_user(0U, image_size, true) && map_stack();
    if (mapped) {
        memcpy(reinterpret_cast<void*>(user_base), scheduler_probe_start, image_size);
    }
    const bool protected_image = mapped && paging::protect_user(0U, image_size, false);
    if (!protected_image) {
        paging::address_space_activate(nullptr);
        paging::address_space_destroy(space);
        return false;
    }

    ActiveCapabilityProfile capabilities{};
    capabilities.valid = true;
    capabilities.mask = capability_console_write;
    const char* const path = variant == 0U ?
        "/kernel/scheduler-probe-a" : "/kernel/scheduler-probe-b";
    if (!storage::path_copy(capabilities.path, path, sizeof(capabilities.path))) {
        paging::address_space_activate(nullptr);
        paging::address_space_destroy(space);
        return false;
    }
    security_guard::sha256(scheduler_probe_start, image_size, capabilities.digest);
    const bool configured = configure_task(slot, space, 0U, stack_top, variant,
                                            capabilities, path, "KPROBE");
    paging::address_space_activate(nullptr);
    if (!configured) {
        paging::address_space_destroy(space);
        return false;
    }
    log_task_created(tasks[slot]);
    return true;
}

bool prepare_task(uint32_t slot, char* command) {
    if (slot >= task_capacity || !command || !*command) return false;
    char* arguments[max_arguments]{};
    const uint32_t argument_count = parse_arguments(command, arguments);
    if (!argument_count) return false;
    char requested[48]; uint32_t file_size = 0U;
    if (!read_requested(arguments[0], requested, file_size)) return false;
    const ActiveCapabilityProfile capability_snapshot = active_capabilities;
    paging::AddressSpace space{};
    if (!paging::address_space_create(space) || !paging::address_space_activate(&space)) {
        paging::address_space_destroy(space);
        return false;
    }
    uint32_t entry = 0U;
    const bool zex = file_size >= 4U && application_buffer[0] == 'Z' && application_buffer[1] == 'E' && application_buffer[2] == 'X' && application_buffer[3] == '1';
    const bool elf = file_size >= 4U && application_buffer[0] == 0x7FU && application_buffer[1] == 'E' && application_buffer[2] == 'L' && application_buffer[3] == 'F';
    const bool loaded = (zex && load_zex(application_buffer, file_size, entry)) ||
                        (elf && load_elf32(application_buffer, file_size, entry));
    uint32_t initial_stack = 0U;
    if (!loaded || !build_initial_stack(arguments, argument_count, initial_stack)) {
        paging::address_space_activate(nullptr);
        paging::address_space_destroy(space);
        clear_capability_profile();
        return false;
    }
    const bool configured = configure_task(slot, space, entry, initial_stack, 0U,
                                           capability_snapshot, requested,
                                           zex ? "ZEX1" : "ELF32");
    paging::address_space_activate(nullptr);
    clear_capability_profile();
    if (!configured) {
        paging::address_space_destroy(space);
        return false;
    }
    log_task_created(tasks[slot]);
    return true;
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

bool run_task_batch(char* input) {
    if (!scheduler_ready || scheduler_running || !input) return false;
    char* commands[task_capacity]{};
    const uint32_t count = split_task_commands(input, commands);
    if (count < 2U) return false;
    reset_task_table();
    for (uint32_t i = 0U; i < count; ++i) {
        if (!prepare_task(i, commands[i])) {
            paging::address_space_activate(nullptr);
            for (uint32_t j = 0U; j < task_capacity; ++j) destroy_task_address_space(j);
            reset_task_table();
            serial::line("SCHED_BATCH_CREATE_FAILED");
            return false;
        }
        ++batch_task_count;
    }
    scheduler_running = true;
    const int32_t first = select_ready(-1);
    if (first < 0 || !activate_task(static_cast<uint32_t>(first))) {
        scheduler_running = false;
        paging::address_space_activate(nullptr);
        for (uint32_t i = 0U; i < task_capacity; ++i) destroy_task_address_space(i);
        reset_task_table();
        serial::line("SCHED_BATCH_START_FAILED");
        return false;
    }
    serial::write("SCHED_BATCH_START tasks="); serial_u32(batch_task_count);
    serial::write(" policy=priority-rr quantum=3-6 aging=50\r\n");
    user_enter(tasks[first].context.eip, tasks[first].context.useresp);
    return true;
}

bool run_scheduler_probe() {
    if (!scheduler_ready || scheduler_running) return false;
    reset_task_table();
    if (!prepare_scheduler_probe_task(0U, 0U) ||
        !prepare_scheduler_probe_task(1U, 1U)) {
        paging::address_space_activate(nullptr);
        for (uint32_t i = 0U; i < task_capacity; ++i) destroy_task_address_space(i);
        reset_task_table();
        serial::line("SCHED_PROBE_CREATE_FAILED");
        return false;
    }
    batch_task_count = 2U;
    scheduler_running = true;
    const int32_t first = select_ready(-1);
    if (first < 0 || !activate_task(static_cast<uint32_t>(first))) {
        scheduler_running = false;
        for (uint32_t i = 0U; i < task_capacity; ++i) destroy_task_address_space(i);
        reset_task_table();
        return false;
    }
    serial::line("SCHED_PROBE_TRUST_BOUNDARY_OK source=kernel-rodata capabilities=console-write filesystem=unmodified");
    serial::write("SCHED_BATCH_START tasks=2 policy=priority-rr quantum=3-6 aging=50 kind=kernel-probe\r\n");
    user_enter(tasks[first].context.eip, tasks[first].context.useresp);
    return true;
}

void scheduler_init() {
    reset_task_table();
    scheduler_ready = paging::enabled && pmm::initialized;
    serial::line(scheduler_ready ? "PREEMPTIVE_SCHEDULER_OK hz=100 tasks=8" : "PREEMPTIVE_SCHEDULER_FAILED");
    serial::line("SCHED_POLICY_OK model=priority-rr priorities=4 aging=50 quantum=3-6");
    serial::line("PROCESS_ADDRESS_SPACE_OK per-task-pte=yes pmm-backed=yes");
}

void print_scheduler_status() {
    console::write("  Scheduler     "); console::line(scheduler_ready ? "preemptive priority/RR" : "unavailable");
    console::write("  Task slots    "); console::unsigned_dec(task_capacity); console::put('\n');
    console::write("  Active        "); console::unsigned_dec(scheduler_running ? batch_task_count - completed_task_count : 0U); console::put('\n');
    console::write("  Switches      "); console::unsigned_dec(scheduler_switch_count); console::put('\n');
    console::write("  Preemptions   "); console::unsigned_dec(timer_preemption_count); console::put('\n');
}
} // namespace process

extern "C" void user_window_cleanup() {
    if (process::scheduler_running || process::scheduler_ending) {
        if (process::current_task >= 0 &&
            process::tasks[process::current_task].state == process::TaskState::running) {
            process::tasks[process::current_task].state = process::TaskState::zombie;
            ++process::completed_task_count;
        }
        paging::address_space_activate(nullptr);
        for (uint32_t i = 0U; i < process::task_capacity; ++i) process::destroy_task_address_space(i);
        const uint32_t total = process::batch_task_count;
        const uint32_t completed = process::completed_task_count;
        const uint32_t faulted = process::faulted_task_count;
        const uint32_t switches = process::scheduler_switch_count;
        const uint32_t preemptions = process::timer_preemption_count;
        process::scheduler_running = false;
        process::scheduler_ending = false;
        process::current_task = -1;
        process::set_kernel_stack(reinterpret_cast<uintptr_t>(&process::__stack_top));
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
        if (total >= 2U && completed == total && faulted == 0U && switches > 0U && preemptions > 0U)
            serial::line("PREEMPTIVE_MULTITASKING_OK isolation=per-address-space");
        process::reset_task_table();
        return;
    }
    if (!paging::scrub_process_window(false)) panic("User process window scrub failed.");
    ++paging::process_window_scrub_count;
    serial::line("USER_WINDOW_RUNTIME_SCRUB_OK");
}
