#pragma once

#include "descriptor.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace zenuniverse {

struct FileDigest {
    std::uint64_t bytes = 0U;
    std::string sha256;
};

FileDigest digest_regular_file(const fs::path& path) {
    struct stat link_info{};
    if (::lstat(path.c_str(), &link_info) != 0) throw Error("cannot stat artifact: " + path.string());
    if (S_ISLNK(link_info.st_mode)) throw Error("symbolic links are not accepted as artifacts: " + path.string());

    const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        if (errno == ELOOP) throw Error("symbolic links are not accepted as artifacts: " + path.string());
        throw Error("cannot open artifact: " + path.string() + ": " + std::strerror(errno));
    }
    struct DescriptorGuard {
        int value;
        ~DescriptorGuard() { if (value >= 0) (void)::close(value); }
    } guard{descriptor};

    struct stat before{};
    if (::fstat(descriptor, &before) != 0) throw Error("cannot inspect opened artifact: " + path.string());
    if (!S_ISREG(before.st_mode)) throw Error("artifact is not a regular file: " + path.string());
    if (before.st_size <= 0) throw Error("artifact must not be empty: " + path.string());
    const auto expected_size = static_cast<std::uint64_t>(before.st_size);

    Sha256 hash;
    std::array<char, 1024U * 1024U> buffer{};
    std::uint64_t total = 0U;
    for (;;) {
        const auto count = ::read(descriptor, buffer.data(), buffer.size());
        if (count < 0) {
            if (errno == EINTR) continue;
            throw Error("artifact read failed: " + path.string() + ": " + std::strerror(errno));
        }
        if (count == 0) break;
        const auto chunk = static_cast<std::uint64_t>(count);
        if (total > std::numeric_limits<std::uint64_t>::max() - chunk) throw Error("artifact size overflows");
        total += chunk;
        hash.update(std::string_view(buffer.data(), static_cast<std::size_t>(count)));
    }

    struct stat after{};
    if (::fstat(descriptor, &after) != 0) throw Error("cannot re-inspect opened artifact: " + path.string());
    const bool identity_changed = before.st_dev != after.st_dev || before.st_ino != after.st_ino;
    const bool size_changed = before.st_size != after.st_size || total != expected_size;
    const bool time_changed = before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
                              before.st_ctim.tv_sec != after.st_ctim.tv_sec || before.st_ctim.tv_nsec != after.st_ctim.tv_nsec;
    if (identity_changed || size_changed || time_changed) throw Error("artifact changed while hashing: " + path.string());
    return FileDigest{total, hash.finish()};
}

struct ArtifactManifest {
    std::string schema = "ZENARTIFACT1";
    std::string profile;
    std::string artifact;
    std::string ownership;
    std::uint64_t bytes = 0U;
    std::string sha256;
};

const StringSet ownership_values = {"redistributable", "user-owned"};

void validate_artifact_manifest(const ArtifactManifest& manifest) {
    if (manifest.schema != "ZENARTIFACT1") throw Error("artifact manifest schema must be ZENARTIFACT1");
    if (!safe_id(manifest.profile)) throw Error("artifact manifest has unsafe profile id");
    if (!artifacts.count(manifest.artifact) || manifest.artifact == "metadata" || manifest.artifact == "runtime-bundle") throw Error("artifact manifest has unsupported artifact family");
    if (!ownership_values.count(manifest.ownership)) throw Error("artifact manifest ownership must be redistributable or user-owned");
    if (manifest.bytes == 0U || !hex64(manifest.sha256)) throw Error("artifact manifest requires non-zero bytes and lowercase SHA-256");
}

std::string canonical_artifact_manifest(const ArtifactManifest& manifest) {
    validate_artifact_manifest(manifest);
    std::ostringstream out;
    out << "ZENARTIFACT1\n"
        << "profile=" << manifest.profile << '\n'
        << "artifact=" << manifest.artifact << '\n'
        << "ownership=" << manifest.ownership << '\n'
        << "bytes=" << manifest.bytes << '\n'
        << "sha256=" << manifest.sha256 << '\n';
    return out.str();
}

ArtifactManifest parse_artifact_manifest(const fs::path& path) {
    const auto text = read_text_bounded(path, 4096U);
    if (text.empty() || text.back() != '\n') throw Error("artifact manifest must end with newline: " + path.string());
    std::istringstream input(text);
    ArtifactManifest manifest;
    std::string line;
    if (!std::getline(input, line) || line != "ZENARTIFACT1") throw Error("invalid artifact manifest header: " + path.string());
    StringSet seen;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') throw Error("artifact manifest must use LF line endings: " + path.string());
        if (line.empty()) throw Error("artifact manifest contains blank line: " + path.string());
        const auto equals = line.find('=');
        if (equals == std::string::npos || equals == 0U) throw Error("artifact manifest expected key=value: " + path.string());
        const auto key = line.substr(0U, equals);
        const auto value = line.substr(equals + 1U);
        if (value.empty() || !seen.insert(key).second) throw Error("artifact manifest empty or duplicate field: " + key);
        if (key == "profile") manifest.profile = value;
        else if (key == "artifact") manifest.artifact = value;
        else if (key == "ownership") manifest.ownership = value;
        else if (key == "bytes") manifest.bytes = parse_u64(value, "artifact bytes");
        else if (key == "sha256") manifest.sha256 = value;
        else throw Error("artifact manifest unknown field: " + key);
    }
    if (seen != StringSet{"artifact", "bytes", "ownership", "profile", "sha256"}) throw Error("artifact manifest field set is incomplete");
    validate_artifact_manifest(manifest);
    if (canonical_artifact_manifest(manifest) != text) throw Error("artifact manifest is not canonical: " + path.string());
    return manifest;
}

FileDigest verify_artifact_file(const ArtifactManifest& manifest, const fs::path& path) {
    const auto digest = digest_regular_file(path);
    if (digest.bytes != manifest.bytes) throw Error("artifact size mismatch");
    if (digest.sha256 != manifest.sha256) throw Error("artifact SHA-256 mismatch");
    return digest;
}

} // namespace zenuniverse
