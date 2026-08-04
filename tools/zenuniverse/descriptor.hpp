#pragma once

#include "capability.hpp"

namespace zenuniverse {

struct Descriptor {
    std::string schema, id, version, kind, platform, architecture, artifact, delivery, runtime, availability;
    std::string entrypoint, channel, category, license, description, homepage, sha;
    std::string provider_abi, launch_mode;
    std::uint64_t bytes = 0U;
    std::vector<std::string> mirrors, requirements, requirement_any, provides, assets, accepts, launch_args;
    fs::path source;
};

const StringSet kinds = {"application", "game", "runtime", "sdk", "toolchain", "profile", "firmware"};
const StringSet platforms = {"zenov", "linux", "windows", "macos", "playstation1", "playstation-portable", "playstation2", "playstation3", "xbox", "xbox360"};
const StringSet architectures = {"any", "x86", "x86_64", "arm64", "ppc64"};
const StringSet artifacts = {"zpk", "zex1", "elf32", "elf64", "appimage", "flatpak", "deb", "rpm", "tar", "exe", "msi", "msix", "appx", "dmg", "pkg", "app", "iso", "disc-image", "rom", "psx-exe", "bin-cue", "chd", "pbp", "cso", "runtime-bundle", "metadata"};
const StringSet deliveries = {"builtin", "embedded", "https", "user-supplied", "metadata-only"};
const StringSet availability_values = {"available", "planned", "external"};
const StringSet launch_modes = {"builtin", "exec", "external"};

std::uint64_t parse_u64(const std::string& value, const std::string& field) {
    if (value.empty()) throw Error(field + " is empty");
    if (value.size() > 1U && value.front() == '0') throw Error(field + " must be canonical decimal");
    std::uint64_t result = 0U;
    for (const unsigned char c : value) {
        if (!std::isdigit(c)) throw Error(field + " must be decimal");
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) throw Error(field + " overflows");
        result = result * 10U + digit;
    }
    return result;
}

bool safe_runtime_entrypoint(std::string_view value) {
    if (value == "@kernel-loader") return true;
    if (value.size() < 2U || value.size() > 192U || value.front() != '/') return false;
    if (value.find("//") != std::string_view::npos || value.find("/../") != std::string_view::npos ||
        value.find("/./") != std::string_view::npos || value.back() == '/') return false;
    for (const unsigned char c : value) if (c < 0x21U || c > 0x7EU || c == '\\') return false;
    return true;
}

bool valid_launch_argument(const std::string& value, const StringSet& assets) {
    if (!printable(value, 192U)) return false;
    if (value == "%artifact%") return true;
    constexpr std::string_view prefix = "%asset:";
    if (value.rfind(prefix, 0U) == 0U && value.size() > prefix.size() + 1U && value.back() == '%') {
        const auto id = value.substr(prefix.size(), value.size() - prefix.size() - 1U);
        return assets.count(id) != 0U;
    }
    return value.find('%') == std::string::npos;
}

Descriptor parse_descriptor(const fs::path& path) {
    Descriptor descriptor;
    descriptor.source = path;
    StringMap<std::string*> scalar = {
        {"schema", &descriptor.schema}, {"id", &descriptor.id}, {"version", &descriptor.version},
        {"kind", &descriptor.kind}, {"platform", &descriptor.platform}, {"architecture", &descriptor.architecture},
        {"artifact", &descriptor.artifact}, {"delivery", &descriptor.delivery}, {"runtime", &descriptor.runtime},
        {"availability", &descriptor.availability}, {"entrypoint", &descriptor.entrypoint},
        {"channel", &descriptor.channel}, {"category", &descriptor.category}, {"license", &descriptor.license},
        {"description", &descriptor.description}, {"homepage", &descriptor.homepage}, {"sha256", &descriptor.sha},
        {"provider-abi", &descriptor.provider_abi}, {"launch-mode", &descriptor.launch_mode}
    };
    StringSet seen;
    std::istringstream input(read_text_bounded(path, 64U * 1024U));
    std::string line;
    std::size_t line_number = 0U;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        if (line == ".") continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos || equals == 0U) throw Error(path.string() + ":" + std::to_string(line_number) + ": expected key=value");
        const auto key = trim(line.substr(0U, equals));
        const auto value = trim(line.substr(equals + 1U));
        if (value.empty()) throw Error(path.string() + ":" + std::to_string(line_number) + ": empty value");
        if (key == "mirror") descriptor.mirrors.push_back(value);
        else if (key == "requires") descriptor.requirements.push_back(value);
        else if (key == "requires-any") descriptor.requirement_any.push_back(value);
        else if (key == "provides") descriptor.provides.push_back(value);
        else if (key == "asset") descriptor.assets.push_back(value);
        else if (key == "accepts") descriptor.accepts.push_back(value);
        else if (key == "arg") descriptor.launch_args.push_back(value);
        else if (key == "bytes") {
            if (!seen.insert(key).second) throw Error("duplicate bytes");
            descriptor.bytes = parse_u64(value, "bytes");
        } else {
            const auto it = scalar.find(key);
            if (it == scalar.end()) throw Error(path.string() + ":" + std::to_string(line_number) + ": unknown key " + key);
            if (!seen.insert(key).second) throw Error("duplicate " + key);
            *it->second = value;
        }
    }
    return descriptor;
}

std::string runtime_capability(const Descriptor& descriptor) {
    return "runtime." + descriptor.runtime;
}

void validate_unique_capabilities(const std::vector<std::string>& values, const std::string& field, const Descriptor& descriptor) {
    StringSet unique;
    for (const auto& value : values) {
        if (!known_capability(value) || !unique.insert(value).second) {
            throw Error("unknown or duplicate " + field + " in " + descriptor.source.string() + ": " + value);
        }
    }
}

void validate_descriptor(const Descriptor& descriptor) {
    if (descriptor.schema != "zen-source-1") throw Error("schema must be zen-source-1: " + descriptor.source.string());
    if (!safe_id(descriptor.id) || !safe_id(descriptor.version)) throw Error("unsafe id or version: " + descriptor.source.string());
    if (!kinds.count(descriptor.kind) || !platforms.count(descriptor.platform) || !architectures.count(descriptor.architecture) ||
        !artifacts.count(descriptor.artifact) || !deliveries.count(descriptor.delivery) ||
        !runtime_names().count(descriptor.runtime) || !availability_values.count(descriptor.availability)) {
        throw Error("unsupported enum in " + descriptor.source.string());
    }
    if (!safe_id(descriptor.channel) || !safe_id(descriptor.category) || !printable(descriptor.license, 96U) ||
        !printable(descriptor.description, 240U) || !https_url(descriptor.homepage)) {
        throw Error("invalid metadata in " + descriptor.source.string());
    }
    if (descriptor.entrypoint.empty() || descriptor.entrypoint.size() > 192U || descriptor.entrypoint.find_first_of("\r\n") != std::string::npos) {
        throw Error("invalid entrypoint in " + descriptor.source.string());
    }

    validate_unique_capabilities(descriptor.requirements, "requires", descriptor);
    validate_unique_capabilities(descriptor.provides, "provides", descriptor);
    StringSet any_groups;
    for (const auto& group : descriptor.requirement_any) {
        const auto canonical = canonical_alternatives(group);
        if (!any_groups.insert(canonical).second) throw Error("duplicate requires-any in " + descriptor.source.string());
    }

    StringSet unique;
    for (const auto& value : descriptor.assets) {
        if (!safe_id(value) || !unique.insert(value).second) throw Error("invalid or duplicate asset in " + descriptor.source.string());
    }
    const auto asset_set = StringSet(descriptor.assets.begin(), descriptor.assets.end());
    unique.clear();
    for (const auto& value : descriptor.accepts) {
        if (!artifacts.count(value) || !unique.insert(value).second) throw Error("invalid or duplicate accepts in " + descriptor.source.string());
    }
    for (const auto& value : descriptor.launch_args) {
        if (!valid_launch_argument(value, asset_set)) throw Error("invalid launch argument in " + descriptor.source.string() + ": " + value);
    }
    unique.clear();
    for (const auto& value : descriptor.mirrors) {
        if (!https_url(value) || !unique.insert(value).second) throw Error("invalid or duplicate HTTPS mirror in " + descriptor.source.string());
    }

    if (descriptor.delivery == "https") {
        if (descriptor.mirrors.empty() || !hex64(descriptor.sha) || descriptor.bytes == 0U) throw Error("https delivery requires mirror, bytes and lowercase sha256: " + descriptor.source.string());
    } else if (descriptor.delivery == "embedded") {
        if (!descriptor.mirrors.empty() || !hex64(descriptor.sha) || descriptor.bytes == 0U) throw Error("embedded delivery requires bytes/sha256 and no mirrors: " + descriptor.source.string());
    } else if (descriptor.delivery == "builtin") {
        if (!descriptor.mirrors.empty() || descriptor.sha != "-" || descriptor.bytes != 0U || descriptor.kind != "runtime") throw Error("builtin delivery is reserved for zero-byte runtime substrate: " + descriptor.source.string());
    } else if (descriptor.delivery == "metadata-only") {
        if (!descriptor.mirrors.empty() || descriptor.sha != "-" || descriptor.bytes != 0U) throw Error("metadata-only requires bytes=0 sha256=- and no mirrors: " + descriptor.source.string());
    } else if (descriptor.delivery == "user-supplied") {
        if (!descriptor.mirrors.empty() || descriptor.sha != "-" || descriptor.bytes != 0U) throw Error("user-supplied content cannot publish mirrors or fixed bytes: " + descriptor.source.string());
    }

    const StringSet console_platforms = {"playstation1", "playstation-portable", "playstation2", "playstation3", "xbox", "xbox360"};
    if (console_platforms.count(descriptor.platform) && descriptor.kind == "game" && descriptor.delivery != "user-supplied") {
        throw Error("console game content must be user-supplied: " + descriptor.source.string());
    }
    const StringMap<StringSet> allowed = {
        {"windows", {"exe", "msi", "msix", "appx", "runtime-bundle", "metadata"}},
        {"linux", {"elf32", "elf64", "appimage", "flatpak", "deb", "rpm", "tar", "runtime-bundle", "metadata"}},
        {"macos", {"dmg", "pkg", "app", "runtime-bundle", "metadata"}},
        {"playstation1", {"psx-exe", "bin-cue", "chd", "pbp", "iso", "disc-image", "metadata"}},
        {"playstation-portable", {"iso", "disc-image", "pbp", "cso", "metadata"}},
        {"playstation2", {"iso", "disc-image", "cso", "metadata"}},
        {"playstation3", {"iso", "disc-image", "metadata"}},
        {"xbox", {"iso", "disc-image", "rom", "metadata"}},
        {"xbox360", {"iso", "disc-image", "rom", "metadata"}},
        {"zenov", {"zpk", "zex1", "elf32", "runtime-bundle", "metadata"}}
    };
    if (!allowed.at(descriptor.platform).count(descriptor.artifact)) throw Error("artifact/platform mismatch: " + descriptor.source.string());

    const StringMap<StringSet> platform_runtime = {
        {"windows", {"wine", "proton", "qemu-system", "external"}},
        {"linux", {"linux-i386-minimal", "linux-abi", "qemu-user", "qemu-system", "external"}},
        {"macos", {"darling", "qemu-system", "external"}},
        {"playstation1", {"duckstation", "psx-r3000a-diagnostic", "external"}},
        {"playstation-portable", {"ppsspp", "external"}},
        {"playstation2", {"pcsx2", "external"}},
        {"playstation3", {"rpcs3", "external"}},
        {"xbox", {"xemu", "external"}},
        {"xbox360", {"xenia", "external"}},
        {"zenov", {"native", "external"}}
    };
    if (descriptor.kind != "runtime" && !platform_runtime.at(descriptor.platform).count(descriptor.runtime)) {
        throw Error("runtime/platform mismatch: " + descriptor.source.string());
    }

    if (descriptor.kind == "runtime") {
        if (descriptor.runtime != "native") throw Error("runtime provider itself must execute natively: " + descriptor.source.string());
        if (descriptor.provider_abi != "zen-runtime-provider-1") throw Error("runtime provider must declare provider-abi=zen-runtime-provider-1: " + descriptor.source.string());
        if (!launch_modes.count(descriptor.launch_mode)) throw Error("runtime provider has invalid launch-mode: " + descriptor.source.string());
        if (descriptor.accepts.empty()) throw Error("runtime provider must declare accepted artifacts: " + descriptor.source.string());
        if (!safe_runtime_entrypoint(descriptor.entrypoint)) throw Error("runtime provider entrypoint must be absolute or @kernel-loader: " + descriptor.source.string());
        if (descriptor.provides.size() != 1U || descriptor.provides.front().rfind("runtime.", 0U) != 0U) {
            throw Error("runtime provider must provide exactly one runtime capability: " + descriptor.source.string());
        }
        const auto expected_suffix = descriptor.id.substr(descriptor.id.find_last_of('.') + 1U);
        if (descriptor.provides.front() != "runtime." + expected_suffix) throw Error("runtime provider id/capability mismatch: " + descriptor.source.string());
        if (descriptor.delivery == "builtin") {
            if (descriptor.availability != "available" || descriptor.launch_mode != "builtin" || descriptor.entrypoint != "@kernel-loader") {
                throw Error("builtin runtime must be available through @kernel-loader: " + descriptor.source.string());
            }
        } else if (descriptor.availability == "available" && descriptor.delivery != "embedded" && descriptor.delivery != "https") {
            throw Error("available runtime must contain downloadable, embedded or builtin bytes: " + descriptor.source.string());
        }
        if (descriptor.launch_mode == "builtin" && descriptor.delivery != "builtin") throw Error("builtin launch-mode requires builtin delivery: " + descriptor.source.string());
    } else {
        if (!descriptor.provider_abi.empty() || !descriptor.launch_mode.empty() || !descriptor.accepts.empty() || !descriptor.launch_args.empty()) {
            throw Error("only runtime providers may declare provider ABI, accepts or launch arguments: " + descriptor.source.string());
        }
    }
}

std::string canonical(const Descriptor& descriptor) {
    auto mirrors = descriptor.mirrors;
    auto requirements = descriptor.requirements;
    auto provides = descriptor.provides;
    auto assets = descriptor.assets;
    auto accepts = descriptor.accepts;
    auto any_groups = descriptor.requirement_any;
    std::sort(mirrors.begin(), mirrors.end());
    std::sort(requirements.begin(), requirements.end());
    std::sort(provides.begin(), provides.end());
    std::sort(assets.begin(), assets.end());
    std::sort(accepts.begin(), accepts.end());
    for (auto& group : any_groups) group = canonical_alternatives(group);
    std::sort(any_groups.begin(), any_groups.end());
    std::ostringstream out;
    out << "schema=" << descriptor.schema << '\n'
        << "id=" << descriptor.id << '\n'
        << "version=" << descriptor.version << '\n'
        << "kind=" << descriptor.kind << '\n'
        << "platform=" << descriptor.platform << '\n'
        << "architecture=" << descriptor.architecture << '\n'
        << "artifact=" << descriptor.artifact << '\n'
        << "delivery=" << descriptor.delivery << '\n'
        << "runtime=" << descriptor.runtime << '\n'
        << "availability=" << descriptor.availability << '\n'
        << "entrypoint=" << descriptor.entrypoint << '\n';
    if (!descriptor.provider_abi.empty()) out << "provider-abi=" << descriptor.provider_abi << '\n';
    if (!descriptor.launch_mode.empty()) out << "launch-mode=" << descriptor.launch_mode << '\n';
    out << "channel=" << descriptor.channel << '\n'
        << "category=" << descriptor.category << '\n'
        << "license=" << descriptor.license << '\n'
        << "description=" << descriptor.description << '\n'
        << "homepage=" << descriptor.homepage << '\n'
        << "bytes=" << descriptor.bytes << '\n'
        << "sha256=" << descriptor.sha << '\n';
    for (const auto& value : mirrors) out << "mirror=" << value << '\n';
    for (const auto& value : accepts) out << "accepts=" << value << '\n';
    for (const auto& value : assets) out << "asset=" << value << '\n';
    for (const auto& value : requirements) out << "requires=" << value << '\n';
    for (const auto& value : any_groups) out << "requires-any=" << value << '\n';
    for (const auto& value : provides) out << "provides=" << value << '\n';
    for (const auto& value : descriptor.launch_args) out << "arg=" << value << '\n';
    return out.str();
}

std::vector<std::uint64_t> version_parts(std::string_view value) {
    std::vector<std::uint64_t> parts;
    std::size_t position = 0U;
    while (position < value.size()) {
        const auto end = value.find('.', position);
        const auto part = value.substr(position, end == std::string_view::npos ? value.size() - position : end - position);
        std::uint64_t number = 0U;
        bool any = false;
        for (const unsigned char c : part) {
            if (!std::isdigit(c)) break;
            any = true;
            const auto digit = static_cast<std::uint64_t>(c - '0');
            if (number > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) { number = std::numeric_limits<std::uint64_t>::max(); break; }
            number = number * 10U + digit;
        }
        parts.push_back(any ? number : 0U);
        if (end == std::string_view::npos) break;
        position = end + 1U;
    }
    return parts;
}

bool version_less(const Descriptor& left, const Descriptor& right) {
    auto a = version_parts(left.version);
    auto b = version_parts(right.version);
    a.resize(std::max(a.size(), b.size()));
    b.resize(a.size());
    return a == b ? left.version < right.version : std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
}

std::vector<Descriptor> load_directory(const fs::path& directory) {
    if (!fs::is_directory(directory)) throw Error("not a directory: " + directory.string());
    std::vector<fs::path> paths;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".zsource") paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());
    std::vector<Descriptor> records;
    StringSet identities;
    for (const auto& path : paths) {
        auto descriptor = parse_descriptor(path);
        validate_descriptor(descriptor);
        if (!identities.insert(descriptor.id + "@" + descriptor.version).second) throw Error("duplicate identity: " + descriptor.id + "@" + descriptor.version);
        records.push_back(std::move(descriptor));
    }
    if (records.empty()) throw Error("catalog has no .zsource descriptors");
    return records;
}

} // namespace zenuniverse
