namespace linux_i386_abi {
constexpr uint32_t syscall_exit = 1U;
constexpr uint32_t syscall_write = 4U;
constexpr uint32_t syscall_exit_group = 252U;

constexpr uint32_t negative_errno(uint32_t value) { return 0U - value; }
constexpr uint32_t error_bad_file_descriptor = negative_errno(9U);
constexpr uint32_t error_fault = negative_errno(14U);
constexpr uint32_t error_not_implemented = negative_errno(38U);

enum class Operation : uint8_t { exit, write, invalid_descriptor, unsupported };

struct Translation {
    Operation operation;
    uint32_t argument0;
    uint32_t argument1;
    uint32_t argument2;
};

constexpr Translation translate(uint32_t number, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    if (number == syscall_exit || number == syscall_exit_group)
        return {Operation::exit, ebx, 0U, 0U};
    if (number == syscall_write) {
        if (ebx != 1U && ebx != 2U)
            return {Operation::invalid_descriptor, ebx, ecx, edx};
        return {Operation::write, ebx, ecx, edx};
    }
    return {Operation::unsupported, number, 0U, 0U};
}
} // namespace linux_i386_abi
