namespace process {
namespace process_handles {

constexpr uint32_t handle_capacity = 8U;
constexpr uint32_t handle_index_mask = 0xFFU;
constexpr uint32_t handle_generation_shift = 8U;
constexpr uint8_t right_wait = 1U << 0U;
constexpr uint8_t right_query = 1U << 1U;
constexpr uint8_t rights_all = right_wait | right_query;

struct ObjectKey {
    uint16_t slot = 0U;
    uint16_t generation = 0U;
};

bool key_equal(const ObjectKey& left, const ObjectKey& right) {
    return left.slot == right.slot && left.generation == right.generation;
}

bool key_valid(const ObjectKey& key) {
    return key.generation != 0U;
}

struct Entry {
    bool used = false;
    uint8_t rights = 0U;
    uint16_t generation = 0U;
    ObjectKey object{};
};

struct Table {
    Entry entries[handle_capacity]{};
    uint16_t generations[handle_capacity]{};
};

uint16_t next_generation(uint16_t current) {
    ++current;
    return current ? current : 1U;
}

uint32_t encode(uint32_t index, uint16_t generation) {
    if (index >= handle_capacity || !generation) return 0U;
    return (static_cast<uint32_t>(generation) << handle_generation_shift) | (index + 1U);
}

bool decode(uint32_t handle, uint32_t& index, uint16_t& generation) {
    const uint32_t encoded_index = handle & handle_index_mask;
    generation = static_cast<uint16_t>(handle >> handle_generation_shift);
    if (!encoded_index || encoded_index > handle_capacity || !generation) return false;
    index = encoded_index - 1U;
    return true;
}

uint32_t allocate(Table& table, const ObjectKey& object, uint8_t rights) {
    if (!key_valid(object) || !rights || (rights & ~rights_all)) return 0U;
    for (uint32_t index = 0U; index < handle_capacity; ++index) {
        if (table.entries[index].used) continue;
        table.generations[index] = next_generation(table.generations[index]);
        Entry& entry = table.entries[index];
        entry.used = true;
        entry.rights = rights;
        entry.generation = table.generations[index];
        entry.object = object;
        return encode(index, entry.generation);
    }
    return 0U;
}

bool resolve(const Table& table, uint32_t handle, uint8_t required_rights,
             ObjectKey& object) {
    uint32_t index = 0U;
    uint16_t generation = 0U;
    if (!decode(handle, index, generation)) return false;
    const Entry& entry = table.entries[index];
    if (!entry.used || entry.generation != generation ||
        (entry.rights & required_rights) != required_rights ||
        !key_valid(entry.object)) return false;
    object = entry.object;
    return true;
}

bool close(Table& table, uint32_t handle, ObjectKey& object) {
    uint32_t index = 0U;
    uint16_t generation = 0U;
    if (!decode(handle, index, generation)) return false;
    Entry& entry = table.entries[index];
    if (!entry.used || entry.generation != generation || !key_valid(entry.object)) return false;
    object = entry.object;
    entry = Entry{};
    return true;
}

uint32_t active_count(const Table& table) {
    uint32_t count = 0U;
    for (const Entry& entry : table.entries) if (entry.used) ++count;
    return count;
}


} // namespace process_handles
} // namespace process
