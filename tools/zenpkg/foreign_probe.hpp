#pragma once

// SPDX-License-Identifier: BSD-2-Clause

#include "foreign.hpp"
#include "../../kernel/parts/package_foreign_policy.inc"

#include <array>

namespace zenpkg {

inline constexpr std::size_t foreign_probe_head_bytes = 64U * 1024U;
inline constexpr std::size_t foreign_probe_tail_bytes = 512U;
inline constexpr std::size_t foreign_probe_chunk_bytes = 64U * 1024U;

struct StreamingForeignProbe final {
    package_foreign::Detection detection;
    std::string sha256;
    std::uint64_t file_size = 0;
    std::size_t sampled_bytes = 0;
};

inline StreamingForeignProbe probe_foreign_streaming(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw Error("cannot open file for probing: " + path.string());

    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end < 0) throw Error("cannot determine probe file size: " + path.string());
    const auto file_size = static_cast<std::uint64_t>(end);
    stream.clear();
    stream.seekg(0, std::ios::beg);
    if (!stream) throw Error("cannot seek probe file: " + path.string());

    Sha256 hasher;
    std::array<std::uint8_t, foreign_probe_chunk_bytes> chunk{};
    std::vector<std::uint8_t> sample;
    std::vector<std::uint8_t> tail;
    sample.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(
        file_size, foreign_probe_head_bytes + foreign_probe_tail_bytes)));
    tail.reserve(foreign_probe_tail_bytes);

    std::uint64_t consumed = 0;
    while (stream) {
        stream.read(reinterpret_cast<char*>(chunk.data()),
                    static_cast<std::streamsize>(chunk.size()));
        const std::streamsize got_stream = stream.gcount();
        if (got_stream <= 0) break;
        const auto got = static_cast<std::size_t>(got_stream);
        hasher.update(chunk.data(), got);
        consumed += static_cast<std::uint64_t>(got);

        if (sample.size() < foreign_probe_head_bytes) {
            const std::size_t copy = std::min(got, foreign_probe_head_bytes - sample.size());
            sample.insert(sample.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(copy));
        }

        if (got >= foreign_probe_tail_bytes) {
            tail.assign(chunk.begin() + static_cast<std::ptrdiff_t>(got - foreign_probe_tail_bytes),
                        chunk.begin() + static_cast<std::ptrdiff_t>(got));
        } else {
            const std::size_t overflow = tail.size() + got > foreign_probe_tail_bytes
                ? tail.size() + got - foreign_probe_tail_bytes
                : 0U;
            if (overflow) tail.erase(tail.begin(), tail.begin() + static_cast<std::ptrdiff_t>(overflow));
            tail.insert(tail.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got));
        }
    }
    if (!stream.eof()) throw Error("probe read failed: " + path.string());
    if (consumed != file_size) throw Error("probe short read: " + path.string());

    if (file_size > sample.size()) {
        const std::uint64_t missing = file_size - static_cast<std::uint64_t>(sample.size());
        const std::size_t append = static_cast<std::size_t>(
            std::min<std::uint64_t>(missing, tail.size()));
        sample.insert(sample.end(), tail.end() - static_cast<std::ptrdiff_t>(append), tail.end());
    }
    if (sample.size() > 0xffffffffULL) throw Error("probe sample exceeds classifier limit");

    const std::string filename = path.filename().string();
    const auto detection = package_foreign::classify(
        sample.data(), static_cast<std::uint32_t>(sample.size()), filename.c_str());
    const auto digest = hasher.final();
    return StreamingForeignProbe{
        detection,
        hex_encode(digest.data(), digest.size()),
        file_size,
        sample.size()
    };
}

inline StreamingForeignProbe require_native_import_candidate(const std::filesystem::path& path) {
    const auto probe = probe_foreign_streaming(path);
    if (probe.file_size > zenov_package_limit) {
        throw Error("native import input exceeds the current 64 KiB package limit");
    }
    if (probe.detection.format != package_foreign::Format::zex1 &&
        probe.detection.format != package_foreign::Format::elf) {
        throw Error(std::string("format ") + foreign_format_id(probe.detection.format) +
                    " is not eligible for native import; support=" +
                    package_foreign::support_text(probe.detection.support));
    }
    return probe;
}

} // namespace zenpkg
