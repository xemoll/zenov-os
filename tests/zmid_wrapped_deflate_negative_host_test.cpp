#include "zmid_content_workspace_host_shim.hpp"

#define FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
#include "zmid_content_inspection_host_test.cpp"
#undef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION

namespace {
using Result = zmid::content_inspection::WrappedDeflateResult;

constexpr std::uint8_t kPayload[] = "ZENOV_RANSOMWARE_TEST_V1";
constexpr std::uint8_t kRawDeflate[] = {
    0x8B,0x72,0xF5,0xF3,0x0F,0x8B,0x0F,0x72,0xF4,0x0B,0xF6,0xF7,0x0D,
    0x77,0x0C,0x72,0x8D,0x0F,0x71,0x0D,0x0E,0x89,0x0F,0x33,0x04,0x00
};
constexpr std::uint32_t kPayloadSize = sizeof(kPayload) - 1U;

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

std::vector<std::uint8_t> header(std::uint8_t flags) {
    return {0x1FU, 0x8BU, 8U, flags, 0U, 0U, 0U, 0U, 0U, 0xFFU};
}

std::vector<std::uint8_t> valid_gzip() {
    auto output = header(0U);
    output.insert(output.end(), std::begin(kRawDeflate), std::end(kRawDeflate));
    append_le32(output, zmid::content_inspection::crc32(kPayload, kPayloadSize));
    append_le32(output, kPayloadSize);
    return output;
}

bool unsupported(const std::vector<std::uint8_t>& input) {
    std::array<std::uint8_t, 64U * 1024U> output{};
    std::uint32_t output_size = 0U;
    return zmid::content_inspection::decode_gzip(
        input.data(), static_cast<std::uint32_t>(input.size()),
        output.data(), static_cast<std::uint32_t>(output.size()),
        output_size) == Result::unsupported;
}

int run() {
    std::array<std::uint8_t, 64U * 1024U> output{};
    std::uint32_t output_size = 0U;
    const auto valid = valid_gzip();
    if (zmid::content_inspection::decode_gzip(
            valid.data(), static_cast<std::uint32_t>(valid.size()),
            output.data(), static_cast<std::uint32_t>(output.size()),
            output_size) != Result::decoded || output_size != kPayloadSize ||
        std::memcmp(output.data(), kPayload, kPayloadSize) != 0) return 10;

    auto bad_method = valid;
    bad_method[2] = 0U;
    if (!unsupported(bad_method)) return 11;

    auto bad_isize = valid;
    bad_isize.back() ^= 0x01U;
    if (!unsupported(bad_isize)) return 12;

    auto short_header = header(0U);
    short_header.resize(9U);
    if (!unsupported(short_header)) return 13;

    auto truncated_extra = header(0x04U);
    append_le16(truncated_extra, 4U);
    truncated_extra.push_back(0x11U);
    truncated_extra.push_back(0x22U);
    if (!unsupported(truncated_extra)) return 14;

    auto truncated_name = header(0x08U);
    truncated_name.insert(truncated_name.end(), {'n','a','m','e'});
    if (!unsupported(truncated_name)) return 15;

    auto truncated_comment = header(0x10U);
    truncated_comment.insert(truncated_comment.end(), {'n','o','t','e'});
    if (!unsupported(truncated_comment)) return 16;

    auto truncated_fhcrc = header(0x02U);
    truncated_fhcrc.push_back(0xAAU);
    if (!unsupported(truncated_fhcrc)) return 17;

    auto empty_extra_without_stream = header(0x04U);
    append_le16(empty_extra_without_stream, 0U);
    if (!unsupported(empty_extra_without_stream)) return 18;

    std::cout << "ZMID_WRAPPED_NEGATIVE_OK gzip=cm+isize+header"
                 " optional=fextra+fname+fcomment+fhcrc truncation=blocked\n";
    return 0;
}
} // namespace

int main() { return run(); }