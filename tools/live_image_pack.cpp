#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::size_t sector_size = 512U;
constexpr std::size_t zero_gap = 8U;
constexpr std::size_t max_chunk_bytes = 0xFFFFU;

struct Chunk {
    std::uint32_t offset;
    std::vector<std::uint8_t> bytes;
};

void append_u16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value >> 16U));
    output.push_back(static_cast<std::uint8_t>(value >> 24U));
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

void emit_byte_array(std::ofstream& output, const std::vector<std::uint8_t>& payload) {
    output << "constexpr uint8_t live_image_pack[] = {\n";
    output << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < payload.size(); ++i) {
        if (i % 16U == 0U) output << "    ";
        output << "0x" << std::setw(2) << static_cast<unsigned>(payload[i]);
        if (i + 1U != payload.size()) output << ',';
        if (i % 16U == 15U || i + 1U == payload.size()) output << '\n';
        else output << ' ';
    }
    output << std::dec << "};\n";
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
            const std::size_t length = end - start;
            if (length > max_chunk_bytes) {
                throw std::runtime_error("sparse chunk exceeds ZLIVE002 uint16 length");
            }
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
        payload.reserve(20U + chunks.size() * 8U + data_bytes);
        payload.insert(payload.end(), {'Z','L','I','V','E','0','0','2'});
        append_u32(payload, static_cast<std::uint32_t>(image.size() / sector_size));
        append_u32(payload, static_cast<std::uint32_t>(chunks.size()));
        append_u32(payload, static_cast<std::uint32_t>(data_bytes));
        for (const Chunk& chunk : chunks) {
            append_u32(payload, chunk.offset);
            append_u16(payload, static_cast<std::uint16_t>(chunk.bytes.size()));
            append_u16(payload, 0U);
        }
        for (const Chunk& chunk : chunks) {
            payload.insert(payload.end(), chunk.bytes.begin(), chunk.bytes.end());
        }

        std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error(std::string("cannot open output: ") + argv[2]);
        output << "// Generated from the canonical ZenovFS image. Regenerate with tools/live_image_pack.cpp.\n";
        output << "constexpr uint32_t live_image_pack_bytes = " << payload.size() << "U;\n";
        output << "constexpr uint32_t live_image_logical_sectors = " << image.size() / sector_size << "U;\n";
        output << "constexpr uint32_t live_image_chunk_count = " << chunks.size() << "U;\n";
        emit_byte_array(output, payload);
        output << "static_assert(sizeof(live_image_pack) == live_image_pack_bytes, \"ZLIVE002 size mismatch\");\n";
        if (!output) throw std::runtime_error("cannot write output file");

        std::cout << "LIVE_IMAGE_PACK_OK format=ZLIVE002 sectors=" << image.size() / sector_size
                  << " chunks=" << chunks.size() << " packed=" << payload.size()
                  << " data=" << data_bytes << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "live-image-pack: " << error.what() << '\n';
        return 1;
    }
}
