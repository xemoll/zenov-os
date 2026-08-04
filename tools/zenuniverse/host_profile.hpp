#pragma once

#include "capability.hpp"

namespace zenuniverse {

struct HostProfile {
    std::string schema;
    std::string id;
    std::string architecture;
    std::string description;
    StringSet capabilities;
    std::uint64_t artifact_bytes_limit = 0U;
    std::uint32_t process_limit = 0U;
    std::uint32_t thread_limit = 0U;
};

void validate_host_profile(const HostProfile& profile) {
    if (profile.schema != "ZENHOST1" || !safe_id(profile.id) || !architectures.count(profile.architecture)) throw Error("invalid built-in host profile metadata");
    if (!printable(profile.description, 240U) || profile.artifact_bytes_limit == 0U || profile.process_limit == 0U || profile.thread_limit == 0U) throw Error("invalid built-in host profile limits");
    for (const auto& capability : profile.capabilities) if (!known_capability(capability)) throw Error("host profile contains unknown capability: " + capability);
    if (!profile.capabilities.count("cpu." + profile.architecture)) throw Error("host profile architecture capability mismatch");
}

const StringMap<HostProfile>& builtin_host_profiles() {
    static const StringMap<HostProfile> profiles = {
        {
            "zenov-0.1.1-i686",
            HostProfile{
                "ZENHOST1",
                "zenov-0.1.1-i686",
                "x86",
                "Current ZenovOS 0.1.1 BIOS/i686 single-foreground-process runtime substrate.",
                {
                    "abi.linux.i386.int80-minimal",
                    "abi.zenov.zex1",
                    "cpu.x86",
                    "graphics.framebuffer",
                    "input.keyboard",
                    "input.mouse",
                    "kernel.single-foreground-process",
                    "loader.elf32-static",
                    "storage.small-files",
                    "storage.zenovfs"
                },
                65536U,
                1U,
                1U
            }
        }
    };
    static const bool validated = [] {
        for (const auto& pair : profiles) validate_host_profile(pair.second);
        return true;
    }();
    (void)validated;
    return profiles;
}

HostProfile require_host_profile(const std::string& id) {
    const auto& profiles = builtin_host_profiles();
    const auto it = profiles.find(id);
    if (it == profiles.end()) throw Error("unknown host profile: " + id);
    return it->second;
}

} // namespace zenuniverse
