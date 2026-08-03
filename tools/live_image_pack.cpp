#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::size_t sector_size = 512U;
constexpr std::size_t zero_gap = 8U;
constexpr std::size_t max_chunk_bytes = 0xFFFFU;
constexpr std::size_t lz_window = 4096U;
constexpr std::size_t lz_min_match = 3U;
constexpr std::size_t lz_max_match = 18U;
constexpr std::size_t lz_chain_limit = 128U;
constexpr std::size_t lz_hash_slots = 65536U;

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

std::uint32_t fnv1a32(const std::vector<std::uint8_t>& input) {
    std::uint32_t value = 2166136261U;
    for (const std::uint8_t byte : input) {
        value ^= byte;
        value *= 16777619U;
    }
    return value;
}

std::uint32_t lz_hash(const std::vector<std::uint8_t>& input, std::size_t position) {
    const std::uint32_t a = input[position];
    const std::uint32_t b = input[position + 1U];
    const std::uint32_t c = input[position + 2U];
    return ((a * 251U + b) * 251U + c) & 0xFFFFU;
}

std::vector<std::uint8_t> lzss_compress(const std::vector<std::uint8_t>& input) {
    std::vector<std::int32_t> head(lz_hash_slots, -1);
    std::vector<std::int32_t> previous(input.size(), -1);
    std::vector<std::uint8_t> output;
    output.reserve(input.size());

    const auto insert_position = [&](std::size_t position) {
        if (position + lz_min_match > input.size()) return;
        const std::uint32_t hash = lz_hash(input, position);
        previous[position] = head[hash];
        head[hash] = static_cast<std::int32_t>(position);
    };

    std::size_t position = 0U;
    while (position < input.size()) {
        const std::size_t flag_position = output.size();
        output.push_back(0U);
        std::uint8_t flags = 0U;

        for (std::uint32_t bit = 0U; bit < 8U && position < input.size(); ++bit) {
            std::size_t best_length = 0U;
            std::size_t best_distance = 0U;

            if (position + lz_min_match <= input.size()) {
                const std::uint32_t hash = lz_hash(input, position);
                std::int32_t candidate = head[hash];
                std::size_t visited = 0U;
                while (candidate >= 0 && visited < lz_chain_limit) {
                    const std::size_t source = static_cast<std::size_t>(candidate);
                    const std::size_t distance = position - source;
                    if (distance > lz_window) break;
                    std::size_t length = 0U;
                    const std::size_t limit =
                        input.size() - position < lz_max_match
                            ? input.size() - position
                            : lz_max_match;
                    while (length < limit && input[source + length] == input[position + length]) {
                        ++length;
                    }
                    if (length > best_length && length >= lz_min_match) {
                        best_length = length;
                        best_distance = distance;
                        if (best_length == lz_max_match) break;
                    }
                    candidate = previous[source];
                    ++visited;
                }
            }

            if (best_length >= lz_min_match) {
                const std::uint16_t code = static_cast<std::uint16_t>(
                    ((best_distance - 1U) << 4U) | (best_length - lz_min_match));
                output.push_back(static_cast<std::uint8_t>(code));
                output.push_back(static_cast<std::uint8_t>(code >> 8U));
                for (std::size_t consumed = 0U; consumed < best_length; ++consumed) {
                    insert_position(position + consumed);
                }
                position += best_length;
            } else {
                flags = static_cast<std::uint8_t>(flags | (1U << bit));
                output.push_back(input[position]);
                insert_position(position);
                ++position;
            }
        }
        output[flag_position] = flags;
    }
    return output;
}

void emit_byte_array(std::ofstream& output,
                     const std::vector<std::uint8_t>& payload,
                     const char* name) {
    output << "constexpr uint8_t " << name << "[] = {\n";
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
        if (image.size() % sector_size != 0U ||
            image.size() / sector_size > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("input must be a sector-aligned 32-bit image");
        }

        std::vector<Chunk> chunks;
        for (std::size_t i = 0U; i < image.size();) {
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
        if (chunks.empty() ||
            chunks.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("input produced an invalid sparse chunk set");
        }

        std::size_t data_bytes = 0U;
        for (const Chunk& chunk : chunks) data_bytes += chunk.bytes.size();
        if (data_bytes > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("packed data is too large");
        }

        std::vector<std::uint8_t> sparse;
        sparse.reserve(20U + chunks.size() * 8U + data_bytes);
        sparse.insert(sparse.end(), {'Z','L','I','V','E','0','0','2'});
        append_u32(sparse, static_cast<std::uint32_t>(image.size() / sector_size));
        append_u32(sparse, static_cast<std::uint32_t>(chunks.size()));
        append_u32(sparse, static_cast<std::uint32_t>(data_bytes));
        for (const Chunk& chunk : chunks) {
            append_u32(sparse, chunk.offset);
            append_u16(sparse, static_cast<std::uint16_t>(chunk.bytes.size()));
            append_u16(sparse, 0U);
        }
        for (const Chunk& chunk : chunks) {
            sparse.insert(sparse.end(), chunk.bytes.begin(), chunk.bytes.end());
        }

        const std::vector<std::uint8_t> compressed = lzss_compress(sparse);
        if (compressed.empty() || compressed.size() >= sparse.size()) {
            throw std::runtime_error("ZLIVE003 compression did not reduce the sparse payload");
        }

        std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error(std::string("cannot open output: ") + argv[2]);
        output << "// Generated from the final canonical ZenovFS image.\n";
        output << "// ZLIVE003 transport: LZSS-compressed ZLIVE002 sparse payload.\n";
        output << "constexpr uint32_t live_image_pack_bytes = " << sparse.size() << "U;\n";
        output << "constexpr uint32_t live_image_compressed_bytes = " << compressed.size() << "U;\n";
        output << "constexpr uint32_t live_image_pack_checksum = " << fnv1a32(sparse) << "U;\n";
        output << "constexpr uint32_t live_image_logical_sectors = "
               << image.size() / sector_size << "U;\n";
        output << "constexpr uint32_t live_image_chunk_count = " << chunks.size() << "U;\n";
        emit_byte_array(output, compressed, "live_image_compressed");
        output << "static_assert(sizeof(live_image_compressed) == live_image_compressed_bytes, "
                  "\"ZLIVE003 size mismatch\");\n";
        if (!output) throw std::runtime_error("cannot write output file");

        std::cout << "LIVE_IMAGE_PACK_OK format=ZLIVE003 sparse=ZLIVE002 sectors="
                  << image.size() / sector_size << " chunks=" << chunks.size()
                  << " raw=" << sparse.size() << " compressed=" << compressed.size()
                  << " data=" << data_bytes << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "live-image-pack: " << error.what() << '\n';
        return 1;
    }
}
