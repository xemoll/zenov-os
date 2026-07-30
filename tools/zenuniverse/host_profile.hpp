#pragma once

#include "common.hpp"

namespace zenuniverse {

struct HostProfile {
    std::string id;
    std::string architecture;
    std::string description;
    std::set<std::string> capabilities;
};

const std::map<std::string, HostProfile>& builtin_host_profiles() {
    static const std::map<std::string, HostProfile> profiles = {
        {
            "zenov-0.1.1-i686",
            HostProfile{
                "zenov-0.1.1-i686",
                "x86",
                "Current ZenovOS 0.1.1 BIOS/i686 single-foreground-process runtime substrate.",
                {
                    "abi.zenov.zex1",
                    "cpu.x86",
                    "graphics.framebuffer",
                    "input.keyboard",
                    "input.mouse",
                    "kernel.single-foreground-process",
                    "loader.elf32-static",
                    "storage.small-files",
                    "storage.zenovfs"
                }
            }
        }
    };
    return profiles;
}

HostProfile require_host_profile(const std::string& id) {
    const auto& profiles = builtin_host_profiles();
    const auto it = profiles.find(id);
    if (it == profiles.end()) throw Error("unknown host profile: " + id);
    return it->second;
}

} // namespace zenuniverse
