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

void append_le16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void append_le32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value >> 16U));
    output.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void append_be32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value >> 24U));
    output.push_back(static_cast<std::uint8_t>(value >> 16U));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value));
}

constexpr std::uint8_t kWrappedPayload[] = "ZENOV_RANSOMWARE_TEST_V1";
constexpr std::uint8_t kRawDeflate[] = {
    0x8B,0x72,0xF5,0xF3,0x0F,0x8B,0x0F,0x72,0xF4,0x0B,0xF6,0xF7,0x0D,
    0x77,0x0C,0x72,0x8D,0x0F,0x71,0x0D,0x0E,0x89,0x0F,0x33,0x04,0x00
};
constexpr std::uint32_t kWrappedPayloadSize = sizeof(kWrappedPayload) - 1U;

std::vector<std::uint8_t> make_gzip(bool optional_fields) {
    const std::uint8_t flags = optional_fields ? 0x1EU : 0U;
    std::vector<std::uint8_t> output = {
        0x1FU, 0x8BU, 8U, flags, 0U, 0U, 0U, 0U, 0U, 0xFFU
    };
    if (optional_fields) {
        append_le16(output, 4U);
        output.insert(output.end(), {0x11U, 0x22U, 0x33U, 0x44U});
        static constexpr char name[] = "payload.bin";
        output.insert(output.end(), name, name + sizeof(name));
        static constexpr char comment[] = "safe-fixture";
        output.insert(output.end(), comment, comment + sizeof(comment));
        const std::uint32_t header_crc = zmid::content_inspection::crc32(
            output.data(), static_cast<std::uint32_t>(output.size()));
        append_le16(output, static_cast<std::uint16_t>(header_crc));
    }
    output.insert(output.end(), std::begin(kRawDeflate), std::end(kRawDeflate));
    append_le32(output, zmid::content_inspection::crc32(kWrappedPayload, kWrappedPayloadSize));
    append_le32(output, kWrappedPayloadSize);
    return output;
}

std::vector<std::uint8_t> make_zlib() {
    std::vector<std::uint8_t> output = {0x78U, 0x01U};
    output.insert(output.end(), std::begin(kRawDeflate), std::end(kRawDeflate));
    append_be32(output, zmid::content_inspection::adler32(kWrappedPayload, kWrappedPayloadSize));
    return output;
}

bool output_matches(const std::array<std::uint8_t, 64U * 1024U>& output,
                    std::uint32_t size,
                    std::uint32_t copies = 1U) {
    if (size != kWrappedPayloadSize * copies) return false;
    for (std::uint32_t copy = 0U; copy < copies; ++copy) {
        if (std::memcmp(output.data() + copy * kWrappedPayloadSize,
                        kWrappedPayload, kWrappedPayloadSize) != 0) return false;
    }
    return true;
}

int wrapped_deflate_property_test() {
    using Result = zmid::content_inspection::WrappedDeflateResult;
    std::array<std::uint8_t, 64U * 1024U> output{};
    std::uint32_t output_size = 0U;

    const auto basic = make_gzip(false);
    if (zmid::content_inspection::decode_gzip(
            basic.data(), static_cast<std::uint32_t>(basic.size()),
            output.data(), static_cast<std::uint32_t>(output.size()), output_size) != Result::decoded ||
        !output_matches(output, output_size)) return 20;

    auto optional = make_gzip(true);
    if (zmid::content_inspection::decode_gzip(
            optional.data(), static_cast<std::uint32_t>(optional.size()),
            output.data(), static_cast<std::uint32_t>(output.size()), output_size) != Result::decoded ||
        !output_matches(output, output_size)) return 21;

    std::vector<std::uint8_t> multi = basic;
    multi.insert(multi.end(), basic.begin(), basic.end());
    if (zmid::content_inspection::decode_gzip(
            multi.data(), static_cast<std::uint32_t>(multi.size()),
            output.data(), static_cast<std::uint32_t>(output.size()), output_size) != Result::decoded ||
        !output_matches(output, output_size, 2U)) return 22;

    std::vector<std::uint8_t> too_many;
    for (std::uint32_t index = 0U;
         index <= zmid::content_inspection::max_archive_entries; ++index) {
        too_many.insert(too_many.end(), basic.begin(), basic.end());
    }
    if (zmid::content_inspection::decode_gzip(
            too_many.data(), static_cast<std::uint32_t>(too_many.size()),
            output.data(), static_cast<std::uint32_t>(output.size()), output_size) != Result::unsupported) return 23;

    auto corrupt_crc = basic;
    corrupt_crc[corrupt_crc.size() - 8U] ^= 0x01U;
    if (zmid::content_inspection::decode_gzip(
            corrupt_crc.data(), static_cast<std::uint32_t>(corrupt_crc.size()),
            output.data(), static_cast<std::uint32_t>(output.size()), output_size) != Result::unsupported) return 24;

    const std::size_t optional_header_end = optional.size() - sizeof(kRawDeflate) - 8U;
    optional[optional_header_end - 2U] ^= 0x01U;
    if (zmid::content_inspection::decode_gzip(
            optional.data(), static_cast<std::uint32_t>(optional.size()),
            output.data(), static_cast<std::uint32_t>(output.size()), output_size) != Result::unsupported) return 25;

    auto trailing_gzip = basic;
    trailing_gzip.push_back(0U);
    if (zmid::content_inspection::decode_gzip(
            trailing_gzip.data(), static_cast<std::uint32_t>(trailing_gzip.size()),
            output.data(), static_cast<std::uint32_t>(output.size()), output_size) != Result::unsupported) return 26;

    auto reserved_flags = basic;
    reserved_flags[3] = 0x20U;
    if (zmid::content_inspection::decode_gzip(
            reserved_flags.data(), static_cast<std::uint32_t>(reserved_flags.size()),
            output.data(), static_cast<std::uint32_t>(output.size()), output_size) != Result::unsupported) return 27;

    const auto zlib = make_zlib();
    if (zmid::content_inspection::decode_zlib(
            zlib.data(), static_cast<std::uint32_t>(zlib.size()),
            output.data(), static_cast<std::uint32_t>(output.size()), output_size) != Result::decoded ||
        !output_matches(output, output_size)) return 28;

    auto corrupt_adler = zlib;
    corrupt_adler.back() ^= 0x01U;
    if (zmid::content_inspection::decode_zlib(
            corrupt_adler.data(), static_cast<std::uint32_t>(corrupt_adler.size()),
            output.data(), static_cast<std::uint32_t>(output.size()), output_size) != Result::unsupported) return 29;

    auto trailing_zlib = zlib;
    trailing_zlib.push_back(0U);
    if (zmid::content_inspection::decode_zlib(
            trailing_zlib.data(), static_cast<std::uint32_t>(trailing_zlib.size()),
            output.data(), static_cast<std::uint32_t>(output.size()), output_size) != Result::unsupported) return 30;

    std::array<std::uint8_t, 6U> preset_dictionary{{0x78U, 0x20U, 0U, 0U, 0U, 0U}};
    if (zmid::content_inspection::decode_zlib(
            preset_dictionary.data(), static_cast<std::uint32_t>(preset_dictionary.size()),
            output.data(), static_cast<std::uint32_t>(output.size()), output_size) != Result::unsupported) return 31;

    std::array<std::uint8_t, 6U> bad_fcheck{{0x78U, 0x02U, 0U, 0U, 0U, 0U}};
    if (zmid::content_inspection::decode_zlib(
            bad_fcheck.data(), static_cast<std::uint32_t>(bad_fcheck.size()),
            output.data(), static_cast<std::uint32_t>(output.size()), output_size) != Result::not_format) return 32;

    std::cout << "ZMID_WRAPPED_DEFLATE_PROPERTY_OK gzip=basic+optional+multi"
                 " integrity=crc32+isize+fhcrc zlib=header+adler"
                 " dictionary=blocked members=16 trailing=blocked\n";
    return 0;
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

    const int wrapped_status = wrapped_deflate_property_test();
    if (wrapped_status) return wrapped_status;
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
    (void)zmid::content_inspection::decode_gzip(
        data, static_cast<std::uint32_t>(size), scratch.data(),
        static_cast<std::uint32_t>(scratch.size()), decoded_size);
    (void)zmid::content_inspection::decode_zlib(
        data, static_cast<std::uint32_t>(size), scratch.data(),
        static_cast<std::uint32_t>(scratch.size()), decoded_size);
    std::uint32_t expanded_size = 0U;
    (void)zmid::content_inspection::inflate_raw(
        data, static_cast<std::uint32_t>(size), scratch.data(),
        static_cast<std::uint32_t>(scratch.size()), expanded_size);
    return 0;
}
#endif
