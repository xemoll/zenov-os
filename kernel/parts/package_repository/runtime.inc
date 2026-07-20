bool verify_candidate(Target candidate[maximum_targets], uint32_t& candidate_count, State& next, bool commit) {
    const Validation validation = verify_repository(candidate, candidate_count, next);
    if (validation != Validation::okay) {
        serial::write(commit ? "ZENREPO_REFRESH_REJECTED reason=" : "ZENREPO_CHECK_REJECTED reason=");
        serial::line(validation_text(validation));
        return false;
    }
    if (commit && !persist_state(next)) {
        serial::line("ZENREPO_STATE_COMMIT_FAILED");
        return false;
    }
    return true;
}

bool check() {
    Target candidate[maximum_targets]{};
    uint32_t candidate_count = 0;
    State next{};
    if (!verify_candidate(candidate, candidate_count, next, false)) return false;
    serial::write("ZENREPO_CHECK_OK packages=");
    process::serial_u32(candidate_count);
    serial::write("\r\n");
    return true;
}

bool refresh() {
    Target candidate[maximum_targets]{};
    uint32_t candidate_count = 0;
    State next{};
    if (!verify_candidate(candidate, candidate_count, next, true)) return false;
    memset(active_targets, 0, sizeof(active_targets));
    for (uint32_t i = 0; i < candidate_count; ++i) active_targets[i] = candidate[i];
    active_target_count = candidate_count;
    ready = true;
    serial::write("ZENREPO_READY trust=verified packages=");
    process::serial_u32(active_target_count);
    serial::write("\r\n");
    return true;
}

bool init() {
    ready = false;
    active_target_count = 0;
    if (!storage::ready()) return false;
    metadata_buffer = metadata_storage;
    memset(metadata_buffer, 0, maximum_metadata_bytes);
    const Validation state_validation = load_state();
    if (state_validation != Validation::okay) {
        serial::write("ZENREPO_STATE_INVALID reason=");
        serial::line(validation_text(state_validation));
        return false;
    }
    return refresh();
}

const Target* find_target(const char* name, const char* version, const char* entrypoint, uint8_t payload_type) {
    if (!ready || !name || !version || !entrypoint) return nullptr;
    for (uint32_t i = 0; i < active_target_count; ++i) {
        const Target& target = active_targets[i];
        if (string_equal(name, target.name) && string_equal(version, target.version) &&
            string_equal(entrypoint, target.entrypoint) && payload_type == target.payload_type) return &target;
    }
    return nullptr;
}

char folded_char(char value) {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + 32) : value;
}

int lexical_compare(const char* left, const char* right) {
    if (!left) left = "";
    if (!right) right = "";
    while (*left && *right && *left == *right) {
        ++left;
        ++right;
    }
    const uint8_t a = static_cast<uint8_t>(*left);
    const uint8_t b = static_cast<uint8_t>(*right);
    return a < b ? -1 : (a > b ? 1 : 0);
}

int compare_release(const char* left, const char* right) {
    uint32_t a[3]{}, b[3]{};
    const char* values[2] = {left, right};
    uint32_t* outputs[2] = {a, b};
    for (uint32_t side = 0; side < 2U; ++side) {
        const char* cursor = values[side];
        if (!cursor) return lexical_compare(left, right);
        for (uint32_t part = 0; part < 3U; ++part) {
            if (*cursor < '0' || *cursor > '9') return lexical_compare(left, right);
            uint32_t value = 0;
            while (*cursor >= '0' && *cursor <= '9') {
                const uint32_t digit = static_cast<uint32_t>(*cursor - '0');
                if (value > (0xFFFFFFFFU - digit) / 10U) return lexical_compare(left, right);
                value = value * 10U + digit;
                ++cursor;
            }
            outputs[side][part] = value;
            if (part < 2U) {
                if (*cursor != '.') return lexical_compare(left, right);
                ++cursor;
            }
        }
        if (*cursor && *cursor != '-' && *cursor != '+') return lexical_compare(left, right);
    }
    for (uint32_t i = 0; i < 3U; ++i) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

const Target* latest_target(const char* name) {
    if (!ready || !name || !*name) return nullptr;
    const Target* latest = nullptr;
    for (uint32_t i = 0; i < active_target_count; ++i) {
        const Target& target = active_targets[i];
        if (!string_equal(name, target.name)) continue;
        if (!latest || compare_release(target.version, latest->version) > 0) latest = &target;
    }
    return latest;
}

const Target* target_at(uint32_t index) {
    return ready && index < active_target_count ? &active_targets[index] : nullptr;
}

uint32_t target_count() {
    return ready ? active_target_count : 0U;
}

bool contains_folded(const char* text, const char* query) {
    if (!text || !query || !*query) return false;
    for (const char* start = text; *start; ++start) {
        const char* left = start;
        const char* right = query;
        while (*left && *right && folded_char(*left) == folded_char(*right)) {
            ++left;
            ++right;
        }
        if (!*right) return true;
    }
    return false;
}

void print_status() {
    console::write("  State         ");
    console::line(ready ? "ready" : "unavailable");
    console::line("  Trust chain   verified signatures, delegations, expiry and anti-rollback");
    console::write("  Packages      ");
    console::unsigned_dec(active_target_count);
    console::put('\n');
    console::line("  Transport     offline ZenovFS repository only");
    console::line("  Freshness     compiled release floor; secure RTC not available");
    console::line("  Revisions     internal and hidden from normal UI");
    serial::line("ZENREPO_STATUS_OK");
}

void print_targets() {
    for (uint32_t i = 0; i < active_target_count; ++i) {
        console::write("  ");
        console::write(active_targets[i].name);
        console::put('@');
        console::write(active_targets[i].version);
        console::write("  ");
        console::line(active_targets[i].path);
    }
    if (!active_target_count) console::line("  <no authorized repository targets>");
    serial::line("ZENREPO_TARGETS_OK");
}

void print_search(const char* query) {
    uint32_t matches = 0;
    for (uint32_t i = 0; i < active_target_count; ++i) {
        const Target& target = active_targets[i];
        if (!contains_folded(target.name, query) && !contains_folded(target.path, query)) continue;
        console::write("  ");
        console::write(target.name);
        console::put('@');
        console::write(target.version);
        console::write("  ");
        console::line(target.path);
        ++matches;
    }
    if (!matches) console::line("  <no matching packages>");
    serial::write("ZENREPO_SEARCH_OK matches=");
    process::serial_u32(matches);
    serial::write("\r\n");
}

void print_policy(const char* name) {
    const Target* target = latest_target(name);
    if (!target) {
        console::line("Package is not present in signed repository metadata.");
        serial::line("ZENREPO_POLICY_NOT_FOUND");
        return;
    }
    console::write("  Package       ");
    console::write(target->name);
    console::put('@');
    console::line(target->version);
    console::write("  Syscall mask  ");
    console::unsigned_dec(target->syscall_mask);
    console::put('\n');
    console::write("  Read scope    ");
    console::line(target->read_scope[0] ? target->read_scope : "<none>");
    console::write("  Write scope   ");
    console::line(target->write_scope[0] ? target->write_scope : "<none>");
    console::write("  Entrypoint    ");
    console::line(target->entrypoint);
    serial::line("ZENREPO_POLICY_OK");
}
