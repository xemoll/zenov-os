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

struct StreamingFileDigest final {
    std::string sha256;
    std::uint64_t file_size = 0;
};

inline StreamingFileDigest sha256_file_streaming(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw Error("cannot open file for hashing: " + path.string());

    Sha256 hasher;
    std::array<std::uint8_t, foreign_probe_chunk_bytes> chunk{};
    std::uint64_t consumed = 0;
    while (stream) {
        stream.read(reinterpret_cast<char*>(chunk.data()),
                    static_cast<std::streamsize>(chunk.size()));
        const std::streamsize got_stream = stream.gcount();
        if (got_stream <= 0) break;
        const auto got = static_cast<std::size_t>(got_stream);
        hasher.update(chunk.data(), got);
        consumed += static_cast<std::uint64_t>(got);
    }
    if (!stream.eof()) throw Error("hash read failed: " + path.string());

    const auto digest = hasher.final();
    return StreamingFileDigest{
        hex_encode(digest.data(), digest.size()),
        consumed
    };
}

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

inline ForeignProbe require_native_import_candidate(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw Error("cannot open native import input: " + path.string());

    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end < 0) throw Error("cannot determine native import input size: " + path.string());
    const auto file_size = static_cast<std::uint64_t>(end);
    if (file_size > zenov_package_limit) {
        throw Error("native import input exceeds the current 64 KiB package limit");
    }

    stream.clear();
    stream.seekg(0, std::ios::beg);
    if (!stream) throw Error("cannot seek native import input: " + path.string());

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_size));
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
            throw Error("native import input changed or was truncated while reading");
        }
    }

    char extra = 0;
    stream.read(&extra, 1);
    if (stream.gcount() != 0) {
        throw Error("native import input changed or grew while reading");
    }
    if (!stream.eof()) throw Error("native import input read failed: " + path.string());

    const std::string filename = path.filename().string();
    const auto detection = package_foreign::classify(
        bytes.data(), static_cast<std::uint32_t>(bytes.size()), filename.c_str());
    if (detection.format != package_foreign::Format::zex1 &&
        detection.format != package_foreign::Format::elf) {
        throw Error(std::string("format ") + foreign_format_id(detection.format) +
                    " is not eligible for native import; support=" +
                    package_foreign::support_text(detection.support));
    }
    const std::string digest = sha256_hex(bytes);
    return ForeignProbe{std::move(bytes), detection, digest};
}

inline Package import_native(const ForeignProbe& probe, const std::string& name,
                             const std::string& version, const std::string& license,
                             const std::string& source, const std::string& asset_policy,
                             const std::filesystem::path& output_path) {
    if (!kernel_safe_package_name(name)) throw Error("name must be a lowercase ZenovOS package identifier of at most 31 characters");
    if (!kernel_safe_package_version(version)) throw Error("version must fit the ZenovOS 0.1.1 package database");
    if (!is_printable_text(license, 96U)) throw Error("license is required and must fit 96 printable characters");
    if (!is_printable_text(source, 256U)) throw Error("source is required and must fit 256 printable characters");
    if (asset_policy != "redistributable") {
        throw Error("0.1.1 native import accepts only asset_policy=redistributable");
    }
    if (probe.sha256 != sha256_hex(probe.bytes)) {
        throw Error("native import candidate snapshot digest mismatch");
    }

    Manifest manifest;
    manifest.format = "zenpkg-manifest-1";
    manifest.name = name;
    manifest.version = version;
    manifest.architecture = "i686";
    manifest.target = "i686-zenov-none";
    manifest.kind = "application";
    manifest.runtime = "native";
    manifest.min_os = "0.1.1";
    manifest.license = license;
    manifest.source = source;
    manifest.asset_policy = asset_policy;
    manifest.capabilities = {"kernel.ring3", "storage.zenovfs1"};

    if (probe.detection.format == package_foreign::Format::zex1) {
        validate_zex1_import(probe.bytes);
        manifest.payload_type = "zex1";
        manifest.entrypoint = "/data/apps/pkg-" + name + "-" + version + ".zex";
        manifest.capabilities.push_back("abi.zex1.v1");
    } else if (probe.detection.format == package_foreign::Format::elf) {
        validate_elf32_i386_import(probe.bytes);
        manifest.payload_type = "elf32";
        manifest.entrypoint = "/data/apps/pkg-" + name + "-" + version + ".elf";
        manifest.capabilities.push_back("abi.elf32.i386.static");
    } else {
        throw Error("internal native import candidate format mismatch");
    }
    if (manifest.entrypoint.size() > 47U) {
        throw Error("name and version produce an entrypoint longer than the 0.1.1 kernel path limit");
    }

    const auto package_bytes = build_package_bytes(manifest, probe.bytes);
    if (package_bytes.size() > zenov_package_limit) {
        throw Error("imported package exceeds the current 64 KiB ZenovFS/package-manager limit");
    }
    write_binary_atomic(output_path, package_bytes);
    auto package = read_package(output_path);
    if (hex_encode(package.payload_digest.data(), package.payload_digest.size()) != probe.sha256) {
        std::error_code ignored;
        std::filesystem::remove(output_path, ignored);
        throw Error("native import output payload does not match the validated snapshot");
    }
    return package;
}

} // namespace zenpkg
