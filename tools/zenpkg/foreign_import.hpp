#pragma once

// SPDX-License-Identifier: BSD-2-Clause

#include "archive.hpp"
#include "../../kernel/parts/package_foreign_format.inc"

namespace zenpkg {

inline constexpr std::uint32_t zenov_user_limit = 0x00100000U;
inline constexpr std::uint32_t zenov_stack_bytes = 16U * 1024U;
inline constexpr std::uint32_t zenov_stack_base = zenov_user_limit - zenov_stack_bytes;
inline constexpr std::uint32_t zenov_page_size = 4096U;
inline constexpr std::size_t zenov_package_limit = 64U * 1024U;

inline std::uint16_t foreign_u16_le(const std::vector<std::uint8_t>& input, std::size_t offset) {
    if (offset > input.size() || input.size() - offset < 2U) throw Error("truncated executable header");
    return static_cast<std::uint16_t>(input[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(input[offset + 1U]) << 8U);
}

inline std::uint32_t foreign_u32_le(const std::vector<std::uint8_t>& input, std::size_t offset) {
    if (offset > input.size() || input.size() - offset < 4U) throw Error("truncated executable header");
    return static_cast<std::uint32_t>(input[offset]) |
           (static_cast<std::uint32_t>(input[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(input[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(input[offset + 3U]) << 24U);
}

inline std::uint32_t zex_checksum(const std::uint8_t* data, std::size_t size) {
    std::uint32_t hash = 2166136261U;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619U;
    }
    return hash;
}

inline void validate_zex1_import(const std::vector<std::uint8_t>& bytes) {
    constexpr std::uint32_t header_size = 32U;
    if (bytes.size() < header_size || bytes[0] != 'Z' || bytes[1] != 'E' ||
        bytes[2] != 'X' || bytes[3] != '1') {
        throw Error("input is not a complete ZEX1 executable");
    }
    const std::uint32_t version = foreign_u32_le(bytes, 4U);
    const std::uint32_t declared_header = foreign_u32_le(bytes, 8U);
    const std::uint32_t image_size = foreign_u32_le(bytes, 12U);
    const std::uint32_t entry_offset = foreign_u32_le(bytes, 16U);
    const std::uint32_t bss_size = foreign_u32_le(bytes, 20U);
    const std::uint32_t stack_size = foreign_u32_le(bytes, 24U);
    const std::uint32_t expected_checksum = foreign_u32_le(bytes, 28U);
    if (version != 1U || declared_header != header_size || !image_size ||
        image_size > bytes.size() - header_size || bytes.size() != header_size + image_size ||
        entry_offset >= image_size || image_size > zenov_stack_base ||
        bss_size > zenov_stack_base - image_size || stack_size > zenov_stack_bytes ||
        (bss_size && (image_size & (zenov_page_size - 1U)))) {
        throw Error("ZEX1 executable is incompatible with the ZenovOS 0.1.1 loader");
    }
    const auto* image = bytes.data() + header_size;
    if (zex_checksum(image, image_size) != expected_checksum) {
        throw Error("ZEX1 image checksum mismatch");
    }
}

struct ForeignElfSegment final {
    std::uint32_t type = 0;
    std::uint32_t offset = 0;
    std::uint32_t virtual_address = 0;
    std::uint32_t file_size = 0;
    std::uint32_t memory_size = 0;
    std::uint32_t flags = 0;
    std::uint32_t alignment = 0;
};

inline bool foreign_ranges_overlap(std::uint32_t left, std::uint32_t left_size,
                                   std::uint32_t right, std::uint32_t right_size) {
    return left < right + right_size && right < left + left_size;
}

inline bool foreign_page_ranges_overlap(std::uint32_t left, std::uint32_t left_size,
                                        std::uint32_t right, std::uint32_t right_size) {
    const std::uint32_t left_begin = left & ~(zenov_page_size - 1U);
    const std::uint32_t left_end = (left + left_size + zenov_page_size - 1U) & ~(zenov_page_size - 1U);
    const std::uint32_t right_begin = right & ~(zenov_page_size - 1U);
    const std::uint32_t right_end = (right + right_size + zenov_page_size - 1U) & ~(zenov_page_size - 1U);
    return left_begin < right_end && right_begin < left_end;
}

inline void validate_elf32_i386_import(const std::vector<std::uint8_t>& bytes) {
    constexpr std::uint32_t elf_header_size = 52U;
    constexpr std::uint32_t program_header_size = 32U;
    if (bytes.size() < elf_header_size || bytes[0] != 0x7fU || bytes[1] != 'E' ||
        bytes[2] != 'L' || bytes[3] != 'F' || bytes[4] != 1U || bytes[5] != 1U ||
        bytes[6] != 1U) {
        throw Error("input is not a little-endian ELF32 executable");
    }
    const std::uint16_t type = foreign_u16_le(bytes, 16U);
    const std::uint16_t machine = foreign_u16_le(bytes, 18U);
    const std::uint32_t version = foreign_u32_le(bytes, 20U);
    const std::uint32_t entry = foreign_u32_le(bytes, 24U);
    const std::uint32_t program_offset = foreign_u32_le(bytes, 28U);
    const std::uint16_t header_size = foreign_u16_le(bytes, 40U);
    const std::uint16_t program_entry_size = foreign_u16_le(bytes, 42U);
    const std::uint16_t program_count = foreign_u16_le(bytes, 44U);
    if (type != 2U || machine != 3U || version != 1U || header_size != elf_header_size ||
        program_entry_size != program_header_size || !program_count || entry >= zenov_stack_base ||
        program_offset > bytes.size() ||
        program_count > (bytes.size() - program_offset) / program_header_size) {
        throw Error("ELF is not an ET_EXEC ELF32/i386 image accepted by ZenovOS 0.1.1");
    }

    std::vector<ForeignElfSegment> loaded;
    bool executable_entry = false;
    for (std::uint32_t i = 0; i < program_count; ++i) {
        const std::size_t base = static_cast<std::size_t>(program_offset) +
                                 static_cast<std::size_t>(i) * program_header_size;
        ForeignElfSegment segment;
        segment.type = foreign_u32_le(bytes, base);
        segment.offset = foreign_u32_le(bytes, base + 4U);
        segment.virtual_address = foreign_u32_le(bytes, base + 8U);
        segment.file_size = foreign_u32_le(bytes, base + 16U);
        segment.memory_size = foreign_u32_le(bytes, base + 20U);
        segment.flags = foreign_u32_le(bytes, base + 24U);
        segment.alignment = foreign_u32_le(bytes, base + 28U);
        if (segment.type == 2U || segment.type == 3U) {
            throw Error("dynamic ELF and PT_INTERP executables cannot be imported");
        }
        if (segment.type != 1U) continue;
        if (!segment.memory_size) {
            if (segment.file_size) throw Error("ELF has a zero-memory load segment with file data");
            continue;
        }
        if (segment.file_size > segment.memory_size || segment.offset > bytes.size() ||
            segment.file_size > bytes.size() - segment.offset ||
            segment.virtual_address >= zenov_stack_base ||
            segment.memory_size > zenov_stack_base - segment.virtual_address ||
            (segment.flags & ~7U) || ((segment.flags & 3U) == 3U) ||
            (segment.alignment > 1U &&
             ((segment.alignment & (segment.alignment - 1U)) ||
              (segment.virtual_address & (segment.alignment - 1U)) !=
                  (segment.offset & (segment.alignment - 1U))))) {
            throw Error("ELF load segment violates the ZenovOS loader contract");
        }
        for (const auto& other : loaded) {
            if (foreign_ranges_overlap(segment.virtual_address, segment.memory_size,
                                       other.virtual_address, other.memory_size) ||
                (((segment.flags ^ other.flags) & 2U) &&
                 foreign_page_ranges_overlap(segment.virtual_address, segment.memory_size,
                                             other.virtual_address, other.memory_size))) {
                throw Error("ELF load segments overlap or require conflicting page permissions");
            }
        }
        if ((segment.flags & 1U) && !(segment.flags & 2U) &&
            entry >= segment.virtual_address && entry < segment.virtual_address + segment.memory_size) {
            executable_entry = true;
        }
        loaded.push_back(segment);
    }
    if (loaded.empty() || !executable_entry) {
        throw Error("ELF has no loadable executable segment containing its entrypoint");
    }
}

inline const char* foreign_format_id(package_foreign::Format format) {
    using package_foreign::Format;
    switch (format) {
        case Format::zenpkg: return "zenpkg";
        case Format::zex1: return "zex1";
        case Format::elf: return "elf";
        case Format::appimage: return "appimage";
        case Format::portable_executable: return "pe";
        case Format::msi: return "msi";
        case Format::msix: return "msix";
        case Format::deb: return "deb";
        case Format::rpm: return "rpm";
        case Format::snap: return "snap";
        case Format::flatpak: return "flatpak";
        case Format::apple_pkg: return "apple-pkg";
        case Format::apple_dmg: return "apple-dmg";
        case Format::macho: return "macho";
        case Format::xbox_xvc: return "xbox-xvc";
        case Format::xbox_msixvc: return "xbox-msixvc";
        case Format::playstation_pkg: return "playstation-pkg";
        case Format::zip: return "zip";
        case Format::unknown: return "unknown";
    }
    return "unknown";
}

struct ForeignProbe final {
    std::vector<std::uint8_t> bytes;
    package_foreign::Detection detection;
    std::string sha256;
};

inline ForeignProbe probe_foreign(const std::filesystem::path& path) {
    auto bytes = read_binary(path);
    if (bytes.size() > 0xffffffffULL) throw Error("file exceeds the 32-bit foreign probe limit");
    const std::string filename = path.filename().string();
    const auto detection = package_foreign::detect(
        bytes.data(), static_cast<std::uint32_t>(bytes.size()), filename.c_str());
    const auto digest = sha256_hex(bytes);
    return ForeignProbe{std::move(bytes), detection, digest};
}

inline bool kernel_safe_package_name(std::string_view value) {
    if (value.empty() || value.size() > 31U || !std::isalnum(static_cast<unsigned char>(value.front())) ||
        !std::isalnum(static_cast<unsigned char>(value.back())) || value.find("..") != std::string_view::npos) {
        return false;
    }
    for (const unsigned char ch : value) {
        if (!((ch >= 'a' && ch <= 'z') || std::isdigit(ch) || ch == '.' || ch == '-' || ch == '_')) return false;
    }
    return true;
}

inline bool kernel_safe_package_version(std::string_view value) {
    if (value.empty() || value.size() > 15U) return false;
    for (const unsigned char ch : value) {
        if (!(std::isdigit(ch) || ch == '.' || ch == '-' || ch == '+' || ch == '_')) return false;
    }
    return true;
}

inline Package import_native(const std::filesystem::path& input_path, const std::string& name,
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

    const auto probe = probe_foreign(input_path);
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
        throw Error(std::string("format ") + foreign_format_id(probe.detection.format) +
                    " is not eligible for native import; support=" +
                    package_foreign::support_text(probe.detection.support));
    }
    if (manifest.entrypoint.size() > 47U) {
        throw Error("name and version produce an entrypoint longer than the 0.1.1 kernel path limit");
    }

    const auto package_bytes = build_package_bytes(manifest, probe.bytes);
    if (package_bytes.size() > zenov_package_limit) {
        throw Error("imported package exceeds the current 64 KiB ZenovFS/package-manager limit");
    }
    write_binary_atomic(output_path, package_bytes);
    return read_package(output_path);
}

} // namespace zenpkg
