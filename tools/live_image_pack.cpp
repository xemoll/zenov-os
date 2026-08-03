#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::size_t sector_size = 512U;
constexpr std::size_t zero_gap = 8U;

struct Chunk {
    std::uint32_t offset;
    std::vector<std::uint8_t> bytes;
};

void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value >> 16U));
    output.push_back(static_cast<std::uint8_t>(value >> 24U));
}

std::string base64(const std::vector<std::uint8_t>& input) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((input.size() + 2U) / 3U) * 4U);
    for (std::size_t i = 0; i < input.size(); i += 3U) {
        const std::uint32_t a = input[i];
        const std::uint32_t b = i + 1U < input.size() ? input[i + 1U] : 0U;
        const std::uint32_t c = i + 2U < input.size() ? input[i + 2U] : 0U;
        const std::uint32_t word = (a << 16U) | (b << 8U) | c;
        output.push_back(alphabet[(word >> 18U) & 63U]);
        output.push_back(alphabet[(word >> 12U) & 63U]);
        output.push_back(i + 1U < input.size() ? alphabet[(word >> 6U) & 63U] : '=');
        output.push_back(i + 2U < input.size() ? alphabet[word & 63U] : '=');
    }
    return output;
}

std::vector<std::uint8_t> read_all(const char* path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error(std::string("cannot open input: ") + path);
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end <= 0) throw std::runtime_error("input image is empty");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("cannot read complete input image");
    return bytes;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            std::cerr << "usage: live-image-pack <zenov-data.img> <output.inc>\n";
            return 2;
        }
        const std::vector<std::uint8_t> image = read_all(argv[1]);
        if (image.size() % sector_size != 0U || image.size() / sector_size > 0xFFFFFFFFULL) {
            throw std::runtime_error("input must be a sector-aligned 32-bit image");
        }

        std::vector<Chunk> chunks;
        for (std::size_t i = 0; i < image.size();) {
            while (i < image.size() && image[i] == 0U) ++i;
            if (i == image.size()) break;
            const std::size_t start = i;
            std::size_t last_nonzero = i;
            ++i;
            while (i < image.size()) {
                if (image[i] != 0U) last_nonzero = i;
                else if (i - last_nonzero > zero_gap) break;
                ++i;
            }
            const std::size_t end = last_nonzero + 1U;
            chunks.push_back(Chunk{
                static_cast<std::uint32_t>(start),
                std::vector<std::uint8_t>(image.begin() + static_cast<std::ptrdiff_t>(start),
                                          image.begin() + static_cast<std::ptrdiff_t>(end))});
            i = end;
        }
        if (chunks.empty() || chunks.size() > 0xFFFFFFFFULL) {
            throw std::runtime_error("input produced an invalid sparse chunk set");
        }

        std::size_t data_bytes = 0U;
        for (const Chunk& chunk : chunks) data_bytes += chunk.bytes.size();
        if (data_bytes > 0xFFFFFFFFULL) throw std::runtime_error("packed data is too large");

        std::vector<std::uint8_t> payload;
        payload.insert(payload.end(), {'Z','L','I','V','E','0','0','1'});
        append_u32(payload, static_cast<std::uint32_t>(image.size() / sector_size));
        append_u32(payload, static_cast<std::uint32_t>(chunks.size()));
        append_u32(payload, static_cast<std::uint32_t>(data_bytes));
        std::uint32_t data_offset = 0U;
        for (const Chunk& chunk : chunks) {
            append_u32(payload, chunk.offset);
            append_u32(payload, static_cast<std::uint32_t>(chunk.bytes.size()));
            append_u32(payload, data_offset);
            data_offset += static_cast<std::uint32_t>(chunk.bytes.size());
        }
        for (const Chunk& chunk : chunks) {
            payload.insert(payload.end(), chunk.bytes.begin(), chunk.bytes.end());
        }

        const std::string encoded = base64(payload);
        std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error(std::string("cannot open output: ") + argv[2]);
        output << "// Generated from the canonical ZenovFS image. Regenerate with tools/live_image_pack.cpp.\n";
        output << "constexpr uint32_t live_image_pack_decoded_bytes = " << payload.size() << "U;\n";
        output << "constexpr uint32_t live_image_logical_sectors = " << image.size() / sector_size << "U;\n";
        output << "constexpr uint32_t live_image_chunk_count = " << chunks.size() << "U;\n";
        output << "constexpr char live_image_pack_base64[] =\n";
        for (std::size_t i = 0; i < encoded.size(); i += 96U) {
            output << "    \"" << encoded.substr(i, 96U) << "\"\n";
        }
        output << "    ;\n";
        if (!output) throw std::runtime_error("cannot write output file");

        std::cout << "LIVE_IMAGE_PACK_OK sectors=" << image.size() / sector_size
                  << " chunks=" << chunks.size() << " decoded=" << payload.size()
                  << " base64=" << encoded.size() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "live-image-pack: " << error.what() << '\n';
        return 1;
    }
}
