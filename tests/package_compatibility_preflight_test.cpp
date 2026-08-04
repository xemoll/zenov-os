#include <cstdint>
#include <cstdio>
#include <vector>

#include "../kernel/parts/package_foreign_format.inc"
#include "../kernel/parts/package_foreign_policy.inc"
#include "../kernel/parts/linux_i386_elf.inc"
#include "../kernel/parts/package_compatibility_preflight.inc"

using package_compatibility::Verdict;

static void put16(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value) {
    bytes[at] = static_cast<std::uint8_t>(value);
    bytes[at + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

static void put32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    bytes[at] = static_cast<std::uint8_t>(value);
    bytes[at + 1U] = static_cast<std::uint8_t>(value >> 8U);
    bytes[at + 2U] = static_cast<std::uint8_t>(value >> 16U);
    bytes[at + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

static std::vector<std::uint8_t> pe32_fixture() {
    std::vector<std::uint8_t> bytes(0x400U, 0U);
    bytes[0] = 'M'; bytes[1] = 'Z'; put32(bytes, 0x3cU, 0x80U);
    bytes[0x80U] = 'P'; bytes[0x81U] = 'E';
    put16(bytes, 0x84U, 0x014cU); put16(bytes, 0x86U, 1U);
    put16(bytes, 0x94U, 0x00e0U); put16(bytes, 0x96U, 0x0102U);
    const std::size_t optional = 0x98U;
    put16(bytes, optional, 0x010bU); put32(bytes, optional + 16U, 0x1000U);
    put32(bytes, optional + 32U, 0x1000U); put32(bytes, optional + 36U, 0x200U);
    put32(bytes, optional + 56U, 0x2000U); put32(bytes, optional + 60U, 0x200U);
    const std::size_t section = 0x178U;
    bytes[section] = '.'; bytes[section + 1U] = 't'; bytes[section + 2U] = 'e';
    bytes[section + 3U] = 'x'; bytes[section + 4U] = 't';
    put32(bytes, section + 8U, 0x100U); put32(bytes, section + 12U, 0x1000U);
    put32(bytes, section + 16U, 0x200U); put32(bytes, section + 20U, 0x200U);
    put32(bytes, section + 36U, 0x60000020U);
    return bytes;
}

static std::vector<std::uint8_t> psx_fixture() {
    std::vector<std::uint8_t> bytes(0x804U, 0U);
    const char magic[8] = {'P','S','-','X',' ','E','X','E'};
    for (std::size_t i = 0; i < 8U; ++i) bytes[i] = static_cast<std::uint8_t>(magic[i]);
    put32(bytes, 0x10U, 0x80010000U); put32(bytes, 0x18U, 0x80010000U);
    put32(bytes, 0x1cU, 4U); bytes[0x800U] = 0x08U;
    return bytes;
}

static std::vector<std::uint8_t> ps2_fixture() {
    std::vector<std::uint8_t> bytes(0x200U, 0U);
    bytes[0] = 0x7fU; bytes[1] = 'E'; bytes[2] = 'L'; bytes[3] = 'F';
    bytes[4] = 1U; bytes[5] = 1U; bytes[6] = 1U;
    put16(bytes, 16U, 2U); put16(bytes, 18U, 8U); put32(bytes, 20U, 1U);
    put32(bytes, 24U, 0x00100000U); put32(bytes, 28U, 52U);
    put32(bytes, 36U, 0x20924001U); put16(bytes, 40U, 52U);
    put16(bytes, 42U, 32U); put16(bytes, 44U, 1U);
    put32(bytes, 52U, 1U); put32(bytes, 56U, 0x100U);
    put32(bytes, 60U, 0x00100000U); put32(bytes, 68U, 4U); put32(bytes, 72U, 4U);
    put32(bytes, 76U, 5U); put32(bytes, 80U, 0x100U);
    return bytes;
}

static std::vector<std::uint8_t> xbe_fixture() {
    std::vector<std::uint8_t> bytes(0x400U, 0U);
    bytes[0] = 'X'; bytes[1] = 'B'; bytes[2] = 'E'; bytes[3] = 'H';
    constexpr std::uint32_t base = 0x00010000U;
    put32(bytes, 0x104U, base); put32(bytes, 0x108U, 0x200U);
    put32(bytes, 0x10cU, 0x3000U); put32(bytes, 0x110U, 0x178U);
    put32(bytes, 0x118U, base + 0x180U); put32(bytes, 0x11cU, 1U);
    put32(bytes, 0x120U, base + 0x1a0U);
    put32(bytes, 0x1a0U, 0x6U); put32(bytes, 0x1a4U, base + 0x1000U);
    put32(bytes, 0x1a8U, 0x100U); put32(bytes, 0x1acU, 0x200U);
    put32(bytes, 0x1b0U, 0x200U);
    return bytes;
}

static std::vector<std::uint8_t> linux_fixture() {
    std::vector<std::uint8_t> bytes(0x100U, 0U);
    bytes[0] = 0x7fU; bytes[1] = 'E'; bytes[2] = 'L'; bytes[3] = 'F';
    bytes[4] = 1U; bytes[5] = 1U; bytes[6] = 1U;
    put16(bytes, 16U, 2U); put16(bytes, 18U, 3U); put32(bytes, 20U, 1U);
    put32(bytes, 24U, 0x08048000U); put32(bytes, 28U, 52U);
    put16(bytes, 40U, 52U); put16(bytes, 42U, 32U); put16(bytes, 44U, 1U);
    put32(bytes, 52U, 1U); put32(bytes, 56U, 0x80U);
    put32(bytes, 60U, 0x08048000U); put32(bytes, 68U, 4U); put32(bytes, 72U, 4U);
    put32(bytes, 76U, 5U); put32(bytes, 80U, 1U);
    return bytes;
}

static bool expect(const char* label, const std::vector<std::uint8_t>& bytes, const char* path,
                   bool structural, bool runtime, Verdict verdict) {
    const auto result = package_compatibility::check(
        bytes.data(), static_cast<std::uint32_t>(bytes.size()), path);
    if (result.structurally_valid == structural && result.runtime_ready == runtime &&
        result.verdict == verdict && !result.trust_verified) return true;
    std::fprintf(stderr, "case=%s structural=%u runtime=%u verdict=%u format=%u\n", label,
                 result.structurally_valid ? 1U : 0U, result.runtime_ready ? 1U : 0U,
                 static_cast<unsigned>(result.verdict), static_cast<unsigned>(result.format));
    return false;
}

int main() {
    unsigned cases = 0U;
    unsigned truncations = 0U;
    bool okay = true;
    auto check = [&](const char* label, const std::vector<std::uint8_t>& bytes, const char* path,
                     bool structural, bool runtime = false,
                     Verdict verdict = Verdict::blocked) {
        ++cases;
        okay &= expect(label, bytes, path, structural, runtime, verdict);
    };
    auto reject_truncated_prefixes = [&](const char* label, const std::vector<std::uint8_t>& bytes,
                                         const char* path, std::size_t required) {
        for (std::size_t size = 0U; size < required; ++size) {
            ++truncations;
            const auto result = package_compatibility::check(
                bytes.data(), static_cast<std::uint32_t>(size), path);
            if (result.structurally_valid) {
                std::fprintf(stderr, "case=%s accepted-truncation=%zu\n", label, size);
                okay = false;
                return;
            }
        }
    };

    check("pe-valid", pe32_fixture(), "game.exe", true, false, Verdict::inspect_only);
    auto pe = pe32_fixture(); put16(pe, 0x84U, 0x8664U); check("pe-amd64", pe, "game.exe", false);
    pe = pe32_fixture(); put16(pe, 0x96U, 0x2102U); check("pe-dll", pe, "game.exe", false);
    pe = pe32_fixture(); put32(pe, 0x178U + 36U, 0xe0000020U); check("pe-wx", pe, "game.exe", false);
    pe = pe32_fixture(); put32(pe, 0x98U + 16U, 0x1800U); check("pe-entry", pe, "game.exe", false);
    pe = pe32_fixture(); put32(pe, 0x178U + 20U, 0x300U); check("pe-raw-align", pe, "game.exe", false);
    pe = pe32_fixture(); pe.resize(0x300U); check("pe-truncated", pe, "game.exe", false);

    check("psx-valid", psx_fixture(), "GAME.EXE", true, false, Verdict::inspect_only);
    auto psx = psx_fixture(); put32(psx, 0x10U, 0x80020000U); check("psx-entry", psx, "GAME.EXE", false);
    psx = psx_fixture(); put32(psx, 0x1cU, 0x200000U); check("psx-payload", psx, "GAME.EXE", false);
    psx = psx_fixture(); put32(psx, 0x18U, 0x80200000U); check("psx-load", psx, "GAME.EXE", false);
    psx = psx_fixture(); put32(psx, 0x28U, 0x80010000U); put32(psx, 0x2cU, 4U);
    check("psx-fill-overlap", psx, "GAME.EXE", false);
    psx = psx_fixture(); psx.resize(0x700U); check("psx-truncated", psx, "GAME.EXE", false);

    check("ps2-valid", ps2_fixture(), "game.elf", true, false, Verdict::inspect_only);
    auto ps2 = ps2_fixture(); put32(ps2, 36U, 0U); check("ps2-generic-mips", ps2, "game.elf", false);
    ps2 = ps2_fixture(); put32(ps2, 76U, 7U); check("ps2-wx", ps2, "game.elf", false);
    ps2 = ps2_fixture(); put32(ps2, 52U, 3U); check("ps2-interp", ps2, "game.elf", false);
    ps2 = ps2_fixture(); put32(ps2, 24U, 0x00200000U); check("ps2-entry", ps2, "game.elf", false);
    ps2 = ps2_fixture(); put32(ps2, 72U, 0x03000000U); check("ps2-memory", ps2, "game.elf", false);
    ps2 = ps2_fixture(); ps2.resize(60U); check("ps2-truncated", ps2, "game.elf", false);

    check("xbe-valid", xbe_fixture(), "default.xbe", true, false, Verdict::inspect_only);
    auto xbe = xbe_fixture(); put32(xbe, 0x120U, 0x00020000U); check("xbe-table", xbe, "default.xbe", false);
    xbe = xbe_fixture(); put32(xbe, 0x1a0U, 0x5U); check("xbe-wx", xbe, "default.xbe", false);
    xbe = xbe_fixture(); put32(xbe, 0x1acU, 0x380U); check("xbe-raw", xbe, "default.xbe", false);
    xbe = xbe_fixture(); put32(xbe, 0x1a8U, 0x4000U); check("xbe-virtual", xbe, "default.xbe", false);
    xbe = xbe_fixture(); xbe.resize(0x170U); check("xbe-truncated", xbe, "default.xbe", false);

    check("linux-valid", linux_fixture(), "hello.elf", true, true, Verdict::runnable_sandbox);
    auto linux = linux_fixture(); put32(linux, 52U, 3U); check("linux-interp", linux, "hello.elf", false);
    linux = linux_fixture(); put32(linux, 76U, 7U); check("linux-wx", linux, "hello.elf", false);
    linux = linux_fixture(); put32(linux, 24U, 0x08049000U); check("linux-entry", linux, "hello.elf", false);
    linux = linux_fixture(); put32(linux, 72U, 0x00200000U); check("linux-window", linux, "hello.elf", false);
    linux = linux_fixture(); linux.resize(60U); check("linux-truncated", linux, "hello.elf", false);

    check("unknown", {0U, 1U, 2U, 3U}, "blob.bin", false);
    reject_truncated_prefixes("pe-prefixes", pe32_fixture(), "game.exe", 0x400U);
    reject_truncated_prefixes("psx-prefixes", psx_fixture(), "GAME.EXE", 0x804U);
    reject_truncated_prefixes("ps2-prefixes", ps2_fixture(), "game.elf", 0x104U);
    reject_truncated_prefixes("xbe-prefixes", xbe_fixture(), "default.xbe", 0x400U);
    reject_truncated_prefixes("linux-prefixes", linux_fixture(), "hello.elf", 0x84U);
    if (!okay) return 1;
    std::printf("PACKAGE_COMPATIBILITY_PREFLIGHT_TEST_OK cases=%u truncations=%u validators=5 runtime-ready=1 fail-closed=1\n",
                cases, truncations);
    return 0;
}
