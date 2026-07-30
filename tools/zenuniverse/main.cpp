// SPDX-License-Identifier: BSD-2-Clause
#include "artifact_manifest.hpp"
#include "resolver.hpp"

#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace zenuniverse {

struct Args {
    std::vector<std::string> positional;
    StringMap<std::vector<std::string>> options;
};

Args parse_args(int argc, char** argv, int start) {
    Args args;
    for (int index = start; index < argc; ++index) {
        const std::string token = argv[index];
        if (token.rfind("--", 0U) == 0U) {
            const auto key = token.substr(2U);
            if (key.empty() || index + 1 >= argc) throw Error("option requires value: " + token);
            args.options[key].push_back(argv[++index]);
        } else {
            args.positional.push_back(token);
        }
    }
    return args;
}

std::string one(const Args& args, const std::string& key) {
    const auto it = args.options.find(key);
    if (it == args.options.end() || it->second.size() != 1U) throw Error("exactly one --" + key + " required");
    return it->second.front();
}

std::string optional_one(const Args& args, const std::string& key, const std::string& fallback = {}) {
    const auto it = args.options.find(key);
    if (it == args.options.end()) return fallback;
    if (it->second.size() != 1U) throw Error("at most one --" + key + " allowed");
    return it->second.front();
}

std::vector<std::string> many(const Args& args, const std::string& key) {
    const auto it = args.options.find(key);
    return it == args.options.end() ? std::vector<std::string>{} : it->second;
}

void reject_unknown(const Args& args, const StringSet& allowed) {
    for (const auto& pair : args.options) if (!allowed.count(pair.first)) throw Error("unknown option --" + pair.first);
}

void require_no_positionals(const Args& args, const std::string& command) {
    if (!args.positional.empty()) throw Error(command + " takes no positional arguments");
}

void usage() {
    std::cout
        << "zenuniverse - deterministic universal package catalog, artifact manifest and runtime resolver\n\n"
        << "  zenuniverse validate FILE.zsource [...]\n"
        << "  zenuniverse compile --input DIR --output CATALOG.zuc\n"
        << "  zenuniverse resolve --input DIR --package ID --host-arch ARCH [--capability CAP ...]\n"
        << "  zenuniverse host-profile --name PROFILE\n"
        << "  zenuniverse runtime-plan --input DIR --package ID --host-profile PROFILE [--artifact FAMILY]\n"
        << "  zenuniverse runtime-status --input DIR --runtime RUNTIME --host-profile PROFILE\n"
        << "  zenuniverse artifact-manifest --input DIR --profile ID --artifact FAMILY --file PATH --ownership MODE --output FILE\n"
        << "  zenuniverse verify-artifact --manifest FILE --file PATH\n"
        << "  zenuniverse launch-plan --input DIR --manifest FILE --file PATH --host-profile PROFILE [--asset ID=PATH ...] [--output FILE]\n"
        << "  zenuniverse fetch-plan --input DIR --package ID\n"
        << "  zenuniverse self-test\n";
}

std::string render_plan(const std::string& id, const Plan& plan) {
    std::ostringstream out;
    for (const auto* descriptor : plan.order) {
        out << "install " << descriptor->id << '@' << descriptor->version
            << " availability=" << descriptor->availability
            << " delivery=" << descriptor->delivery
            << " runtime=" << descriptor->runtime << '\n';
    }
    for (const auto& decision : plan.alternatives) {
        out << "capability-alternative=" << decision.expression
            << " selected=" << decision.selected
            << " satisfied=" << (decision.satisfied ? "yes" : "no") << '\n';
    }
    for (const auto& asset : plan.required_assets) out << "required-asset=" << asset << " source=user-supplied\n";
    for (const auto& blocker : plan.blocked) out << "blocked: " << blocker << '\n';
    for (const auto& diagnostic : plan.diagnostics) out << "diagnostic: " << diagnostic << '\n';
    if (plan.user_asset) out << "asset: user-supplied; ZenovOS must not download or redistribute proprietary content\n";
    if (plan.blocked.empty()) out << "ZENUNIVERSE_RUNTIME_READY package=" << id << '\n';
    else out << "ZENUNIVERSE_RUNTIME_BLOCKED package=" << id << " reasons=" << plan.blocked.size() << '\n';
    return out.str();
}

StringMap<fs::path> parse_asset_arguments(const std::vector<std::string>& values) {
    StringMap<fs::path> result;
    for (const auto& value : values) {
        const auto equals = value.find('=');
        if (equals == std::string::npos || equals == 0U || equals + 1U >= value.size()) throw Error("--asset must use ID=PATH");
        const auto id = value.substr(0U, equals);
        const auto path = value.substr(equals + 1U);
        if (!safe_id(id) || !result.emplace(id, fs::path(path)).second) throw Error("unsafe or duplicate --asset id: " + id);
    }
    return result;
}

std::string expand_argument(const std::string& argument) {
    if (argument == "%artifact%") return "<verified-artifact>";
    constexpr std::string_view prefix = "%asset:";
    if (argument.rfind(prefix, 0U) == 0U && argument.back() == '%') {
        return "<verified-asset:" + argument.substr(prefix.size(), argument.size() - prefix.size() - 1U) + ">";
    }
    return argument;
}

bool is_console_platform(const std::string& platform) {
    return platform == "playstation1" || platform == "playstation-portable" || platform == "playstation2" ||
           platform == "playstation3" || platform == "xbox" || platform == "xbox360";
}

int command(const std::string& command_name, const Args& args) {
    if (command_name == "self-test") {
        reject_unknown(args, {});
        require_no_positionals(args, "self-test");
        if (sha256("abc") != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") throw Error("SHA-256 known-answer test failed");
        const auto profile = require_host_profile("zenov-0.1.1-i686");
        if (profile.architecture != "x86" || profile.artifact_bytes_limit != 65536U || !profile.capabilities.count("loader.elf32-static") || profile.capabilities.count("kernel.threads")) {
            throw Error("host-profile invariant failed");
        }
        if (canonical_alternatives("graphics.vulkan1.0|graphics.opengl3.1") != "graphics.opengl3.1|graphics.vulkan1.0") throw Error("capability alternative canonicalization failed");
        ArtifactManifest manifest{"ZENARTIFACT1", "org.zenov.profile.native-app", "zex1", "redistributable", 4U, std::string(64U, 'a')};
        if (canonical_artifact_manifest(manifest).find("ZENARTIFACT1\n") != 0U) throw Error("artifact manifest canonicalization failed");
        std::cout << "ZENUNIVERSE_SELF_TEST_OK host-profile=yes capability-registry=yes artifact-manifest=yes\n";
        return 0;
    }

    if (command_name == "validate") {
        reject_unknown(args, {});
        if (args.positional.empty()) throw Error("validate requires descriptors");
        for (const auto& path : args.positional) {
            auto descriptor = parse_descriptor(path);
            validate_descriptor(descriptor);
            std::cout << "VALID " << descriptor.id << '@' << descriptor.version << '\n';
        }
        std::cout << "ZENUNIVERSE_VALIDATE_OK count=" << args.positional.size() << '\n';
        return 0;
    }

    if (command_name == "compile") {
        reject_unknown(args, {"input", "output"});
        require_no_positionals(args, "compile");
        const auto records = load_directory(one(args, "input"));
        const auto catalog = compile_catalog(records);
        write_atomic(one(args, "output"), catalog);
        std::cout << "ZENUNIVERSE_COMPILE_OK packages=" << records.size() << " sha256=" << sha256(catalog) << '\n';
        return 0;
    }

    if (command_name == "host-profile") {
        reject_unknown(args, {"name"});
        require_no_positionals(args, "host-profile");
        const auto profile = require_host_profile(one(args, "name"));
        std::cout << profile.schema << "\nprofile=" << profile.id << "\narchitecture=" << profile.architecture
                  << "\ndescription=" << profile.description << "\nartifact-bytes-limit=" << profile.artifact_bytes_limit
                  << "\nprocess-limit=" << profile.process_limit << "\nthread-limit=" << profile.thread_limit << '\n';
        for (const auto& capability : profile.capabilities) std::cout << "capability=" << capability << '\n';
        std::cout << "ZENUNIVERSE_HOST_PROFILE_OK capabilities=" << profile.capabilities.size() << '\n';
        return 0;
    }

    if (command_name == "resolve") {
        reject_unknown(args, {"input", "package", "host-arch", "capability"});
        require_no_positionals(args, "resolve");
        const auto records = load_directory(one(args, "input"));
        const auto id = one(args, "package");
        const auto architecture = one(args, "host-arch");
        if (!architectures.count(architecture)) throw Error("unsupported host architecture");
        StringSet capabilities;
        for (const auto& value : many(args, "capability")) {
            if (!known_capability(value)) throw Error("unknown manual capability: " + value);
            capabilities.insert(value);
        }
        const auto* application = latest(records, id);
        if (!application) throw Error("package not found: " + id);
        const auto plan = make_plan(*application, records, architecture, capabilities);
        std::cout << "capability-source=manual-unverified\n" << render_plan(id, plan);
        if (plan.blocked.empty()) {
            std::cout << "ZENUNIVERSE_RESOLVE_OK package=" << id << '\n';
            return 0;
        }
        std::cout << "ZENUNIVERSE_RESOLVE_BLOCKED package=" << id << " reasons=" << plan.blocked.size() << '\n';
        return 3;
    }

    if (command_name == "runtime-plan") {
        reject_unknown(args, {"input", "package", "host-profile", "artifact"});
        require_no_positionals(args, "runtime-plan");
        const auto records = load_directory(one(args, "input"));
        const auto id = one(args, "package");
        const auto profile = require_host_profile(one(args, "host-profile"));
        const auto* application = latest(records, id);
        if (!application) throw Error("package not found: " + id);
        auto plan = make_plan(*application, records, profile.architecture, profile.capabilities);
        const auto artifact = optional_one(args, "artifact");
        const auto* provider = runtime_provider(records, application->runtime);
        if (!artifact.empty()) {
            if (!artifacts.count(artifact)) throw Error("unsupported artifact family: " + artifact);
            if (!provider || !provider_accepts_artifact(*provider, artifact)) append_blocker(plan, "runtime provider does not accept artifact: " + artifact);
        }
        std::cout << "ZEN_RUNTIME_PLAN2\nhost-profile=" << profile.id << "\nhost-architecture=" << profile.architecture
                  << "\npackage=" << id << '\n';
        if (!artifact.empty()) std::cout << "artifact=" << artifact << '\n';
        std::cout << render_plan(id, plan);
        return plan.blocked.empty() ? 0 : 3;
    }

    if (command_name == "runtime-status") {
        reject_unknown(args, {"input", "runtime", "host-profile"});
        require_no_positionals(args, "runtime-status");
        const auto records = load_directory(one(args, "input"));
        const auto runtime = one(args, "runtime");
        const auto profile = require_host_profile(one(args, "host-profile"));
        const auto* provider = runtime_provider(records, runtime);
        if (!provider) throw Error("runtime provider not found: " + runtime);
        const auto plan = make_runtime_plan(runtime, records, profile.architecture, profile.capabilities);
        std::cout << "ZEN_RUNTIME_STATUS2\nruntime=" << runtime << "\nprovider=" << provider->id << '@' << provider->version
                  << "\nprovider-abi=" << provider->provider_abi << "\nprovider-availability=" << provider->availability
                  << "\nlaunch-mode=" << provider->launch_mode << "\nhost-profile=" << profile.id << '\n';
        for (const auto& artifact : provider->accepts) std::cout << "accepts=" << artifact << '\n';
        std::cout << render_plan(provider->id, plan);
        return plan.blocked.empty() ? 0 : 3;
    }

    if (command_name == "artifact-manifest") {
        reject_unknown(args, {"input", "profile", "artifact", "file", "ownership", "output"});
        require_no_positionals(args, "artifact-manifest");
        const auto records = load_directory(one(args, "input"));
        const auto profile_id = one(args, "profile");
        const auto artifact = one(args, "artifact");
        const auto ownership = one(args, "ownership");
        const auto* profile = latest(records, profile_id);
        if (!profile || profile->kind == "runtime") throw Error("artifact profile not found: " + profile_id);
        const auto* provider = runtime_provider(records, profile->runtime);
        if (!provider) throw Error("runtime provider not found for profile: " + profile->runtime);
        if (!provider_accepts_artifact(*provider, artifact)) throw Error("runtime provider " + provider->id + " does not accept artifact " + artifact);
        if (is_console_platform(profile->platform) && ownership != "user-owned") throw Error("console artifacts must use ownership=user-owned");
        if (profile->delivery == "user-supplied" && ownership != "user-owned") throw Error("user-supplied profile requires ownership=user-owned");
        const auto digest = digest_regular_file(one(args, "file"));
        const ArtifactManifest manifest{"ZENARTIFACT1", profile_id, artifact, ownership, digest.bytes, digest.sha256};
        const auto canonical = canonical_artifact_manifest(manifest);
        write_atomic(one(args, "output"), canonical);
        std::cout << "ZENUNIVERSE_ARTIFACT_MANIFEST_OK profile=" << profile_id << " artifact=" << artifact
                  << " bytes=" << digest.bytes << " sha256=" << digest.sha256 << '\n';
        return 0;
    }

    if (command_name == "verify-artifact") {
        reject_unknown(args, {"manifest", "file"});
        require_no_positionals(args, "verify-artifact");
        const auto manifest = parse_artifact_manifest(one(args, "manifest"));
        const auto digest = verify_artifact_file(manifest, one(args, "file"));
        std::cout << "ZENUNIVERSE_ARTIFACT_VERIFIED profile=" << manifest.profile << " artifact=" << manifest.artifact
                  << " bytes=" << digest.bytes << " sha256=" << digest.sha256 << '\n';
        return 0;
    }

    if (command_name == "launch-plan") {
        reject_unknown(args, {"input", "manifest", "file", "host-profile", "asset", "output"});
        require_no_positionals(args, "launch-plan");
        const auto records = load_directory(one(args, "input"));
        const auto manifest = parse_artifact_manifest(one(args, "manifest"));
        const auto artifact_digest = verify_artifact_file(manifest, one(args, "file"));
        const auto host = require_host_profile(one(args, "host-profile"));
        const auto* profile = latest(records, manifest.profile);
        if (!profile || profile->kind == "runtime") throw Error("artifact manifest profile not found: " + manifest.profile);
        const auto* provider = runtime_provider(records, profile->runtime);
        if (!provider) throw Error("runtime provider not found for profile: " + profile->runtime);
        auto plan = make_plan(*profile, records, host.architecture, host.capabilities);
        if (!provider_accepts_artifact(*provider, manifest.artifact)) append_blocker(plan, "runtime provider does not accept manifest artifact: " + manifest.artifact);
        if (manifest.bytes > host.artifact_bytes_limit) {
            append_blocker(plan, "artifact exceeds host profile storage limit: bytes=" + std::to_string(manifest.bytes) + " limit=" + std::to_string(host.artifact_bytes_limit));
        }
        if (is_console_platform(profile->platform) && manifest.ownership != "user-owned") append_blocker(plan, "console artifact ownership is not user-owned");

        const auto supplied_assets = parse_asset_arguments(many(args, "asset"));
        for (const auto& pair : supplied_assets) if (!plan.required_assets.count(pair.first)) throw Error("asset not required by plan: " + pair.first);
        StringMap<FileDigest> asset_digests;
        for (const auto& asset : plan.required_assets) {
            const auto it = supplied_assets.find(asset);
            if (it == supplied_assets.end()) append_blocker(plan, "required asset not supplied: " + asset);
            else asset_digests.emplace(asset, digest_regular_file(it->second));
        }

        std::ostringstream output;
        output << "ZENLAUNCH1\nprofile=" << manifest.profile << "\nartifact=" << manifest.artifact
               << "\nartifact-bytes=" << artifact_digest.bytes << "\nartifact-sha256=" << artifact_digest.sha256
               << "\nownership=" << manifest.ownership << "\nhost-profile=" << host.id
               << "\nprovider=" << provider->id << '@' << provider->version
               << "\nprovider-abi=" << provider->provider_abi << "\nlaunch-mode=" << provider->launch_mode
               << "\nentrypoint=" << provider->entrypoint << '\n';
        for (const auto& pair : asset_digests) {
            output << "asset=" << pair.first << " bytes=" << pair.second.bytes << " sha256=" << pair.second.sha256 << '\n';
        }
        for (const auto& argument : provider->launch_args) output << "argument=" << expand_argument(argument) << '\n';
        output << render_plan(manifest.profile, plan);
        output << (plan.blocked.empty() ? "ZENUNIVERSE_LAUNCH_READY" : "ZENUNIVERSE_LAUNCH_BLOCKED")
               << " profile=" << manifest.profile << " reasons=" << plan.blocked.size() << '\n';
        const auto text = output.str();
        std::cout << text;
        const auto output_path = optional_one(args, "output");
        if (!output_path.empty()) write_atomic(output_path, text);
        return plan.blocked.empty() ? 0 : 3;
    }

    if (command_name == "fetch-plan") {
        reject_unknown(args, {"input", "package"});
        require_no_positionals(args, "fetch-plan");
        const auto records = load_directory(one(args, "input"));
        const auto id = one(args, "package");
        const auto* descriptor = latest(records, id);
        if (!descriptor) throw Error("package not found: " + id);
        std::cout << "package=" << descriptor->id << '@' << descriptor->version << "\ndelivery=" << descriptor->delivery
                  << "\nbytes=" << descriptor->bytes << "\nsha256=" << descriptor->sha << '\n';
        for (const auto& mirror : descriptor->mirrors) std::cout << "mirror=" << mirror << '\n';
        std::cout << "runtime=" << descriptor->runtime << "\nartifact=" << descriptor->artifact << '\n';
        if (descriptor->delivery == "https") std::cout << "ZENUNIVERSE_FETCH_READY verified-https=yes atomic-temp=yes resume-policy=range-if-server-supports\n";
        else if (descriptor->delivery == "user-supplied") std::cout << "ZENUNIVERSE_FETCH_USER_SUPPLIED\n";
        else if (descriptor->delivery == "builtin") std::cout << "ZENUNIVERSE_FETCH_BUILTIN\n";
        else std::cout << "ZENUNIVERSE_FETCH_NO_NETWORK\n";
        return 0;
    }

    throw Error("unknown command: " + command_name);
}

} // namespace zenuniverse

int main(int argc, char** argv) {
    try {
        if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "help") {
            zenuniverse::usage();
            return argc < 2 ? 2 : 0;
        }
        return zenuniverse::command(argv[1], zenuniverse::parse_args(argc, argv, 2));
    } catch (const zenuniverse::Error& error) {
        std::cerr << "zenuniverse: error: " << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "zenuniverse: fatal: " << error.what() << '\n';
        return 2;
    }
}
