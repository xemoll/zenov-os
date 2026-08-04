#include <cstdint>
#include <cstdio>
#include <cstdlib>

using std::int32_t;
using std::uint8_t;
using std::uint16_t;
using std::uint32_t;

#include "../kernel/parts/process_handles.inc"
#include "../kernel/parts/process_lifecycle.inc"

namespace {
[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "kernel-process-lifecycle-test: %s\n", message);
    std::exit(1);
}
void require(bool condition, const char* message) {
    if (!condition) fail(message);
}
}

int main() {
    using namespace process;
    using namespace process::process_handles;
    using namespace process::process_lifecycle;

    reset();
    Table parent_handles{};
    ObjectKey first_key{};
    uint32_t first_pid = 0U;
    require(create(0x100U, 3, first_key, first_pid), "first process creation failed");
    require(first_pid != 0U && find_pid(first_pid) != nullptr, "first PID lookup failed");
    require(live_objects == 1U && retained_objects == 1U, "object counters incorrect after create");

    require(retain(first_key), "process retain failed");
    const uint32_t handle = allocate(parent_handles, first_key, right_wait | right_query);
    require(handle != 0U, "handle allocation failed");
    ObjectKey resolved{};
    require(resolve(parent_handles, handle, right_wait, resolved) && key_equal(resolved, first_key),
            "wait right resolution failed");
    require(resolve(parent_handles, handle, right_query, resolved), "query right resolution failed");

    require(terminate(first_key, 37U, 0U, 91U, 7U, TerminationReason::exited),
            "process termination failed");
    const Object* zombie = find_const(first_key);
    require(zombie && zombie->signaled && zombie->exit_code == 37U && zombie->task_slot == 3,
            "zombie state was not retained");
    require(live_objects == 0U && retained_objects == 1U, "zombie counters incorrect");

    require(reap_parent(first_key, 0x100U), "parent reap failed");
    require(find_const(first_key) != nullptr, "handle did not retain reaped process object");

    ObjectKey closed{};
    require(close(parent_handles, handle, closed) && key_equal(closed, first_key),
            "handle close failed");
    require(!resolve(parent_handles, handle, right_wait, resolved), "closed handle resolved");
    require(release(closed), "process release failed");
    require(find_const(first_key) == nullptr && retained_objects == 0U,
            "reaped process object was not collected");

    ObjectKey second_key{};
    uint32_t second_pid = 0U;
    require(create(0x100U, 4, second_key, second_pid), "second process creation failed");
    require(second_pid != first_pid, "PID generation did not change after slot reuse");
    require(second_key.slot == first_key.slot && second_key.generation != first_key.generation,
            "object slot generation did not advance");
    require(find_pid(first_pid) == nullptr && find(first_key) == nullptr,
            "stale PID or object key resolved after generation rollover");
    require(!resolve(parent_handles, handle, right_query, resolved),
            "stale handle resolved after generation rollover");

    require(terminate(second_key, 0U, 14U, 3U, 1U, TerminationReason::faulted),
            "second process termination failed");
    require(reap_parent(second_key, 0x100U), "second process reap failed");
    require(find(second_key) == nullptr, "unreferenced process object was not collected");
    require(live_objects == 0U && retained_objects == 0U && active_count(parent_handles) == 0U,
            "final process or handle counters are not zero");

    std::puts("KERNEL_PROCESS_LIFECYCLE_TEST_OK objects=16 handles=8 pid-generation=1 waitable=1 reap=deferred stale=blocked");
    return 0;
}
