#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
#pragma pack(push, 1)
struct ZexHeader { char magic[4]; std::uint32_t version, header_size, image_size, entry_offset, bss_size, stack_size, checksum; };
#pragma pack(pop)
static_assert(sizeof(ZexHeader) == 32);
struct Say { std::string text; std::size_t patch; };
std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r"); return value.substr(first, last - first + 1);
}
std::string quoted(const std::string& line, const std::string& prefix) {
    if (line.rfind(prefix, 0) != 0 || line.size() < prefix.size() + 3 || line.back() != ';') throw std::runtime_error("unsupported ZenovOS app statement: " + line);
    std::size_t pos = prefix.size(); if (line[pos++] != '(' || line[pos++] != '"') throw std::runtime_error("expected quoted string: " + line);
    std::string out; bool closed = false;
    for (; pos < line.size(); ++pos) {
        char c = line[pos];
        if (c == '"') { closed = true; ++pos; break; }
        if (c == '\\') {
            if (++pos >= line.size()) throw std::runtime_error("unterminated escape");
            c = line[pos];
            if (c == 'n') out.push_back('\n'); else if (c == 'r') out.push_back('\r'); else if (c == 't') out.push_back('\t');
            else if (c == '\\' || c == '"') out.push_back(c); else throw std::runtime_error("unsupported escape");
        } else out.push_back(c);
    }
    if (!closed || line.substr(pos) != ");") throw std::runtime_error("malformed call: " + line);
    if (out.size() > 4096) throw std::runtime_error("string literal too large");
    return out;
}
void u32(std::vector<std::uint8_t>& out, std::uint32_t value) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(value >> (i * 8))); }
void patch_u32(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t value) { for (int i = 0; i < 4; ++i) out.at(at + i) = static_cast<std::uint8_t>(value >> (i * 8)); }
std::uint32_t fnv1a(const std::vector<std::uint8_t>& data) { std::uint32_t hash = 2166136261U; for (auto byte : data) { hash ^= byte; hash *= 16777619U; } return hash; }
std::vector<std::uint8_t> compile(const std::string& path, std::string& name) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open source: " + path);
    std::vector<std::string> messages; std::string line; int exit_code = 0; bool have_exit = false;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line.rfind("//", 0) == 0 || line[0] == '#') continue;
        if (line.rfind("app", 0) == 0) { if (!name.empty()) throw std::runtime_error("duplicate app declaration"); name = quoted(line, "app"); }
        else if (line.rfind("say", 0) == 0) messages.push_back(quoted(line, "say"));
        else if (line.rfind("exit", 0) == 0) {
            if (line.rfind("exit(", 0) != 0 || line.size() < 8 || line.substr(line.size() - 2) != ");") throw std::runtime_error("malformed exit statement");
            const std::string number = line.substr(5, line.size() - 7); std::size_t used = 0; const long parsed = std::stol(number, &used, 10);
            if (used != number.size() || parsed < 0 || parsed > 255) throw std::runtime_error("exit code must be 0..255");
            exit_code = static_cast<int>(parsed);
            have_exit = true;
        } else throw std::runtime_error("unsupported ZenovOS app statement: " + line);
    }
    if (name.empty()) throw std::runtime_error("missing app declaration");
    if (messages.empty()) throw std::runtime_error("app must emit at least one message");
    if (!have_exit) throw std::runtime_error("missing exit statement");
    std::vector<std::uint8_t> image; std::vector<Say> patches;
    for (const auto& message : messages) {
        image.push_back(0xB8); u32(image, 1); image.push_back(0xBB); const std::size_t patch = image.size(); u32(image, 0);
        image.push_back(0xB9); u32(image, static_cast<std::uint32_t>(message.size())); image.push_back(0xCD); image.push_back(0x80); patches.push_back(Say{message, patch});
    }
    image.push_back(0xBB); u32(image, static_cast<std::uint32_t>(exit_code)); image.push_back(0xB8); u32(image, 0); image.push_back(0xCD); image.push_back(0x80); image.push_back(0x0F); image.push_back(0x0B);
    for (const auto& item : patches) { const std::uint32_t offset = static_cast<std::uint32_t>(image.size()); patch_u32(image, item.patch, offset); image.insert(image.end(), item.text.begin(), item.text.end()); }
    return image;
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 6 || std::string(argv[2]) != "-o" || std::string(argv[4]) != "--abi" || std::string(argv[5]) != "0.1.1") {
            std::cerr << "usage: zenov-app-compiler <source.zv> -o <output.zex> --abi 0.1.1\n"; return 2;
        }
        std::string name; const auto image = compile(argv[1], name);
        ZexHeader header{{'Z','E','X','1'}, 1U, sizeof(ZexHeader), static_cast<std::uint32_t>(image.size()), 0U, 0U, 16384U, fnv1a(image)};
        std::ofstream output(argv[3], std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot open output");
        output.write(reinterpret_cast<const char*>(&header), sizeof(header)); output.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
        if (!output) throw std::runtime_error("cannot write output");
        std::cout << "ZENOV_SOURCE_APP_BUILD_OK app=" << name << " abi=0.1.1 bytes=" << (sizeof(header) + image.size()) << "\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "zenov-app-compiler: " << error.what() << "\n"; return 1; }
}
