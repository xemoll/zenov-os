#ifndef ZENOV_PSX_R3000A_INC
#define ZENOV_PSX_R3000A_INC
namespace psx_r3000a {

enum class StopReason : uint8_t {
    running,
    exited,
    instruction_budget,
    invalid_instruction,
    arithmetic_overflow,
    memory_fault,
    alignment_fault,
    unsupported_bios_call,
    output_budget
};

struct Machine {
    uint32_t registers[32]{};
    uint32_t hi = 0U, lo = 0U;
    uint32_t pc = 0U, next_pc = 0U;
    uint32_t pending_load_register = 0U, pending_load_value = 0U;
    uint32_t steps = 0U, exit_code = 0U, fault_address = 0U, fault_instruction = 0U;
    uint32_t output_bytes = 0U, output_limit = 16U * 1024U;
    uint8_t* ram = nullptr;
    uint32_t ram_size = 0U;
    void (*output)(void*, char) = nullptr;
    void* output_context = nullptr;
    StopReason stop = StopReason::running;
    bool next_is_delay_slot = false;
};

constexpr uint32_t register_v0 = 2U;
constexpr uint32_t register_a0 = 4U;
constexpr uint32_t register_a1 = 5U;
constexpr uint32_t register_a2 = 6U;
constexpr uint32_t register_t1 = 9U;
constexpr uint32_t register_sp = 29U;
constexpr uint32_t register_ra = 31U;
constexpr uint32_t maximum_hle_string = 4096U;

const char* stop_name(StopReason reason) {
    switch (reason) {
        case StopReason::running: return "running";
        case StopReason::exited: return "exited";
        case StopReason::instruction_budget: return "instruction-budget";
        case StopReason::invalid_instruction: return "invalid-instruction";
        case StopReason::arithmetic_overflow: return "arithmetic-overflow";
        case StopReason::memory_fault: return "memory-fault";
        case StopReason::alignment_fault: return "alignment-fault";
        case StopReason::unsupported_bios_call: return "unsupported-bios-call";
        case StopReason::output_budget: return "output-budget";
    }
    return "unknown";
}

bool address_offset(const Machine& machine, uint32_t address, uint32_t width, uint32_t& offset) {
    if (!width) return false;
    uint32_t physical = 0U;
    if (address < machine.ram_size) physical = address;
    else if (address >= 0x80000000U && address < 0x80000000U + machine.ram_size) physical = address - 0x80000000U;
    else if (address >= 0xA0000000U && address < 0xA0000000U + machine.ram_size) physical = address - 0xA0000000U;
    else return false;
    if (width > machine.ram_size - physical) return false;
    offset = physical;
    return true;
}

bool read8(Machine& machine, uint32_t address, uint8_t& value) {
    uint32_t offset = 0U;
    if (!address_offset(machine, address, 1U, offset)) { machine.fault_address = address; return false; }
    value = machine.ram[offset];
    return true;
}

bool read16(Machine& machine, uint32_t address, uint16_t& value) {
    if (address & 1U) { machine.stop = StopReason::alignment_fault; machine.fault_address = address; return false; }
    uint32_t offset = 0U;
    if (!address_offset(machine, address, 2U, offset)) { machine.fault_address = address; return false; }
    value = static_cast<uint16_t>(machine.ram[offset]) |
        static_cast<uint16_t>(static_cast<uint16_t>(machine.ram[offset + 1U]) << 8U);
    return true;
}

bool read32(Machine& machine, uint32_t address, uint32_t& value) {
    if (address & 3U) { machine.stop = StopReason::alignment_fault; machine.fault_address = address; return false; }
    uint32_t offset = 0U;
    if (!address_offset(machine, address, 4U, offset)) { machine.fault_address = address; return false; }
    value = static_cast<uint32_t>(machine.ram[offset]) |
        (static_cast<uint32_t>(machine.ram[offset + 1U]) << 8U) |
        (static_cast<uint32_t>(machine.ram[offset + 2U]) << 16U) |
        (static_cast<uint32_t>(machine.ram[offset + 3U]) << 24U);
    return true;
}

bool write8(Machine& machine, uint32_t address, uint8_t value) {
    uint32_t offset = 0U;
    if (!address_offset(machine, address, 1U, offset)) { machine.fault_address = address; return false; }
    machine.ram[offset] = value;
    return true;
}

bool write16(Machine& machine, uint32_t address, uint16_t value) {
    if (address & 1U) { machine.stop = StopReason::alignment_fault; machine.fault_address = address; return false; }
    uint32_t offset = 0U;
    if (!address_offset(machine, address, 2U, offset)) { machine.fault_address = address; return false; }
    machine.ram[offset] = static_cast<uint8_t>(value);
    machine.ram[offset + 1U] = static_cast<uint8_t>(value >> 8U);
    return true;
}

bool write32(Machine& machine, uint32_t address, uint32_t value) {
    if (address & 3U) { machine.stop = StopReason::alignment_fault; machine.fault_address = address; return false; }
    uint32_t offset = 0U;
    if (!address_offset(machine, address, 4U, offset)) { machine.fault_address = address; return false; }
    machine.ram[offset] = static_cast<uint8_t>(value);
    machine.ram[offset + 1U] = static_cast<uint8_t>(value >> 8U);
    machine.ram[offset + 2U] = static_cast<uint8_t>(value >> 16U);
    machine.ram[offset + 3U] = static_cast<uint8_t>(value >> 24U);
    return true;
}

uint32_t sign_extend16(uint32_t value) { return (value & 0x8000U) ? value | 0xFFFF0000U : value; }
uint32_t sign_extend8(uint32_t value) { return (value & 0x80U) ? value | 0xFFFFFF00U : value; }
bool signed_less(uint32_t left, uint32_t right) { return (left ^ 0x80000000U) < (right ^ 0x80000000U); }
uint32_t arithmetic_shift_right(uint32_t value, uint32_t shift) {
    if (!shift) return value;
    const uint32_t shifted = value >> shift;
    return (value & 0x80000000U) ? shifted | ~(0xFFFFFFFFU >> shift) : shifted;
}

bool add_checked(uint32_t left, uint32_t right, uint32_t& result) {
    result = left + right;
    return ((~(left ^ right) & (left ^ result)) & 0x80000000U) == 0U;
}

bool sub_checked(uint32_t left, uint32_t right, uint32_t& result) {
    result = left - right;
    return ((((left ^ right) & (left ^ result)) & 0x80000000U) == 0U);
}

void initialize(Machine& machine, uint8_t* ram, uint32_t ram_size, uint32_t entry,
                uint32_t stack, uint32_t global_pointer,
                void (*output)(void*, char), void* context) {
    machine = Machine{};
    machine.ram = ram;
    machine.ram_size = ram_size;
    machine.pc = entry;
    machine.next_pc = entry + 4U;
    machine.registers[register_sp] = stack;
    machine.registers[28U] = global_pointer;
    machine.output = output;
    machine.output_context = context;
}

bool emit(Machine& machine, char value) {
    if (machine.output_bytes >= machine.output_limit) {
        machine.stop = StopReason::output_budget;
        return false;
    }
    ++machine.output_bytes;
    if (machine.output) machine.output(machine.output_context, value);
    return true;
}

bool hle_string(Machine& machine, uint32_t address) {
    for (uint32_t i = 0U; i < maximum_hle_string; ++i) {
        uint8_t value = 0U;
        if (address > 0xFFFFFFFFU - i || !read8(machine, address + i, value)) { machine.stop = StopReason::memory_fault; return false; }
        if (!value) return true;
        if (!emit(machine, static_cast<char>(value))) return false;
    }
    machine.stop = StopReason::memory_fault;
    machine.fault_address = address + maximum_hle_string;
    return false;
}

bool hle_write(Machine& machine) {
    if (machine.registers[register_a0] != 1U || machine.registers[register_a2] > maximum_hle_string) {
        machine.stop = StopReason::unsupported_bios_call;
        return false;
    }
    const uint32_t address = machine.registers[register_a1];
    const uint32_t count = machine.registers[register_a2];
    for (uint32_t i = 0U; i < count; ++i) {
        uint8_t value = 0U;
        if (address > 0xFFFFFFFFU - i || !read8(machine, address + i, value)) { machine.stop = StopReason::memory_fault; return false; }
        if (!emit(machine, static_cast<char>(value))) return false;
    }
    machine.registers[register_v0] = count;
    return true;
}

bool hle_vector(const Machine& machine, uint32_t& vector) {
    uint32_t offset = 0U;
    if (!address_offset(machine, machine.pc, 1U, offset) || (offset != 0xA0U && offset != 0xB0U)) return false;
    vector = offset;
    return true;
}

bool handle_hle(Machine& machine) {
    uint32_t vector = 0U;
    if (!hle_vector(machine, vector)) return false;
    const uint32_t call = machine.registers[register_t1];
    bool okay = true;
    if ((vector == 0xA0U && call == 0x03U) || (vector == 0xB0U && call == 0x35U)) {
        okay = hle_write(machine);
    } else if ((vector == 0xA0U && (call == 0x09U || call == 0x3CU)) ||
               (vector == 0xB0U && (call == 0x3BU || call == 0x3DU))) {
        okay = emit(machine, static_cast<char>(machine.registers[register_a0]));
    } else if ((vector == 0xA0U && call == 0x3EU) || (vector == 0xB0U && call == 0x3FU)) {
        okay = hle_string(machine, machine.registers[register_a0]);
    } else {
        machine.stop = StopReason::unsupported_bios_call;
        okay = false;
    }
    if (okay) {
        machine.pc = machine.registers[register_ra];
        machine.next_pc = machine.pc + 4U;
        machine.next_is_delay_slot = false;
    }
    return true;
}

bool step(Machine& machine) {
    if (machine.stop != StopReason::running || !machine.ram || machine.ram_size == 0U) return false;
    uint32_t vector = 0U;
    if (hle_vector(machine, vector)) {
        handle_hle(machine);
        return machine.stop == StopReason::running;
    }

    uint32_t instruction = 0U;
    if (!read32(machine, machine.pc, instruction)) {
        if (machine.stop == StopReason::running) machine.stop = StopReason::memory_fault;
        return false;
    }
    machine.fault_instruction = instruction;
    const uint32_t current_pc = machine.pc;
    const bool in_delay_slot = machine.next_is_delay_slot;
    machine.next_is_delay_slot = false;
    machine.pc = machine.next_pc;
    machine.next_pc += 4U;

    const uint32_t old_load_register = machine.pending_load_register;
    const uint32_t old_load_value = machine.pending_load_value;
    machine.pending_load_register = 0U;
    uint32_t written_register = 0U;
    auto write_register = [&](uint32_t index, uint32_t value) {
        if (index) { machine.registers[index] = value; written_register = index; }
    };
    auto delayed_register = [&](uint32_t index, uint32_t value) {
        if (index) { machine.pending_load_register = index; machine.pending_load_value = value; written_register = index; }
    };
    auto branch = [&](bool taken, uint32_t target) {
        if (in_delay_slot) { machine.stop = StopReason::invalid_instruction; return; }
        machine.next_is_delay_slot = true;
        if (taken) machine.next_pc = target;
    };

    const uint32_t opcode = instruction >> 26U;
    const uint32_t rs = (instruction >> 21U) & 31U;
    const uint32_t rt = (instruction >> 16U) & 31U;
    const uint32_t rd = (instruction >> 11U) & 31U;
    const uint32_t shift = (instruction >> 6U) & 31U;
    const uint32_t immediate = instruction & 0xFFFFU;
    const uint32_t signed_immediate = sign_extend16(immediate);
    const uint32_t branch_target = machine.pc + (signed_immediate << 2U);
    uint32_t result = 0U;

    if (opcode == 0U) {
        switch (instruction & 63U) {
            case 0x00U: write_register(rd, machine.registers[rt] << shift); break;
            case 0x02U: write_register(rd, machine.registers[rt] >> shift); break;
            case 0x03U: write_register(rd, arithmetic_shift_right(machine.registers[rt], shift)); break;
            case 0x04U: write_register(rd, machine.registers[rt] << (machine.registers[rs] & 31U)); break;
            case 0x06U: write_register(rd, machine.registers[rt] >> (machine.registers[rs] & 31U)); break;
            case 0x07U: write_register(rd, arithmetic_shift_right(machine.registers[rt], machine.registers[rs] & 31U)); break;
            case 0x08U: branch(true, machine.registers[rs]); break;
            case 0x09U: write_register(rd ? rd : register_ra, current_pc + 8U); branch(true, machine.registers[rs]); break;
            case 0x0CU:
                if (machine.registers[register_v0] == 0U) { machine.exit_code = machine.registers[register_a0]; machine.stop = StopReason::exited; }
                else machine.stop = StopReason::invalid_instruction;
                break;
            case 0x0DU: machine.exit_code = machine.registers[register_a0]; machine.stop = StopReason::exited; break;
            case 0x10U: write_register(rd, machine.hi); break;
            case 0x11U: machine.hi = machine.registers[rs]; break;
            case 0x12U: write_register(rd, machine.lo); break;
            case 0x13U: machine.lo = machine.registers[rs]; break;
            case 0x18U: {
                const int64_t product = static_cast<int64_t>(static_cast<int32_t>(machine.registers[rs])) *
                    static_cast<int64_t>(static_cast<int32_t>(machine.registers[rt]));
                machine.lo = static_cast<uint32_t>(product); machine.hi = static_cast<uint32_t>(static_cast<uint64_t>(product) >> 32U); break;
            }
            case 0x19U: {
                const uint64_t product = static_cast<uint64_t>(machine.registers[rs]) * machine.registers[rt];
                machine.lo = static_cast<uint32_t>(product); machine.hi = static_cast<uint32_t>(product >> 32U); break;
            }
            case 0x1AU: {
                const int32_t numerator = static_cast<int32_t>(machine.registers[rs]);
                const int32_t denominator = static_cast<int32_t>(machine.registers[rt]);
                if (!denominator) { machine.lo = numerator >= 0 ? 0xFFFFFFFFU : 1U; machine.hi = static_cast<uint32_t>(numerator); }
                else if (static_cast<uint32_t>(numerator) == 0x80000000U && denominator == -1) { machine.lo = 0x80000000U; machine.hi = 0U; }
                else { machine.lo = static_cast<uint32_t>(numerator / denominator); machine.hi = static_cast<uint32_t>(numerator % denominator); }
                break;
            }
            case 0x1BU:
                if (!machine.registers[rt]) { machine.lo = 0xFFFFFFFFU; machine.hi = machine.registers[rs]; }
                else { machine.lo = machine.registers[rs] / machine.registers[rt]; machine.hi = machine.registers[rs] % machine.registers[rt]; }
                break;
            case 0x20U: if (add_checked(machine.registers[rs], machine.registers[rt], result)) write_register(rd, result); else machine.stop = StopReason::arithmetic_overflow; break;
            case 0x21U: write_register(rd, machine.registers[rs] + machine.registers[rt]); break;
            case 0x22U: if (sub_checked(machine.registers[rs], machine.registers[rt], result)) write_register(rd, result); else machine.stop = StopReason::arithmetic_overflow; break;
            case 0x23U: write_register(rd, machine.registers[rs] - machine.registers[rt]); break;
            case 0x24U: write_register(rd, machine.registers[rs] & machine.registers[rt]); break;
            case 0x25U: write_register(rd, machine.registers[rs] | machine.registers[rt]); break;
            case 0x26U: write_register(rd, machine.registers[rs] ^ machine.registers[rt]); break;
            case 0x27U: write_register(rd, ~(machine.registers[rs] | machine.registers[rt])); break;
            case 0x2AU: write_register(rd, signed_less(machine.registers[rs], machine.registers[rt]) ? 1U : 0U); break;
            case 0x2BU: write_register(rd, machine.registers[rs] < machine.registers[rt] ? 1U : 0U); break;
            default: machine.stop = StopReason::invalid_instruction; break;
        }
    } else {
        switch (opcode) {
            case 0x01U:
                if (rt == 0U || rt == 16U) { if (rt == 16U) write_register(register_ra, current_pc + 8U); branch(signed_less(machine.registers[rs], 0U), branch_target); }
                else if (rt == 1U || rt == 17U) { if (rt == 17U) write_register(register_ra, current_pc + 8U); branch(!signed_less(machine.registers[rs], 0U), branch_target); }
                else machine.stop = StopReason::invalid_instruction;
                break;
            case 0x02U: branch(true, (machine.pc & 0xF0000000U) | ((instruction & 0x03FFFFFFU) << 2U)); break;
            case 0x03U: write_register(register_ra, current_pc + 8U); branch(true, (machine.pc & 0xF0000000U) | ((instruction & 0x03FFFFFFU) << 2U)); break;
            case 0x04U: branch(machine.registers[rs] == machine.registers[rt], branch_target); break;
            case 0x05U: branch(machine.registers[rs] != machine.registers[rt], branch_target); break;
            case 0x06U: branch(machine.registers[rs] == 0U || signed_less(machine.registers[rs], 0U), branch_target); break;
            case 0x07U: branch(machine.registers[rs] != 0U && !signed_less(machine.registers[rs], 0U), branch_target); break;
            case 0x08U: if (add_checked(machine.registers[rs], signed_immediate, result)) write_register(rt, result); else machine.stop = StopReason::arithmetic_overflow; break;
            case 0x09U: write_register(rt, machine.registers[rs] + signed_immediate); break;
            case 0x0AU: write_register(rt, signed_less(machine.registers[rs], signed_immediate) ? 1U : 0U); break;
            case 0x0BU: write_register(rt, machine.registers[rs] < signed_immediate ? 1U : 0U); break;
            case 0x0CU: write_register(rt, machine.registers[rs] & immediate); break;
            case 0x0DU: write_register(rt, machine.registers[rs] | immediate); break;
            case 0x0EU: write_register(rt, machine.registers[rs] ^ immediate); break;
            case 0x0FU: write_register(rt, immediate << 16U); break;
            case 0x20U: { uint8_t value = 0U; if (read8(machine, machine.registers[rs] + signed_immediate, value)) delayed_register(rt, sign_extend8(value)); else if (machine.stop == StopReason::running) machine.stop = StopReason::memory_fault; break; }
            case 0x21U: { uint16_t value = 0U; if (read16(machine, machine.registers[rs] + signed_immediate, value)) delayed_register(rt, sign_extend16(value)); else if (machine.stop == StopReason::running) machine.stop = StopReason::memory_fault; break; }
            case 0x23U: { uint32_t value = 0U; if (read32(machine, machine.registers[rs] + signed_immediate, value)) delayed_register(rt, value); else if (machine.stop == StopReason::running) machine.stop = StopReason::memory_fault; break; }
            case 0x24U: { uint8_t value = 0U; if (read8(machine, machine.registers[rs] + signed_immediate, value)) delayed_register(rt, value); else if (machine.stop == StopReason::running) machine.stop = StopReason::memory_fault; break; }
            case 0x25U: { uint16_t value = 0U; if (read16(machine, machine.registers[rs] + signed_immediate, value)) delayed_register(rt, value); else if (machine.stop == StopReason::running) machine.stop = StopReason::memory_fault; break; }
            case 0x28U: if (!write8(machine, machine.registers[rs] + signed_immediate, static_cast<uint8_t>(machine.registers[rt])) && machine.stop == StopReason::running) machine.stop = StopReason::memory_fault; break;
            case 0x29U: if (!write16(machine, machine.registers[rs] + signed_immediate, static_cast<uint16_t>(machine.registers[rt])) && machine.stop == StopReason::running) machine.stop = StopReason::memory_fault; break;
            case 0x2BU: if (!write32(machine, machine.registers[rs] + signed_immediate, machine.registers[rt]) && machine.stop == StopReason::running) machine.stop = StopReason::memory_fault; break;
            default: machine.stop = StopReason::invalid_instruction; break;
        }
    }

    if (old_load_register && old_load_register != written_register) machine.registers[old_load_register] = old_load_value;
    machine.registers[0] = 0U;
    ++machine.steps;
    return machine.stop == StopReason::running;
}

StopReason run(Machine& machine, uint32_t instruction_budget) {
    while (machine.stop == StopReason::running && machine.steps < instruction_budget) step(machine);
    if (machine.stop == StopReason::running) machine.stop = StopReason::instruction_budget;
    return machine.stop;
}

} // namespace psx_r3000a
#endif
