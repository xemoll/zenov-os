#pragma once

// SPDX-License-Identifier: BSD-2-Clause

#include "manifest.hpp"
#include "sha256.hpp"

#include <array>
#include <cstring>

namespace zenpkg {

inline constexpr std::array<std::uint8_t, 8> package_magic = {'Z','E','N','P','K','G','1','\0'};
inline constexpr std::uint32_t package_version = 1;
inline constexpr std::size_t package_header_size = 128;
inline constexpr std::uint64_t max_manifest_size = 64U * 1024U;
inline constexpr std::uint64_t max_payload_size = 4ULL * 1024ULL * 1024ULL * 1024ULL;

struct Package final {
    Manifest manifest;
    std::string manifest_text;
    std::vector<std::uint8_t> payload;
    Sha256::Digest manifest_digest{};
    Sha256::Digest payload_digest{};
    std::string package_digest_hex;
};

inline std::vector<std::uint8_t> build_package_bytes(const Manifest& manifest,
                                                     const std::vector<std::uint8_t>& payload) {
    validate_manifest(manifest, payload.size());
    const std::string canonical = canonical_manifest(manifest);
    if (canonical.size() > max_manifest_size) throw Error("manifest exceeds package limit");
    if (payload.size() > max_payload_size) throw Error("payload exceeds package limit");

    const auto manifest_digest = Sha256::hash(canonical);
    const auto payload_digest = Sha256::hash(payload);
    std::vector<std::uint8_t> output;
    output.reserve(package_header_size + canonical.size() + payload.size());
    output.insert(output.end(), package_magic.begin(), package_magic.end());
    append_u32_le(output, package_version);
    append_u32_le(output, 0U);
    append_u64_le(output, canonical.size());
    append_u64_le(output, payload.size());
    output.insert(output.end(), manifest_digest.begin(), manifest_digest.end());
    output.insert(output.end(), payload_digest.begin(), payload_digest.end());

    std::vector<std::uint8_t> header_without_hash = output;
    const auto header_digest = Sha256::hash(header_without_hash);
    output.insert(output.end(), header_digest.begin(), header_digest.end());
    if (output.size() != package_header_size) throw Error("internal package header size mismatch");
    output.insert(output.end(), canonical.begin(), canonical.end());
    output.insert(output.end(), payload.begin(), payload.end());
    return output;
}

inline void pack_to_file(const std::filesystem::path& manifest_path,
                         const std::filesystem::path& payload_path,
                         const std::filesystem::path& output_path) {
    const auto payload = payload_path == "-" ? std::vector<std::uint8_t>{} : read_binary(payload_path);
    const auto manifest = parse_and_validate_manifest(read_text(manifest_path), payload.size());
    write_binary_atomic(output_path, build_package_bytes(manifest, payload));
}

inline Package parse_package_bytes(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < package_header_size) throw Error("package is smaller than the fixed header");
    if (!std::equal(package_magic.begin(), package_magic.end(), bytes.begin())) throw Error("invalid package magic");
    const auto version = read_u32_le(bytes, 8);
    const auto flags = read_u32_le(bytes, 12);
    const auto manifest_size = read_u64_le(bytes, 16);
    const auto payload_size = read_u64_le(bytes, 24);
    if (version != package_version) throw Error("unsupported package version: " + std::to_string(version));
    if (flags != 0) throw Error("unsupported package flags");
    if (manifest_size > max_manifest_size) throw Error("manifest size exceeds package limit");
    if (payload_size > max_payload_size) throw Error("payload size exceeds package limit");
    if (manifest_size > std::numeric_limits<std::size_t>::max() || payload_size > std::numeric_limits<std::size_t>::max()) {
        throw Error("package sizes exceed host address space");
    }
    const std::uint64_t expected_size = static_cast<std::uint64_t>(package_header_size) + manifest_size + payload_size;
    if (expected_size != bytes.size()) throw Error("package length does not match header sizes");

    Sha256::Digest expected_manifest{};
    Sha256::Digest expected_payload{};
    Sha256::Digest expected_header{};
    std::copy_n(bytes.begin() + 32, expected_manifest.size(), expected_manifest.begin());
    std::copy_n(bytes.begin() + 64, expected_payload.size(), expected_payload.begin());
    std::copy_n(bytes.begin() + 96, expected_header.size(), expected_header.begin());
    const std::vector<std::uint8_t> header_without_hash(bytes.begin(), bytes.begin() + 96);
    if (Sha256::hash(header_without_hash) != expected_header) throw Error("package header checksum mismatch");

    const auto manifest_begin = bytes.begin() + static_cast<std::ptrdiff_t>(package_header_size);
    const auto manifest_end = manifest_begin + static_cast<std::ptrdiff_t>(manifest_size);
    const std::string manifest_text(manifest_begin, manifest_end);
    const std::vector<std::uint8_t> payload(manifest_end, bytes.end());
    const auto actual_manifest = Sha256::hash(manifest_text);
    const auto actual_payload = Sha256::hash(payload);
    if (actual_manifest != expected_manifest) throw Error("manifest SHA-256 mismatch");
    if (actual_payload != expected_payload) throw Error("payload SHA-256 mismatch");

    auto manifest = parse_and_validate_manifest(manifest_text, payload.size());
    if (canonical_manifest(manifest) != manifest_text) throw Error("manifest is not in canonical form");

    Package package;
    package.manifest = std::move(manifest);
    package.manifest_text = manifest_text;
    package.payload = payload;
    package.manifest_digest = actual_manifest;
    package.payload_digest = actual_payload;
    package.package_digest_hex = sha256_hex(bytes);
    return package;
}

inline Package read_package(const std::filesystem::path& path) {
    return parse_package_bytes(read_binary(path));
}

inline void extract_package(const std::filesystem::path& package_path,
                            const std::filesystem::path& manifest_output,
                            const std::filesystem::path& payload_output) {
    const auto package = read_package(package_path);
    write_text_atomic(manifest_output, package.manifest_text);
    write_binary_atomic(payload_output, package.payload);
}

struct IndexRecord final {
    std::string name;
    std::string version;
    std::string architecture;
    std::string target;
    std::string kind;
    std::string runtime;
    std::string file;
    std::uint64_t bytes = 0;
    std::string sha256;
};

inline std::vector<IndexRecord> scan_packages(const std::filesystem::path& directory) {
    if (!std::filesystem::is_directory(directory)) throw Error("index input is not a directory: " + directory.string());
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".zpk") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    std::vector<IndexRecord> records;
    std::set<std::string> identities;
    for (const auto& path : files) {
        const auto filename = path.filename().string();
        if (!is_printable_text(filename, 128) || filename.size() <= 4 ||
            filename.substr(filename.size() - 4) != ".zpk") {
            throw Error("unsafe package filename in repository: " + filename);
        }
        for (const unsigned char ch : filename) {
            if (!(std::isalnum(ch) || ch == '.' || ch == '-' || ch == '_')) {
                throw Error("unsafe package filename in repository: " + filename);
            }
        }
        const auto bytes = read_binary(path);
        const auto package = parse_package_bytes(bytes);
        const std::string identity = package.manifest.name + "@" + package.manifest.version + "#" + package.manifest.target;
        if (!identities.insert(identity).second) throw Error("duplicate package identity in index: " + identity);
        records.push_back(IndexRecord{
            package.manifest.name,
            package.manifest.version,
            package.manifest.architecture,
            package.manifest.target,
            package.manifest.kind,
            package.manifest.runtime,
            filename,
            static_cast<std::uint64_t>(bytes.size()),
            package.package_digest_hex
        });
    }
    std::sort(records.begin(), records.end(), [](const IndexRecord& left, const IndexRecord& right) {
        if (left.name != right.name) return left.name < right.name;
        if (left.version != right.version) return left.version < right.version;
        return left.target < right.target;
    });
    return records;
}

inline std::string render_index(const std::vector<IndexRecord>& records) {
    std::ostringstream output;
    output << "format=zenpkg-index-1\n";
    output << "columns=name\tversion\tarchitecture\ttarget\tkind\truntime\tfile\tbytes\tsha256\n";
    for (const auto& record : records) {
        output << "package=" << record.name << '\t' << record.version << '\t' << record.architecture << '\t'
               << record.target << '\t' << record.kind << '\t' << record.runtime << '\t' << record.file << '\t'
               << record.bytes << '\t' << record.sha256 << '\n';
    }
    return output.str();
}

inline void build_index(const std::filesystem::path& directory, const std::filesystem::path& output_path) {
    write_text_atomic(output_path, render_index(scan_packages(directory)));
}

} // namespace zenpkg
