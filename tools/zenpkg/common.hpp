#pragma once

// SPDX-License-Identifier: BSD-2-Clause

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace zenpkg {

class Error final : public std::runtime_error {
public:
    explicit Error(const std::string& message) : std::runtime_error(message) {}
};

inline std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch) != 0; }).base();
    if (first >= last) return {};
    return std::string(first, last);
}

inline bool starts_with(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

inline std::vector<std::uint8_t> read_binary(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw Error("cannot open file for reading: " + path.string());
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end < 0) throw Error("cannot determine file size: " + path.string());
    if (static_cast<unsigned long long>(end) > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
        throw Error("file is too large for this host: " + path.string());
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!data.empty()) {
        stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!stream) throw Error("short read: " + path.string());
    }
    return data;
}

inline std::string read_text(const std::filesystem::path& path) {
    const auto bytes = read_binary(path);
    return std::string(bytes.begin(), bytes.end());
}

inline void write_binary_atomic(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
    const auto parent = path.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) throw Error("cannot open temporary file for writing: " + temporary);
        if (!data.empty()) stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        stream.flush();
        if (!stream) throw Error("write failed: " + temporary);
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temporary, path, ec);
        if (ec) {
            std::filesystem::remove(temporary);
            throw Error("cannot replace output file: " + path.string() + ": " + ec.message());
        }
    }
}

inline void write_text_atomic(const std::filesystem::path& path, const std::string& text) {
    write_binary_atomic(path, std::vector<std::uint8_t>(text.begin(), text.end()));
}

inline std::string hex_encode(const std::uint8_t* data, std::size_t size) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < size; ++i) output << std::setw(2) << static_cast<unsigned>(data[i]);
    return output.str();
}

inline bool is_safe_identifier(std::string_view value) {
    if (value.empty() || value.size() > 64) return false;
    if (!std::isalnum(static_cast<unsigned char>(value.front())) ||
        !std::isalnum(static_cast<unsigned char>(value.back())) ||
        value.find("..") != std::string_view::npos) return false;
    for (const unsigned char ch : value) {
        if (!(std::isalnum(ch) || ch == '.' || ch == '-' || ch == '_')) return false;
    }
    return true;
}

inline bool is_printable_text(std::string_view value, std::size_t maximum) {
    if (value.empty() || value.size() > maximum) return false;
    for (const unsigned char ch : value) {
        if (ch < 0x20U || ch == 0x7FU) return false;
    }
    return true;
}

inline bool is_safe_version(std::string_view value) {
    if (value.empty() || value.size() > 64) return false;
    for (const unsigned char ch : value) {
        if (!(std::isalnum(ch) || ch == '.' || ch == '-' || ch == '+' || ch == '_')) return false;
    }
    return true;
}

inline void append_u32_le(std::vector<std::uint8_t>& output, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) output.push_back(static_cast<std::uint8_t>(value >> shift));
}

inline void append_u64_le(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) output.push_back(static_cast<std::uint8_t>(value >> shift));
}

inline std::uint32_t read_u32_le(const std::vector<std::uint8_t>& input, std::size_t offset) {
    if (offset > input.size() || input.size() - offset < 4) throw Error("truncated package header");
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(input[offset + i]) << (i * 8);
    return value;
}

inline std::uint64_t read_u64_le(const std::vector<std::uint8_t>& input, std::size_t offset) {
    if (offset > input.size() || input.size() - offset < 8) throw Error("truncated package header");
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(input[offset + i]) << (i * 8);
    return value;
}

inline std::string join(const std::vector<std::string>& values, std::string_view separator) {
    std::ostringstream output;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) output << separator;
        output << values[i];
    }
    return output.str();
}

} // namespace zenpkg
