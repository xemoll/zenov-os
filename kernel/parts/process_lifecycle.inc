namespace process {
namespace process_lifecycle {

using ObjectKey = process_handles::ObjectKey;

constexpr uint32_t object_capacity = 16U;
constexpr uint32_t pid_index_mask = 0xFFU;
constexpr uint32_t pid_generation_shift = 8U;

enum class TerminationReason : uint8_t {
    running = 0U,
    exited = 1U,
    faulted = 2U,
    cancelled = 3U,
};

struct Object {
    bool used = false;
    bool signaled = false;
    bool parent_reaped = false;
    uint8_t reserved = 0U;
    uint16_t generation = 0U;
    uint16_t slot = 0U;
    int32_t task_slot = -1;
    uint32_t pid = 0U;
    uint32_t parent_pid = 0U;
    uint32_t exit_code = 0U;
    uint32_t fault_vector = 0U;
    uint32_t runtime_ticks = 0U;
    uint32_t context_switches = 0U;
    uint32_t handle_references = 0U;
    TerminationReason reason = TerminationReason::running;
};

Object objects[object_capacity]{};
uint16_t generations[object_capacity]{};
uint32_t live_objects = 0U;
uint32_t retained_objects = 0U;

uint16_t next_generation(uint16_t current) {
    ++current;
    return current ? current : 1U;
}

uint32_t encode_pid(uint32_t slot, uint16_t generation) {
    if (slot >= object_capacity || !generation) return 0U;
    return (static_cast<uint32_t>(generation - 1U) << pid_generation_shift) | (slot + 1U);
}

bool decode_pid(uint32_t pid, uint32_t& slot, uint16_t& generation) {
    const uint32_t encoded_slot = pid & pid_index_mask;
    const uint32_t encoded_generation = pid >> pid_generation_shift;
    if (!encoded_slot || encoded_slot > object_capacity || encoded_generation > 0xFFFEU) return false;
    generation = static_cast<uint16_t>(encoded_generation + 1U);
    slot = encoded_slot - 1U;
    return true;
}

ObjectKey key_for(const Object& object) {
    return ObjectKey{object.slot, object.generation};
}

Object* find(const ObjectKey& key) {
    if (!process_handles::key_valid(key) || key.slot >= object_capacity) return nullptr;
    Object& object = objects[key.slot];
    return object.used && object.generation == key.generation ? &object : nullptr;
}

const Object* find_const(const ObjectKey& key) {
    if (!process_handles::key_valid(key) || key.slot >= object_capacity) return nullptr;
    const Object& object = objects[key.slot];
    return object.used && object.generation == key.generation ? &object : nullptr;
}

Object* find_pid(uint32_t pid) {
    uint32_t slot = 0U;
    uint16_t generation = 0U;
    if (!decode_pid(pid, slot, generation)) return nullptr;
    Object& object = objects[slot];
    return object.used && object.generation == generation && object.pid == pid ? &object : nullptr;
}


bool collect_if_unreferenced(const ObjectKey& key) {
    Object* object = find(key);
    if (!object || !object->signaled || !object->parent_reaped || object->handle_references) return false;
    *object = Object{};
    if (retained_objects) --retained_objects;
    return true;
}

bool create(uint32_t parent_pid, int32_t task_slot, ObjectKey& key, uint32_t& pid) {
    for (uint32_t slot = 0U; slot < object_capacity; ++slot) {
        if (objects[slot].used) continue;
        generations[slot] = next_generation(generations[slot]);
        Object& object = objects[slot];
        object = Object{};
        object.used = true;
        object.generation = generations[slot];
        object.slot = static_cast<uint16_t>(slot);
        object.task_slot = task_slot;
        object.pid = encode_pid(slot, object.generation);
        object.parent_pid = parent_pid;
        key = key_for(object);
        pid = object.pid;
        ++live_objects;
        ++retained_objects;
        return true;
    }
    return false;
}

bool destroy_unstarted(const ObjectKey& key) {
    Object* object = find(key);
    if (!object || object->signaled || object->handle_references) return false;
    *object = Object{};
    if (live_objects) --live_objects;
    if (retained_objects) --retained_objects;
    return true;
}

bool retain(const ObjectKey& key) {
    Object* object = find(key);
    if (!object || object->handle_references == 0xFFFFFFFFU) return false;
    ++object->handle_references;
    return true;
}

bool release(const ObjectKey& key) {
    Object* object = find(key);
    if (!object || !object->handle_references) return false;
    --object->handle_references;
    (void)collect_if_unreferenced(key);
    return true;
}

bool terminate(const ObjectKey& key, uint32_t exit_code, uint32_t fault_vector,
               uint32_t runtime_ticks, uint32_t context_switches,
               TerminationReason reason) {
    Object* object = find(key);
    if (!object || object->signaled || reason == TerminationReason::running) return false;
    object->signaled = true;
    object->exit_code = exit_code;
    object->fault_vector = fault_vector;
    object->runtime_ticks = runtime_ticks;
    object->context_switches = context_switches;
    object->reason = reason;
    if (live_objects) --live_objects;
    if (object->parent_pid == 0U) object->parent_reaped = true;
    (void)collect_if_unreferenced(key);
    return true;
}

bool reap_parent(const ObjectKey& key, uint32_t parent_pid) {
    Object* object = find(key);
    if (!object || !object->signaled || object->parent_reaped ||
        object->parent_pid != parent_pid) return false;
    object->parent_reaped = true;
    (void)collect_if_unreferenced(key);
    return true;
}

bool reparent(uint32_t old_parent_pid, uint32_t new_parent_pid) {
    bool changed = false;
    for (uint32_t slot = 0U; slot < object_capacity; ++slot) {
        Object& object = objects[slot];
        if (!object.used || object.parent_pid != old_parent_pid) continue;
        object.parent_pid = new_parent_pid;
        changed = true;
        if (new_parent_pid == 0U && object.signaled) {
            object.parent_reaped = true;
            const ObjectKey key = key_for(object);
            (void)collect_if_unreferenced(key);
        }
    }
    return changed;
}


Object* first_child(uint32_t parent_pid, bool signaled_only) {
    for (Object& object : objects) {
        if (!object.used || object.parent_pid != parent_pid || object.parent_reaped) continue;
        if (signaled_only && !object.signaled) continue;
        return &object;
    }
    return nullptr;
}

void reset() {
    for (Object& object : objects) object = Object{};
    live_objects = 0U;
    retained_objects = 0U;
}

} // namespace process_lifecycle
} // namespace process
