#pragma once

// SPDX-License-Identifier: BSD-2-Clause

#include "common.hpp"

#include <map>

namespace zenpkg {

struct Manifest final {
    std::string format;
    std::string name;
    std::string version;
    std::string architecture;
    std::string target;
    std::string kind;
    std::string entrypoint;
    std::string payload_type;
    std::string runtime;
    std::string min_os;
    std::string license;
    std::string source;
    std::string asset_policy;
    std::vector<std::string> capabilities;
    std::vector<std::string> dependencies;
    std::vector<std::string> conflicts;
};

inline bool is_allowed_kind(std::string_view value) {
    static const std::set<std::string> allowed = {
        "native", "application", "runtime", "compat-profile", "firmware-descriptor"
    };
    return allowed.count(std::string(value)) != 0;
}

inline bool is_allowed_payload_type(std::string_view value) {
    static const std::set<std::string> allowed = {
        "zex1", "elf32", "elf64", "tar", "runtime-bundle", "metadata", "none"
    };
    return allowed.count(std::string(value)) != 0;
}

inline bool is_allowed_runtime(std::string_view value) {
    static const std::set<std::string> allowed = {
        "native", "wine", "proton", "qemu-user", "qemu-system", "darling",
        "pcsx2", "rpcs3", "xemu", "xenia", "external"
    };
    return allowed.count(std::string(value)) != 0;
}

inline bool is_safe_absolute_entrypoint(std::string_view value) {
    if (value.empty()) return false;
    if (value == "-") return true;
    if (value.front() != '/' || value.size() > 127 || value.back() == '/') return false;
    std::size_t start = 1;
    while (start <= value.size()) {
        const auto slash = value.find('/', start);
        const auto end = slash == std::string_view::npos ? value.size() : slash;
        const auto component = value.substr(start, end - start);
        if (component.empty() || component == "." || component == "..") return false;
        for (const unsigned char ch : component) {
            if (ch < 0x20U || ch == 0x7FU || ch == '\\' || ch == ':') return false;
        }
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    return true;
}

inline void validate_requirement(std::string_view value, const char* field) {
    if (value.empty() || value.size() > 96) throw Error(std::string(field) + " entry has invalid length");
    for (const unsigned char ch : value) {
        if (!(std::isalnum(ch) || ch == '.' || ch == '-' || ch == '_' || ch == ':' || ch == '@' || ch == '<' || ch == '>' || ch == '=' || ch == '+')) {
            throw Error(std::string(field) + " contains an unsupported character: " + std::string(value));
        }
    }
}

inline Manifest parse_manifest(std::string_view input) {
    Manifest manifest;
    std::map<std::string, std::string*> scalar_fields = {
        {"format", &manifest.format}, {"name", &manifest.name}, {"version", &manifest.version},
        {"architecture", &manifest.architecture}, {"target", &manifest.target}, {"kind", &manifest.kind},
        {"entrypoint", &manifest.entrypoint}, {"payload_type", &manifest.payload_type},
        {"runtime", &manifest.runtime}, {"min_os", &manifest.min_os}, {"license", &manifest.license},
        {"source", &manifest.source}, {"asset_policy", &manifest.asset_policy}
    };
    std::set<std::string> seen_scalars;
    std::istringstream stream{std::string(input)};
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(stream, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto cleaned = trim(line);
        if (cleaned.empty() || cleaned.front() == '#') continue;
        const auto equal = cleaned.find('=');
        if (equal == std::string::npos || equal == 0) {
            throw Error("manifest line " + std::to_string(line_number) + " must use key=value syntax");
        }
        const auto key = trim(cleaned.substr(0, equal));
        const auto value = trim(cleaned.substr(equal + 1));
        if (value.empty()) throw Error("manifest line " + std::to_string(line_number) + " has an empty value");
        if (key == "capability") manifest.capabilities.push_back(value);
        else if (key == "dependency") manifest.dependencies.push_back(value);
        else if (key == "conflict") manifest.conflicts.push_back(value);
        else {
            const auto found = scalar_fields.find(key);
            if (found == scalar_fields.end()) throw Error("unknown manifest key on line " + std::to_string(line_number) + ": " + key);
            if (!seen_scalars.insert(key).second) throw Error("duplicate manifest key: " + key);
            *found->second = value;
        }
    }
    return manifest;
}

inline void validate_manifest(const Manifest& manifest, std::uint64_t payload_size) {
    if (manifest.format != "zenpkg-manifest-1") throw Error("format must be zenpkg-manifest-1");
    if (!is_safe_identifier(manifest.name)) throw Error("name must be a 1-64 character safe identifier");
    if (!is_safe_version(manifest.version)) throw Error("version contains unsupported characters");
    if (!is_safe_identifier(manifest.architecture)) throw Error("architecture must be a safe identifier");
    if (!is_safe_identifier(manifest.target)) throw Error("target must be a safe identifier");
    if (!is_allowed_kind(manifest.kind)) throw Error("unsupported package kind: " + manifest.kind);
    if (!is_safe_absolute_entrypoint(manifest.entrypoint)) throw Error("entrypoint must be '-' or a safe absolute path");
    if (!is_allowed_payload_type(manifest.payload_type)) throw Error("unsupported payload_type: " + manifest.payload_type);
    if (!is_allowed_runtime(manifest.runtime)) throw Error("unsupported runtime: " + manifest.runtime);
    if (!is_safe_version(manifest.min_os)) throw Error("min_os contains unsupported characters");
    if (!is_printable_text(manifest.license, 96)) throw Error("license is required, printable and must fit 96 characters");
    if (!is_printable_text(manifest.source, 256)) throw Error("source is required, printable and must fit 256 characters");
    if (manifest.asset_policy != "redistributable" && manifest.asset_policy != "user-supplied" && manifest.asset_policy != "metadata-only") {
        throw Error("asset_policy must be redistributable, user-supplied, or metadata-only");
    }
    if (manifest.payload_type == "none" && payload_size != 0) throw Error("payload_type=none requires an empty payload");
    if (manifest.payload_type != "none" && payload_size == 0) throw Error("non-empty payload_type requires payload bytes");
    if ((manifest.kind == "compat-profile" || manifest.kind == "firmware-descriptor") && manifest.entrypoint != "-") {
        throw Error("metadata-only package kinds must use entrypoint=-");
    }
    if ((manifest.kind == "application" || manifest.kind == "native" || manifest.kind == "runtime") && manifest.entrypoint == "-") {
        throw Error("executable package kinds require an entrypoint");
    }

    std::set<std::string> unique;
    for (const auto& capability : manifest.capabilities) {
        validate_requirement(capability, "capability");
        if (!unique.insert(capability).second) throw Error("duplicate capability: " + capability);
    }
    unique.clear();
    for (const auto& dependency : manifest.dependencies) {
        validate_requirement(dependency, "dependency");
        if (!unique.insert(dependency).second) throw Error("duplicate dependency: " + dependency);
    }
    unique.clear();
    for (const auto& conflict : manifest.conflicts) {
        validate_requirement(conflict, "conflict");
        if (!unique.insert(conflict).second) throw Error("duplicate conflict: " + conflict);
    }
}

inline std::string canonical_manifest(const Manifest& manifest) {
    std::vector<std::string> capabilities = manifest.capabilities;
    std::vector<std::string> dependencies = manifest.dependencies;
    std::vector<std::string> conflicts = manifest.conflicts;
    std::sort(capabilities.begin(), capabilities.end());
    std::sort(dependencies.begin(), dependencies.end());
    std::sort(conflicts.begin(), conflicts.end());

    std::ostringstream output;
    output << "format=" << manifest.format << '\n';
    output << "name=" << manifest.name << '\n';
    output << "version=" << manifest.version << '\n';
    output << "architecture=" << manifest.architecture << '\n';
    output << "target=" << manifest.target << '\n';
    output << "kind=" << manifest.kind << '\n';
    output << "entrypoint=" << manifest.entrypoint << '\n';
    output << "payload_type=" << manifest.payload_type << '\n';
    output << "runtime=" << manifest.runtime << '\n';
    output << "min_os=" << manifest.min_os << '\n';
    output << "license=" << manifest.license << '\n';
    output << "source=" << manifest.source << '\n';
    output << "asset_policy=" << manifest.asset_policy << '\n';
    for (const auto& value : capabilities) output << "capability=" << value << '\n';
    for (const auto& value : dependencies) output << "dependency=" << value << '\n';
    for (const auto& value : conflicts) output << "conflict=" << value << '\n';
    return output.str();
}

inline Manifest parse_and_validate_manifest(std::string_view input, std::uint64_t payload_size) {
    auto manifest = parse_manifest(input);
    validate_manifest(manifest, payload_size);
    return manifest;
}

struct Resolution final {
    bool compatible = false;
    std::vector<std::string> reasons;
};

inline std::vector<std::uint64_t> parse_numeric_version(std::string_view value) {
    const auto suffix = value.find_first_of("-+");
    const auto core = value.substr(0, suffix);
    std::vector<std::uint64_t> parts;
    std::size_t start = 0;
    while (start <= core.size()) {
        const auto dot = core.find('.', start);
        const auto end = dot == std::string_view::npos ? core.size() : dot;
        const auto component = core.substr(start, end - start);
        if (component.empty()) throw Error("version has an empty numeric component: " + std::string(value));
        std::uint64_t parsed = 0;
        for (const unsigned char ch : component) {
            if (!std::isdigit(ch)) throw Error("version core must be numeric: " + std::string(value));
            if (parsed > (std::numeric_limits<std::uint64_t>::max() - (ch - '0')) / 10U) {
                throw Error("version component overflows: " + std::string(value));
            }
            parsed = parsed * 10U + static_cast<std::uint64_t>(ch - '0');
        }
        parts.push_back(parsed);
        if (dot == std::string_view::npos) break;
        start = dot + 1;
    }
    return parts;
}

inline bool version_at_least(std::string_view current, std::string_view minimum) {
    auto left = parse_numeric_version(current);
    auto right = parse_numeric_version(minimum);
    const auto count = std::max(left.size(), right.size());
    left.resize(count, 0);
    right.resize(count, 0);
    if (left != right) return std::lexicographical_compare(right.begin(), right.end(), left.begin(), left.end());
    const bool current_prerelease = current.find('-') != std::string_view::npos;
    const bool minimum_prerelease = minimum.find('-') != std::string_view::npos;
    if (current_prerelease != minimum_prerelease) return !current_prerelease;
    return current >= minimum;
}

inline Resolution resolve_manifest(const Manifest& manifest, std::string_view architecture,
                                   std::string_view target, std::string_view os_version,
                                   const std::set<std::string>& capabilities) {
    Resolution resolution;
    if (manifest.architecture != "any" && manifest.architecture != architecture) {
        resolution.reasons.push_back("architecture mismatch: package=" + manifest.architecture + " host=" + std::string(architecture));
    }
    if (manifest.target != "any" && manifest.target != target) {
        resolution.reasons.push_back("target mismatch: package=" + manifest.target + " host=" + std::string(target));
    }
    if (!version_at_least(os_version, manifest.min_os)) {
        resolution.reasons.push_back("OS version too old: package requires " + manifest.min_os + " host=" + std::string(os_version));
    }
    for (const auto& required : manifest.capabilities) {
        if (capabilities.count(required) == 0) resolution.reasons.push_back("missing capability: " + required);
    }
    resolution.compatible = resolution.reasons.empty();
    return resolution;
}

} // namespace zenpkg
