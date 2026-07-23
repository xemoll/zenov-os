// SPDX-License-Identifier: BSD-2-Clause

#include "foreign_probe.hpp"

#include <cstdlib>
#include <iostream>
#include <map>

namespace {

using zenpkg::Error;

struct Arguments final {
    std::vector<std::string> positionals;
    std::map<std::string, std::vector<std::string>> options;

    static Arguments parse(int argc, char** argv, int start) {
        Arguments result;
        for (int index = start; index < argc; ++index) {
            std::string token = argv[index];
            if (zenpkg::starts_with(token, "--")) {
                const auto equal = token.find('=');
                std::string key;
                std::string value;
                if (equal != std::string::npos) {
                    key = token.substr(2, equal - 2);
                    value = token.substr(equal + 1);
                } else {
                    key = token.substr(2);
                    if (index + 1 >= argc || zenpkg::starts_with(argv[index + 1], "--")) {
                        throw Error("option --" + key + " requires a value");
                    }
                    value = argv[++index];
                }
                if (key.empty() || value.empty()) throw Error("invalid empty option");
                result.options[key].push_back(value);
            } else {
                result.positionals.push_back(token);
            }
        }
        return result;
    }

    std::string require_one(const std::string& name) const {
        const auto found = options.find(name);
        if (found == options.end() || found->second.size() != 1) throw Error("exactly one --" + name + " is required");
        return found->second.front();
    }

    std::vector<std::string> many(const std::string& name) const {
        const auto found = options.find(name);
        return found == options.end() ? std::vector<std::string>{} : found->second;
    }

    void reject_unknown(const std::set<std::string>& allowed) const {
        for (const auto& [key, values] : options) {
            (void)values;
            if (allowed.count(key) == 0) throw Error("unknown option: --" + key);
        }
    }

    std::string require_positional(const char* label) const {
        if (positionals.size() != 1) throw Error(std::string("exactly one ") + label + " is required");
        return positionals.front();
    }
};

void print_usage(std::ostream& output) {
    output <<
        "zenpkg - deterministic ZenovOS package tooling\n\n"
        "Usage:\n"
        "  zenpkg pack --manifest FILE --payload FILE|- --output FILE.zpk\n"
        "  zenpkg verify FILE.zpk\n"
        "  zenpkg inspect FILE.zpk\n"
        "  zenpkg probe FILE\n"
        "  zenpkg import-native FILE --name NAME --version VERSION --license TEXT --source TEXT --asset-policy redistributable --output FILE.zpk\n"
        "  zenpkg extract FILE.zpk --manifest-output FILE --payload-output FILE\n"
        "  zenpkg resolve FILE.zpk --architecture ARCH --target TARGET --os-version VERSION [--capability NAME ...]\n"
        "  zenpkg index --input DIRECTORY --output INDEX\n"
        "  zenpkg manifest-check FILE --payload-size BYTES\n"
        "  zenpkg hash FILE\n\n"
        "Foreign package probing uses bounded head/tail sampling and a streaming SHA-256.\n"
        "Detection never implies execution compatibility. Native import is limited to validated\n"
        "ZEX1 and static ELF32/i386 payloads and still requires ZenRepo authorization.\n\n"
        "Exit codes: 0 success, 2 invalid package/arguments, 3 incompatible resolution.\n";
}

std::uint64_t parse_u64(const std::string& value, const char* field) {
    if (value.empty()) throw Error(std::string(field) + " is empty");
    std::size_t consumed = 0;
    unsigned long long parsed = 0;
    try {
        parsed = std::stoull(value, &consumed, 10);
    } catch (const std::exception&) {
        throw Error(std::string(field) + " is not an unsigned integer: " + value);
    }
    if (consumed != value.size()) throw Error(std::string(field) + " contains trailing characters: " + value);
    return static_cast<std::uint64_t>(parsed);
}

void print_manifest(const zenpkg::Package& package) {
    const auto& manifest = package.manifest;
    std::cout << "package: " << manifest.name << '@' << manifest.version << '\n';
    std::cout << "kind: " << manifest.kind << '\n';
    std::cout << "target: " << manifest.target << '\n';
    std::cout << "architecture: " << manifest.architecture << '\n';
    std::cout << "runtime: " << manifest.runtime << '\n';
    std::cout << "payload_type: " << manifest.payload_type << '\n';
    std::cout << "entrypoint: " << manifest.entrypoint << '\n';
    std::cout << "min_os: " << manifest.min_os << '\n';
    std::cout << "asset_policy: " << manifest.asset_policy << '\n';
    std::cout << "payload_bytes: " << package.payload.size() << '\n';
    std::cout << "manifest_sha256: " << zenpkg::hex_encode(package.manifest_digest.data(), package.manifest_digest.size()) << '\n';
    std::cout << "payload_sha256: " << zenpkg::hex_encode(package.payload_digest.data(), package.payload_digest.size()) << '\n';
    std::cout << "package_sha256: " << package.package_digest_hex << '\n';
    std::cout << "capabilities: " << (manifest.capabilities.empty() ? "-" : zenpkg::join(manifest.capabilities, ",")) << '\n';
    std::cout << "dependencies: " << (manifest.dependencies.empty() ? "-" : zenpkg::join(manifest.dependencies, ",")) << '\n';
    std::cout << "conflicts: " << (manifest.conflicts.empty() ? "-" : zenpkg::join(manifest.conflicts, ",")) << '\n';
}

void print_probe(const std::filesystem::path& path, const zenpkg::StreamingForeignProbe& probe) {
    std::cout << "path: " << path.string() << '\n';
    std::cout << "format: " << zenpkg::foreign_format_id(probe.detection.format) << '\n';
    std::cout << "name: " << probe.detection.name << '\n';
    std::cout << "family: " << probe.detection.family << '\n';
    std::cout << "support: " << package_foreign::support_text(probe.detection.support) << '\n';
    std::cout << "confidence: " << (probe.detection.extension_only ? "extension-only" : "signature") << '\n';
    std::cout << "reason: " << probe.detection.reason << '\n';
    std::cout << "bytes: " << probe.file_size << '\n';
    std::cout << "sampled_bytes: " << probe.sampled_bytes << '\n';
    std::cout << "sha256: " << probe.sha256 << '\n';
    std::cout << "PROBE_OK format=" << zenpkg::foreign_format_id(probe.detection.format)
              << " support=" << package_foreign::support_text(probe.detection.support)
              << " confidence=" << (probe.detection.extension_only ? "extension-only" : "signature") << '\n';
}

int run_command(const std::string& command, const Arguments& arguments) {
    if (command == "pack") {
        arguments.reject_unknown({"manifest", "payload", "output"});
        if (!arguments.positionals.empty()) throw Error("pack does not accept positional arguments");
        const auto manifest = arguments.require_one("manifest");
        const auto payload = arguments.require_one("payload");
        const auto output = arguments.require_one("output");
        zenpkg::pack_to_file(manifest, payload, output);
        const auto package = zenpkg::read_package(output);
        std::cout << "PACK_OK " << package.manifest.name << '@' << package.manifest.version
                  << " bytes=" << std::filesystem::file_size(output)
                  << " sha256=" << package.package_digest_hex << '\n';
        return 0;
    }
    if (command == "verify") {
        arguments.reject_unknown({});
        const auto path = arguments.require_positional("package path");
        const auto package = zenpkg::read_package(path);
        std::cout << "VERIFY_OK " << package.manifest.name << '@' << package.manifest.version
                  << " sha256=" << package.package_digest_hex << '\n';
        return 0;
    }
    if (command == "inspect") {
        arguments.reject_unknown({});
        const auto package = zenpkg::read_package(arguments.require_positional("package path"));
        print_manifest(package);
        return 0;
    }
    if (command == "probe") {
        arguments.reject_unknown({});
        const auto path = arguments.require_positional("file path");
        print_probe(path, zenpkg::probe_foreign_streaming(path));
        return 0;
    }
    if (command == "import-native") {
        arguments.reject_unknown({"name", "version", "license", "source", "asset-policy", "output"});
        const auto input = arguments.require_positional("native executable path");
        const auto output = arguments.require_one("output");
        (void)zenpkg::require_native_import_candidate(input);
        const auto package = zenpkg::import_native(
            input, arguments.require_one("name"), arguments.require_one("version"),
            arguments.require_one("license"), arguments.require_one("source"),
            arguments.require_one("asset-policy"), output);
        std::cout << "IMPORT_NATIVE_OK " << package.manifest.name << '@' << package.manifest.version
                  << " payload=" << package.manifest.payload_type
                  << " bytes=" << std::filesystem::file_size(output)
                  << " sha256=" << package.package_digest_hex << '\n';
        return 0;
    }
    if (command == "extract") {
        arguments.reject_unknown({"manifest-output", "payload-output"});
        const auto path = arguments.require_positional("package path");
        zenpkg::extract_package(path, arguments.require_one("manifest-output"), arguments.require_one("payload-output"));
        std::cout << "EXTRACT_OK\n";
        return 0;
    }
    if (command == "resolve") {
        arguments.reject_unknown({"architecture", "target", "os-version", "capability"});
        const auto package = zenpkg::read_package(arguments.require_positional("package path"));
        const auto architecture = arguments.require_one("architecture");
        const auto target = arguments.require_one("target");
        const auto os_version = arguments.require_one("os-version");
        if (!zenpkg::is_safe_identifier(architecture)) throw Error("invalid host architecture");
        if (!zenpkg::is_safe_identifier(target)) throw Error("invalid host target");
        if (!zenpkg::is_safe_version(os_version)) throw Error("invalid host OS version");
        const auto capability_values = arguments.many("capability");
        const std::set<std::string> capabilities(capability_values.begin(), capability_values.end());
        const auto resolution = zenpkg::resolve_manifest(package.manifest, architecture, target, os_version, capabilities);
        if (resolution.compatible) {
            std::cout << "RESOLVE_OK " << package.manifest.name << '@' << package.manifest.version << '\n';
            return 0;
        }
        std::cout << "RESOLVE_INCOMPATIBLE " << package.manifest.name << '@' << package.manifest.version << '\n';
        for (const auto& reason : resolution.reasons) std::cout << "reason: " << reason << '\n';
        return 3;
    }
    if (command == "index") {
        arguments.reject_unknown({"input", "output"});
        if (!arguments.positionals.empty()) throw Error("index does not accept positional arguments");
        const auto input = arguments.require_one("input");
        const auto output = arguments.require_one("output");
        const auto records = zenpkg::scan_packages(input);
        zenpkg::write_text_atomic(output, zenpkg::render_index(records));
        std::cout << "INDEX_OK packages=" << records.size() << " output=" << output << '\n';
        return 0;
    }
    if (command == "hash") {
        arguments.reject_unknown({});
        const auto path = arguments.require_positional("file path");
        const auto digest = zenpkg::sha256_file_streaming(path);
        std::cout << digest.sha256 << '\n';
        return 0;
    }
    if (command == "manifest-check") {
        arguments.reject_unknown({"payload-size"});
        const auto path = arguments.require_positional("manifest path");
        const auto payload_size = parse_u64(arguments.require_one("payload-size"), "payload-size");
        const auto manifest = zenpkg::parse_and_validate_manifest(zenpkg::read_text(path), payload_size);
        std::cout << "MANIFEST_OK " << manifest.name << '@' << manifest.version << '\n';
        std::cout << zenpkg::canonical_manifest(manifest);
        return 0;
    }
    throw Error("unknown command: " + command);
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "help") {
            print_usage(std::cout);
            return argc < 2 ? 2 : 0;
        }
        return run_command(argv[1], Arguments::parse(argc, argv, 2));
    } catch (const Error& error) {
        std::cerr << "zenpkg: error: " << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "zenpkg: fatal: " << error.what() << '\n';
        return 2;
    }
}
