#include <algorithm>

#define FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
#include "zmid_content_inspection_host_test.cpp"
#undef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION

namespace {
constexpr std::uint8_t kPayload[] = "ZENOV_RANSOMWARE_TEST_V1";
constexpr std::uint8_t kRawDeflate[] = {
    0x8B,0x72,0xF5,0xF3,0x0F,0x8B,0x0F,0x72,0xF4,0x0B,0xF6,0xF7,0x0D,
    0x77,0x0C,0x72,0x8D,0x0F,0x71,0x0D,0x0E,0x89,0x0F,0x33,0x04,0x00
};
constexpr std::uint32_t kPayloadSize = sizeof(kPayload) - 1U;

std::uint8_t valid_fcheck(std::uint8_t cmf) {
    const std::uint32_t base = static_cast<std::uint32_t>(cmf) * 256U;
    return static_cast<std::uint8_t>((31U - (base % 31U)) % 31U);
}

void append_be32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value >> 24U));
    output.push_back(static_cast<std::uint8_t>(value >> 16U));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value));
}

std::vector<std::uint8_t> make_stream(std::uint8_t cinfo) {
    const std::uint8_t cmf = static_cast<std::uint8_t>((cinfo << 4U) | 8U);
    const std::uint8_t flg = valid_fcheck(cmf);
    std::vector<std::uint8_t> output{cmf, flg};
    output.insert(output.end(), std::begin(kRawDeflate), std::end(kRawDeflate));
    append_be32(output, zmid::content_inspection::adler32(kPayload, kPayloadSize));
    return output;
}

std::vector<std::uint8_t> read_all(const char* path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error(std::string("cannot open: ") + path);
    const std::streamoff length = input.tellg();
    if (length < 6 || length > 128 * 1024) {
        throw std::runtime_error(std::string("invalid size: ") + path);
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error(std::string("cannot read: ") + path);
    return bytes;
}

int run(const char* undersized_window_path, const char* full_window_path) {
    using InspectionResult = zmid::content_inspection::InspectionResult;
    using WrappedResult = zmid::content_inspection::WrappedDeflateResult;
    std::array<std::uint8_t, 64U * 1024U> decoded{};
    std::array<std::uint8_t, 64U * 1024U> alternate{};

    for (std::uint8_t cinfo = 0U; cinfo <= 7U; ++cinfo) {
        const auto stream = make_stream(cinfo);
        if (!zmid::content_inspection::zlib_header_valid(
                stream.data(), static_cast<std::uint32_t>(stream.size())) ||
            !zmid::content_inspection::looks_like_zlib_stream(
                stream.data(), static_cast<std::uint32_t>(stream.size())) ||
            zmid::content_inspection::zlib_window_limit(stream.data()) !=
                (1U << (static_cast<std::uint32_t>(cinfo) + 8U))) {
            return 10 + cinfo;
        }

        std::uint32_t decoded_size = 0U;
        if (zmid::content_inspection::decode_zlib(
                stream.data(), static_cast<std::uint32_t>(stream.size()),
                decoded.data(), static_cast<std::uint32_t>(decoded.size()),
                decoded_size) != WrappedResult::decoded ||
            decoded_size != kPayloadSize ||
            std::memcmp(decoded.data(), kPayload, kPayloadSize) != 0) {
            return 20 + cinfo;
        }

        security_guard::ScanResult result{};
        decoded.fill(0U);
        alternate.fill(0U);
        if (zmid::content_inspection::inspect_payload(
                "<zlib-cinfo-no-extension>", stream.data(),
                static_cast<std::uint32_t>(stream.size()), 0U,
                decoded.data(), alternate.data(), result) != InspectionResult::matched ||
            result.verdict != security_guard::Verdict::infected ||
            std::strcmp(result.signature, "Zlib:Eicar.Pattern") != 0) {
            return 30 + cinfo;
        }
    }

    std::array<std::uint8_t, 6U> invalid_cinfo{{0x88U, 0U, 0U, 0U, 0U, 0U}};
    invalid_cinfo[1] = valid_fcheck(invalid_cinfo[0]);
    if (zmid::content_inspection::zlib_header_valid(
            invalid_cinfo.data(), static_cast<std::uint32_t>(invalid_cinfo.size())) ||
        zmid::content_inspection::looks_like_zlib_stream(
            invalid_cinfo.data(), static_cast<std::uint32_t>(invalid_cinfo.size()))) {
        return 50;
    }

    const auto undersized = read_all(undersized_window_path);
    const auto full_window = read_all(full_window_path);
    if (!zmid::content_inspection::zlib_header_valid(
            undersized.data(), static_cast<std::uint32_t>(undersized.size())) ||
        zmid::content_inspection::zlib_window_limit(undersized.data()) != 256U ||
        !zmid::content_inspection::zlib_header_valid(
            full_window.data(), static_cast<std::uint32_t>(full_window.size())) ||
        zmid::content_inspection::zlib_window_limit(full_window.data()) != 32768U ||
        undersized.size() != full_window.size() ||
        !std::equal(undersized.begin() + 2, undersized.end(), full_window.begin() + 2)) {
        return 51;
    }

    std::uint32_t decoded_size = 0U;
    if (zmid::content_inspection::decode_zlib(
            full_window.data(), static_cast<std::uint32_t>(full_window.size()),
            decoded.data(), static_cast<std::uint32_t>(decoded.size()),
            decoded_size) != WrappedResult::decoded || decoded_size != 260U ||
        decoded[0] != 0U || decoded[1] != 1U || decoded[2] != 2U ||
        decoded[256] != 0U || decoded[257] != 0U ||
        decoded[258] != 1U || decoded[259] != 2U) {
        return 52;
    }

    decoded.fill(0U);
    decoded_size = 0U;
    if (zmid::content_inspection::decode_zlib(
            undersized.data(), static_cast<std::uint32_t>(undersized.size()),
            decoded.data(), static_cast<std::uint32_t>(decoded.size()),
            decoded_size) != WrappedResult::unsupported) {
        return 53;
    }

    std::cout << "ZMID_ZLIB_CINFO_OK windows=0-7 magic=cmf-flg"
                 " decode=8/8 inspect=8/8 invalid=blocked"
                 " window=control-accepted+declared-distance-blocked\n";
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: zmid-zlib-cinfo-host-test <undersized-window.zlib>"
                     " <full-window-control.zlib>\n";
        return 2;
    }
    try {
        return run(argv[1], argv[2]);
    } catch (const std::exception& error) {
        std::cerr << "zmid-zlib-cinfo-host-test: " << error.what() << "\n";
        return 1;
    }
}