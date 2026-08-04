namespace scheduler_policy {
constexpr uint8_t priority_levels = 4U;
constexpr uint8_t default_priority = 1U;
constexpr uint8_t maximum_priority = priority_levels - 1U;
constexpr uint32_t aging_ticks = 50U;

struct Candidate {
    bool ready;
    uint8_t base_priority;
    uint8_t dynamic_priority;
    uint32_t wait_ticks;
};

uint8_t clamp_priority(uint8_t priority) {
    return priority > maximum_priority ? maximum_priority : priority;
}
uint8_t quantum_ticks(uint8_t priority) {
    return static_cast<uint8_t>(3U + clamp_priority(priority));
}
void account_wait(Candidate& candidate) {
    if (!candidate.ready) { candidate.wait_ticks = 0U; return; }
    ++candidate.wait_ticks;
    if (candidate.wait_ticks >= aging_ticks && candidate.dynamic_priority < maximum_priority) {
        ++candidate.dynamic_priority;
        candidate.wait_ticks = 0U;
    }
}
void reset_after_run(Candidate& candidate) {
    candidate.dynamic_priority = clamp_priority(candidate.base_priority);
    candidate.wait_ticks = 0U;
}
int select(const Candidate* candidates, uint32_t count, int current) {
    if (!candidates || !count) return -1;
    int selected = -1;
    uint8_t best = 0U;
    for (uint32_t distance = 1U; distance <= count; ++distance) {
        const uint32_t index = static_cast<uint32_t>((current + static_cast<int>(distance) + static_cast<int>(count)) % static_cast<int>(count));
        if (!candidates[index].ready) continue;
        if (selected < 0 || candidates[index].dynamic_priority > best) {
            selected = static_cast<int>(index);
            best = candidates[index].dynamic_priority;
        }
    }
    return selected;
}
} // namespace scheduler_policy
