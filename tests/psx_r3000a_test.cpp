#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "../kernel/parts/psx_exe.inc"
#include "../kernel/parts/psx_r3000a.inc"

namespace {
constexpr std::uint32_t base = 0x80010000U;

void put32(std::vector<std::uint8_t>& ram, std::uint32_t address, std::uint32_t value) {
    const std::size_t at = address & 0x1FFFFFFFU;
    ram.at(at) = static_cast<std::uint8_t>(value);
    ram.at(at + 1U) = static_cast<std::uint8_t>(value >> 8U);
    ram.at(at + 2U) = static_cast<std::uint8_t>(value >> 16U);
    ram.at(at + 3U) = static_cast<std::uint8_t>(value >> 24U);
}

void append(void* context, char value) { static_cast<std::string*>(context)->push_back(value); }

bool expect(bool condition, const char* label) {
    if (condition) return true;
    std::fprintf(stderr, "psx-r3000a-test: failed: %s\n", label);
    return false;
}

void reset(std::vector<std::uint8_t>& ram, psx_r3000a::Machine& machine, std::string& output) {
    std::memset(ram.data(), 0, ram.size());
    output.clear();
    psx_r3000a::initialize(machine, ram.data(), static_cast<std::uint32_t>(ram.size()),
                           base, 0x801FFF00U, 0U, append, &output);
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: psx-r3000a-test <fixture.exe>\n");
        return 2;
    }
    bool okay = true;
    unsigned cases = 0U;
    std::vector<std::uint8_t> ram(psx_exe::ram_bytes, 0U);
    psx_r3000a::Machine machine{};
    std::string output;

    std::ifstream input(argv[1], std::ios::binary);
    const std::vector<std::uint8_t> exe((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    psx_exe::Layout layout{};
    ++cases; okay &= expect(psx_exe::validate(exe.data(), static_cast<std::uint32_t>(exe.size()), layout), "fixture-loader");
    std::uint32_t load_offset = 0U;
    okay &= expect(psx_exe::address_offset(layout.load_address, load_offset), "fixture-load-address");
    std::memcpy(ram.data() + load_offset, exe.data() + psx_exe::header_bytes, layout.payload_bytes);
    psx_r3000a::initialize(machine, ram.data(), static_cast<std::uint32_t>(ram.size()), layout.entry,
                           layout.stack_pointer, layout.global_pointer, append, &output);
    const auto fixture_stop = psx_r3000a::run(machine, 1000U);
    ++cases; okay &= expect(fixture_stop == psx_r3000a::StopReason::exited && machine.exit_code == 0U &&
                            output == "PSX_R3000A_HLE_PUTS_OK\n" && machine.steps == 8U, "fixture-execution");
    if (fixture_stop != psx_r3000a::StopReason::exited || output != "PSX_R3000A_HLE_PUTS_OK\n")
        std::fprintf(stderr, "fixture stop=%s steps=%u output=%s\n", psx_r3000a::stop_name(fixture_stop), machine.steps, output.c_str());

    reset(ram, machine, output);
    const std::uint32_t branch_program[] = {
        0x24080001U, 0x11080002U, 0x24100007U, 0x24100009U, 0x0000000DU
    };
    for (std::size_t i = 0U; i < 5U; ++i) put32(ram, base + static_cast<std::uint32_t>(i * 4U), branch_program[i]);
    ++cases; okay &= expect(psx_r3000a::run(machine, 20U) == psx_r3000a::StopReason::exited && machine.registers[16] == 7U,
                            "branch-delay-slot");

    reset(ram, machine, output);
    machine.registers[8] = base + 0x200U;
    put32(ram, base + 0x200U, 0x12345678U);
    const std::uint32_t load_program[] = {0x8D090000U, 0x01205021U, 0x01205821U, 0x0000000DU};
    for (std::size_t i = 0U; i < 4U; ++i) put32(ram, base + static_cast<std::uint32_t>(i * 4U), load_program[i]);
    ++cases; okay &= expect(psx_r3000a::run(machine, 20U) == psx_r3000a::StopReason::exited &&
                            machine.registers[10] == 0U && machine.registers[11] == 0x12345678U,
                            "load-delay-slot");

    reset(ram, machine, output);
    machine.registers[8] = 0xFFFFFFFFU; machine.registers[9] = 2U;
    put32(ram, base, 0x01090019U); put32(ram, base + 4U, 0x00008012U);
    put32(ram, base + 8U, 0x00008810U); put32(ram, base + 12U, 0x0000000DU);
    ++cases; okay &= expect(psx_r3000a::run(machine, 10U) == psx_r3000a::StopReason::exited &&
                            machine.registers[16] == 0xFFFFFFFEU && machine.registers[17] == 1U,
                            "unsigned-multiply-hi-lo");

    reset(ram, machine, output);
    machine.registers[8] = 0x7FFFFFFFU;
    put32(ram, base, 0x21080001U);
    ++cases; okay &= expect(psx_r3000a::run(machine, 2U) == psx_r3000a::StopReason::arithmetic_overflow, "signed-overflow");

    reset(ram, machine, output);
    put32(ram, base, 0x40000000U);
    ++cases; okay &= expect(psx_r3000a::run(machine, 2U) == psx_r3000a::StopReason::invalid_instruction, "coprocessor-denied");

    reset(ram, machine, output);
    machine.registers[8] = base;
    put32(ram, base, 0x8D090001U);
    ++cases; okay &= expect(psx_r3000a::run(machine, 2U) == psx_r3000a::StopReason::alignment_fault, "unaligned-load");

    reset(ram, machine, output);
    machine.registers[8] = 0x80200000U;
    put32(ram, base, 0x8D090000U);
    ++cases; okay &= expect(psx_r3000a::run(machine, 2U) == psx_r3000a::StopReason::memory_fault, "out-of-ram");

    reset(ram, machine, output);
    machine.pc = 0xA0U; machine.next_pc = 0xA4U; machine.registers[9] = 1U;
    ++cases; okay &= expect(psx_r3000a::run(machine, 2U) == psx_r3000a::StopReason::unsupported_bios_call, "unsupported-hle");

    reset(ram, machine, output);
    machine.pc = 0xB0U; machine.next_pc = 0xB4U; machine.registers[9] = 0x3DU;
    machine.registers[4] = 'B'; machine.registers[31] = base; put32(ram, base, 0x0000000DU);
    ++cases; okay &= expect(psx_r3000a::run(machine, 2U) == psx_r3000a::StopReason::exited && output == "B", "b0-putchar-hle");

    reset(ram, machine, output);
    machine.pc = 0xE00000A0U; machine.next_pc = 0xE00000A4U; machine.registers[9] = 0x3CU;
    ++cases; okay &= expect(psx_r3000a::run(machine, 2U) == psx_r3000a::StopReason::memory_fault && output.empty(),
                            "invalid-hle-alias");

    reset(ram, machine, output);
    machine.pc = 0xA0U; machine.next_pc = 0xA4U; machine.registers[9] = 0x3CU;
    machine.registers[4] = 'X'; machine.output_limit = 0U;
    ++cases; okay &= expect(psx_r3000a::run(machine, 2U) == psx_r3000a::StopReason::output_budget, "output-budget");

    reset(ram, machine, output);
    put32(ram, base, 0U); put32(ram, base + 4U, 0U);
    ++cases; okay &= expect(psx_r3000a::run(machine, 2U) == psx_r3000a::StopReason::instruction_budget, "instruction-budget");

    reset(ram, machine, output);
    machine.registers[1] = 0xFFFFFFFFU;
    put32(ram, base, 0x24200001U); put32(ram, base + 4U, 0x0000000DU);
    ++cases; okay &= expect(psx_r3000a::run(machine, 4U) == psx_r3000a::StopReason::exited && machine.registers[0] == 0U,
                            "zero-register-immutable");

    if (!okay) return 1;
    std::printf("PSX_R3000A_TEST_OK cases=%u cpu=integer+branch-delay+load-delay+hi-lo memory=2MiB hle=A0+B0 fail-closed=1\n", cases);
    return 0;
}
