#include <cstdint>
#include <iostream>

using uint8_t = std::uint8_t;
using uint32_t = std::uint32_t;

#include "../kernel/parts/linux_i386_abi.inc"

int main() {
    using linux_i386_abi::Operation;
    const auto write_stdout = linux_i386_abi::translate(4U, 1U, 0x8048054U, 12U);
    const auto write_stderr = linux_i386_abi::translate(4U, 2U, 0x200U, 4U);
    const auto write_file = linux_i386_abi::translate(4U, 3U, 0x200U, 4U);
    const auto exit = linux_i386_abi::translate(1U, 7U, 0U, 0U);
    const auto exit_group = linux_i386_abi::translate(252U, 9U, 0U, 0U);
    const auto open = linux_i386_abi::translate(5U, 0U, 0U, 0U);

    if (write_stdout.operation != Operation::write || write_stdout.argument0 != 1U ||
        write_stdout.argument1 != 0x8048054U || write_stdout.argument2 != 12U ||
        write_stderr.operation != Operation::write || write_file.operation != Operation::invalid_descriptor ||
        exit.operation != Operation::exit || exit.argument0 != 7U ||
        exit_group.operation != Operation::exit || exit_group.argument0 != 9U ||
        open.operation != Operation::unsupported || open.argument0 != 5U ||
        linux_i386_abi::error_bad_file_descriptor != 0xFFFFFFF7U ||
        linux_i386_abi::error_fault != 0xFFFFFFF2U ||
        linux_i386_abi::error_not_implemented != 0xFFFFFFDAU) return 1;

    std::cout << "LINUX_I386_ABI_TEST_OK syscalls=write,exit,exit_group fail_closed=1\n";
    return 0;
}
