#pragma once

#include "common.hpp"

namespace zenuniverse {

const StringSet& runtime_names() {
    static const StringSet values = {
        "native", "linux-i386-minimal", "linux-abi", "wine", "proton", "qemu-user", "qemu-system",
        "darling", "duckstation", "psx-r3000a-diagnostic", "ppsspp", "pcsx2", "rpcs3", "xemu", "xenia", "external"
    };
    return values;
}

const StringSet& static_capabilities() {
    static const StringSet values = {
        "abi.zenov.zex1",
        "abi.linux.i386.int80-minimal",
        "audio.low-latency", "audio.stream",
        "cpu.avx2", "cpu.sse4.1", "cpu.x86", "cpu.x86_64-modern",
        "filesystem.fuse-or-extract", "filesystem.overlay",
        "graphics.framebuffer", "graphics.opengl", "graphics.opengl3.0", "graphics.opengl3.1",
        "graphics.opengl3.3", "graphics.vulkan", "graphics.vulkan1.0", "graphics.vulkan1.1",
        "graphics.vulkan1.3", "graphics.window-system",
        "input.gamepad", "input.keyboard", "input.mouse",
        "kernel.dynamic-linker", "kernel.futex", "kernel.jit", "kernel.mmap", "kernel.namespaces",
        "kernel.processes", "kernel.signals", "kernel.single-foreground-process", "kernel.threads",
        "loader.elf32-static", "loader.elf64-dynamic", "loader.elf64-static",
        "storage.large-files", "storage.small-files", "storage.zenovfs"
    };
    return values;
}

bool known_capability(std::string_view value) {
    if (static_capabilities().count(std::string(value))) return true;
    constexpr std::string_view prefix = "runtime.";
    if (value.substr(0, prefix.size()) != prefix) return false;
    return runtime_names().count(std::string(value.substr(prefix.size()))) != 0U;
}

std::vector<std::string> parse_capability_alternatives(const std::string& value) {
    std::vector<std::string> options;
    std::size_t begin = 0U;
    while (begin <= value.size()) {
        const auto end = value.find('|', begin);
        const auto token = value.substr(begin, end == std::string::npos ? value.size() - begin : end - begin);
        if (!known_capability(token)) throw Error("unknown capability in requires-any: " + token);
        options.push_back(token);
        if (end == std::string::npos) break;
        begin = end + 1U;
    }
    if (options.size() < 2U) throw Error("requires-any must contain at least two capabilities");
    std::sort(options.begin(), options.end());
    if (std::adjacent_find(options.begin(), options.end()) != options.end()) {
        throw Error("requires-any contains duplicate capability");
    }
    return options;
}

std::string canonical_alternatives(const std::string& value) {
    const auto options = parse_capability_alternatives(value);
    std::ostringstream out;
    for (std::size_t i = 0U; i < options.size(); ++i) {
        if (i) out << '|';
        out << options[i];
    }
    return out.str();
}

} // namespace zenuniverse
