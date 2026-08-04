#include <cstdint>
#include <cstdlib>
#include <iostream>
using uint8_t = std::uint8_t;
using uint32_t = std::uint32_t;
#include "../kernel/parts/scheduler_policy.inc"

namespace {
void require(bool condition) { if (!condition) std::exit(1); }
}
int main() {
    using namespace scheduler_policy;
    Candidate tasks[4]{{true,1,1,0},{true,1,1,0},{true,2,2,0},{false,3,3,0}};
    require(select(tasks,4,0) == 2);
    tasks[2].ready = false;
    require(select(tasks,4,0) == 1);
    require(select(tasks,4,1) == 0);
    tasks[0].wait_ticks = aging_ticks - 1U;
    account_wait(tasks[0]);
    require(tasks[0].dynamic_priority == 2U && tasks[0].wait_ticks == 0U);
    reset_after_run(tasks[0]);
    require(tasks[0].dynamic_priority == 1U && quantum_ticks(0) == 3U && quantum_ticks(3) == 6U);
    std::cout << "SCHEDULER_POLICY_TEST_OK priority-levels=4 rr=equal aging=50 quantum=3-6\n";
}
