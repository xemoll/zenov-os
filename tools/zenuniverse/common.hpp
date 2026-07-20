#pragma once

// SPDX-License-Identifier: BSD-2-Clause
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace zenuniverse {

struct Error final : std::runtime_error { using std::runtime_error::runtime_error; };

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

bool safe_id(std::string_view value) {
    if (value.empty() || value.size() > 96U) return false;
    for (const unsigned char c : value) {
        if (!(std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == '+')) return false;
    }
    return value.front() != '.' && value.back() != '.' && value.find("..") == std::string_view::npos;
}

bool printable(std::string_view value, std::size_t maximum) {
    if (value.empty() || value.size() > maximum) return false;
    for (const unsigned char c : value) if (c < 0x20U || c == 0x7FU) return false;
    return true;
}

bool hex64(std::string_view value) {
    if (value.size() != 64U) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isxdigit(c) && !std::isupper(c); });
}

bool https_url(std::string_view value) {
    if (value.size() < 12U || value.size() > 512U || value.substr(0, 8) != "https://") return false;
    if (value.find_first_of("\r\n\t ") != std::string_view::npos) return false;
    const auto host_end = value.find('/', 8U);
    const auto host = value.substr(8U, host_end == std::string_view::npos ? value.size() - 8U : host_end - 8U);
    return !host.empty() && host.find('.') != std::string_view::npos && host.front() != '.' && host.back() != '.';
}

std::string read_text(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw Error("cannot open: " + path.string());
    std::ostringstream out; out << input.rdbuf();
    if (!input.good() && !input.eof()) throw Error("cannot read: " + path.string());
    return out.str();
}

void write_atomic(const fs::path& path, const std::string& data) {
    const fs::path temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw Error("cannot create: " + temporary.string());
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.flush();
        if (!output) throw Error("cannot write: " + temporary.string());
    }
    std::error_code ec;
    fs::rename(temporary, path, ec);
    if (ec) {
        fs::remove(path, ec); ec.clear(); fs::rename(temporary, path, ec);
        if (ec) throw Error("cannot replace: " + path.string());
    }
}

struct Sha256 {
    std::array<std::uint32_t, 8> state{0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    std::array<std::uint8_t, 64> buffer{};
    std::uint64_t bytes = 0;
    std::size_t used = 0;
    static std::uint32_t rotr(std::uint32_t x, unsigned n) { return (x >> n) | (x << (32U - n)); }
    void block(const std::uint8_t* p) {
        static constexpr std::uint32_t k[64] = {
            0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
            0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
            0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
            0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
            0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
            0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
            0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
            0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U};
        std::uint32_t w[64]{};
        for (int i=0;i<16;++i) w[i]=(std::uint32_t(p[i*4])<<24)|(std::uint32_t(p[i*4+1])<<16)|(std::uint32_t(p[i*4+2])<<8)|p[i*4+3];
        for (int i=16;i<64;++i) { const auto s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3); const auto s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10); w[i]=w[i-16]+s0+w[i-7]+s1; }
        auto a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
        for (int i=0;i<64;++i) { const auto S1=rotr(e,6)^rotr(e,11)^rotr(e,25); const auto ch=(e&f)^((~e)&g); const auto t1=h+S1+ch+k[i]+w[i]; const auto S0=rotr(a,2)^rotr(a,13)^rotr(a,22); const auto maj=(a&b)^(a&c)^(b&c); const auto t2=S0+maj; h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2; }
        state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
    }
    void update(std::string_view data) {
        bytes += data.size();
        for (unsigned char c : data) { buffer[used++]=c; if (used==64U) { block(buffer.data()); used=0; } }
    }
    std::string finish() {
        const std::uint64_t bits = bytes * 8U;
        buffer[used++] = 0x80U;
        if (used > 56U) { while (used<64U) buffer[used++]=0; block(buffer.data()); used=0; }
        while (used<56U) buffer[used++]=0;
        for (int i=7;i>=0;--i) buffer[used++]=static_cast<std::uint8_t>(bits>>(i*8));
        block(buffer.data());
        static constexpr char hex[]="0123456789abcdef";
        std::string out; out.reserve(64);
        for (auto v:state) for (int i=3;i>=0;--i) { auto b=static_cast<unsigned char>(v>>(i*8)); out.push_back(hex[b>>4]); out.push_back(hex[b&15]); }
        return out;
    }
};

std::string sha256(std::string_view data) { Sha256 h; h.update(data); return h.finish(); }

} // namespace zenuniverse
