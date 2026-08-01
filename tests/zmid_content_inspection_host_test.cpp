#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace security_guard {
constexpr std::uint32_t sha256_bytes = 32U;
enum class Verdict : std::uint8_t { clean, trusted, untrusted, suspicious, infected, error };
struct ScanResult {
    Verdict verdict = Verdict::error;
    char path[48]{};
    char signature[48]{};
    std::uint8_t digest[sha256_bytes]{};
    std::uint32_t size = 0U;
    std::uint8_t policy_action = 0U;
    bool executable = false;
};
void sha256(const std::uint8_t* data, std::uint32_t size, std::uint8_t output[sha256_bytes]) {
    std::uint8_t accumulator = 0x5AU;
    for (std::uint32_t i = 0U; i < size; ++i) {
        accumulator = static_cast<std::uint8_t>(accumulator ^ data[i] ^ static_cast<std::uint8_t>(i));
    }
    for (std::uint32_t i = 0U; i < sha256_bytes; ++i) {
        output[i] = static_cast<std::uint8_t>(accumulator ^ static_cast<std::uint8_t>(i * 17U));
    }
}
bool digest_equal(const std::uint8_t* left, const std::uint8_t* right) {
    return std::memcmp(left, right, sha256_bytes) == 0;
}
} // namespace security_guard

namespace storage {
bool bytes_equal(const std::uint8_t* left, const std::uint8_t* right, std::uint32_t size) {
    return std::memcmp(left, right, size) == 0;
}
void path_copy(char* output, const char* input, std::uint32_t capacity) {
    if (!output || !capacity) return;
    std::uint32_t used = 0U;
    if (input) while (input[used] && used + 1U < capacity) { output[used] = input[used]; ++used; }
    output[used] = 0;
}
} // namespace storage

namespace heap {
alignas(4096) std::array<std::uint8_t, 64U * 1024U> workspace_a{};
alignas(4096) std::array<std::uint8_t, 64U * 1024U> workspace_b{};
std::uint32_t allocations = 0U;
void* allocate(std::uint32_t bytes, std::uint32_t alignment) {
    if (bytes != workspace_a.size() || !alignment || allocations >= 2U) return nullptr;
    return allocations++ == 0U ? static_cast<void*>(workspace_a.data()) : static_cast<void*>(workspace_b.data());
}
} // namespace heap

namespace serial { void line(const char*) {} }
namespace process { void serial_u32(std::uint32_t) {} }

namespace zmid {
constexpr std::uint8_t action_block = 1U;
constexpr std::uint8_t action_quarantine = 2U;
std::uint32_t active_database_version = 1U;
bool ready = true;

bool contains(const std::uint8_t* data, std::uint32_t size, const char* needle) {
    const auto needle_size = static_cast<std::uint32_t>(std::strlen(needle));
    if (!data || needle_size > size) return false;
    for (std::uint32_t offset = 0U; offset <= size - needle_size; ++offset) {
        if (std::memcmp(data + offset, needle, needle_size) == 0) return true;
    }
    return false;
}

bool classify_raw(const char* path,
                  const std::uint8_t* data,
                  std::uint32_t size,
                  const std::uint8_t[security_guard::sha256_bytes],
                  security_guard::ScanResult& result) {
    const char* signature = nullptr;
    if (contains(data, size, "EICAR-STANDARD-ANTIVIRUS-TEST-FILE")) signature = "Eicar.Pattern";
    else if (contains(data, size, "ZENOV_RANSOMWARE_TEST_V1")) signature = "Pattern.Ransomware.Test";
    if (!signature) return false;
    result.verdict = security_guard::Verdict::infected;
    result.policy_action = action_block | action_quarantine;
    storage::path_copy(result.signature, signature, sizeof(result.signature));
    storage::path_copy(result.path, path, sizeof(result.path));
    return true;
}
} // namespace zmid

#include "../kernel/parts/zmid_content_inspection.inc"

#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
namespace {
std::string encode_base64(const std::vector<std::uint8_t>& data, bool url_safe, bool padding) {
    static constexpr char standard[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static constexpr char url[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    const char* alphabet = url_safe ? url : standard;
    std::string output;
    for (std::size_t offset = 0U; offset < data.size(); offset += 3U) {
        const std::uint32_t a = data[offset];
        const bool have_b = offset + 1U < data.size();
        const bool have_c = offset + 2U < data.size();
        const std::uint32_t b = have_b ? data[offset + 1U] : 0U;
        const std::uint32_t c = have_c ? data[offset + 2U] : 0U;
        output.push_back(alphabet[a >> 2U]);
        output.push_back(alphabet[((a & 3U) << 4U) | (b >> 4U)]);
        if (have_b) output.push_back(alphabet[((b & 15U) << 2U) | (c >> 6U)]);
        else if (padding) output.push_back('=');
        if (have_c) output.push_back(alphabet[c & 63U]);
        else if (padding) output.push_back('=');
    }
    return output;
}

std::vector<std::uint8_t> read_all(const char* path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error(std::string("cannot open: ") + path);
    const std::streamoff length = input.tellg();
    if (length < 0 || length > 128 * 1024) throw std::runtime_error(std::string("invalid size: ") + path);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) throw std::runtime_error(std::string("cannot read: ") + path);
    return bytes;
}

int property_test() {
    std::uint64_t state = 0xC001D00DULL;
    std::array<std::uint8_t, 64U * 1024U> decoded{};
    std::uint64_t cases = 0U;
    for (std::uint32_t length = 12U; length <= 4096U; ++length) {
        std::vector<std::uint8_t> input(length);
        for (auto& byte : input) {
            state = (state * 1664525ULL + 1013904223ULL) & 0xFFFFFFFFULL;
            byte = static_cast<std::uint8_t>(state >> 24U);
        }
        for (const bool url_safe : {false, true}) {
            for (const bool padding : {false, true}) {
                const std::string encoded = encode_base64(input, url_safe, padding);
                std::uint32_t decoded_size = 0U;
                if (!zmid::content_inspection::decode_base64(
                        reinterpret_cast<const std::uint8_t*>(encoded.data()),
                        static_cast<std::uint32_t>(encoded.size()), decoded.data(),
                        static_cast<std::uint32_t>(decoded.size()), decoded_size) ||
                    decoded_size != input.size() || std::memcmp(decoded.data(), input.data(), input.size()) != 0) {
                    return 10;
                }
                ++cases;
            }
        }
    }
    static constexpr const char* invalid[] = {
        "A", "AB==", "AAB=", "Zm9v=", "Zm=9", "Zm9v===", "Zm9v!", "!!!!!!!!!!!!!!!!!"
    };
    for (const char* encoded : invalid) {
        std::uint32_t decoded_size = 0U;
        if (zmid::content_inspection::decode_base64(
                reinterpret_cast<const std::uint8_t*>(encoded),
                static_cast<std::uint32_t>(std::strlen(encoded)), decoded.data(),
                static_cast<std::uint32_t>(decoded.size()), decoded_size)) return 11;
    }
    if (!zmid::content_inspection::self_test()) return 12;

    static constexpr std::uint8_t path_sensitive[] = "not a zip archive";
    std::uint8_t digest[security_guard::sha256_bytes]{};
    security_guard::sha256(path_sensitive, sizeof(path_sensitive) - 1U, digest);
    security_guard::ScanResult result{};
    zmid::content_inspection::clear_cache();
    if (zmid::classify("/cache/plain.txt", path_sensitive, sizeof(path_sensitive) - 1U, digest, result)) return 13;
    result = {};
    if (!zmid::classify("/cache/plain.zip", path_sensitive, sizeof(path_sensitive) - 1U, digest, result) ||
        result.verdict != security_guard::Verdict::suspicious ||
        std::strcmp(result.signature, "Container.Unsupported") != 0) return 14;
    const std::uint32_t cache_before = zmid::content_inspection::cache_hits;
    result = {};
    if (!zmid::classify("/cache/plain.zip", path_sensitive, sizeof(path_sensitive) - 1U, digest, result) ||
        zmid::content_inspection::cache_hits != cache_before + 1U) return 15;

    zmid::content_inspection::clear_cache();
    result = {};
    if (!zmid::classify("/cache/plain.zip", path_sensitive, sizeof(path_sensitive) - 1U, digest, result)) return 16;
    result = {};
    if (zmid::classify("/cache/plain.txt", path_sensitive, sizeof(path_sensitive) - 1U, digest, result)) return 17;

    std::cout << "ZMID_BASE64_PROPERTY_OK cases=" << cases
              << " invalid=" << std::size(invalid)
              << " selftest=1 cache-context=2\n";
    return 0;
}
} // namespace

const char* inspection_name(zmid::content_inspection::InspectionResult result) {
    using InspectionResult = zmid::content_inspection::InspectionResult;
    if (result == InspectionResult::clean) return "clean";
    if (result == InspectionResult::matched) return "matched";
    return "unsupported";
}

int main(int argc, char** argv) {
    try {
        const int property_status = property_test();
        if (property_status) return property_status;
        std::uint32_t clean = 0U, matched = 0U, unsupported = 0U;
        for (int index = 1; index < argc; ++index) {
            const auto input = read_all(argv[index]);
            std::array<std::uint8_t, 64U * 1024U> scratch{};
            std::array<std::uint8_t, 64U * 1024U> alternate{};
            security_guard::ScanResult result{};
            const auto verdict = zmid::content_inspection::inspect_payload(
                argv[index], input.data(), static_cast<std::uint32_t>(input.size()), 0U,
                scratch.data(), alternate.data(), result);
            std::cout << "ZMID_HOST_CASE path=" << argv[index]
                      << " result=" << inspection_name(verdict)
                      << " signature=" << result.signature << "\n";
            if (verdict == zmid::content_inspection::InspectionResult::clean) ++clean;
            else if (verdict == zmid::content_inspection::InspectionResult::matched) ++matched;
            else ++unsupported;
        }
        std::cout << "ZMID_HOST_CORPUS_OK files=" << (argc - 1)
                  << " clean=" << clean << " matched=" << matched
                  << " unsupported=" << unsupported << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zmid-content-host-test: " << error.what() << "\n";
        return 1;
    }
}
#else
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > 128U * 1024U) return 0;
    std::array<std::uint8_t, 64U * 1024U> scratch{};
    std::array<std::uint8_t, 64U * 1024U> alternate{};
    security_guard::ScanResult result{};
    (void)zmid::content_inspection::inspect_payload(
        "<fuzz>", data, static_cast<std::uint32_t>(size), 0U,
        scratch.data(), alternate.data(), result);
    std::uint32_t decoded_size = 0U;
    (void)zmid::content_inspection::decode_base64(
        data, static_cast<std::uint32_t>(size), scratch.data(),
        static_cast<std::uint32_t>(scratch.size()), decoded_size);
    std::uint32_t expanded_size = 0U;
    (void)zmid::content_inspection::inflate_raw(
        data, static_cast<std::uint32_t>(size), scratch.data(),
        static_cast<std::uint32_t>(scratch.size()), expanded_size);
    return 0;
}
#endif
