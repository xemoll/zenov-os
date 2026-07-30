#pragma once

#include "descriptor.hpp"
#include "host_profile.hpp"

namespace zenuniverse {

const Descriptor* latest(const std::vector<Descriptor>& records, const std::string& id) {
    const Descriptor* best = nullptr;
    for (const auto& descriptor : records) {
        if (descriptor.id == id && (!best || version_less(*best, descriptor))) best = &descriptor;
    }
    return best;
}

const Descriptor* runtime_provider(const std::vector<Descriptor>& records, const std::string& runtime) {
    return latest(records, "org.zenov.runtime." + runtime);
}

struct AlternativeDecision {
    std::string expression;
    std::string selected;
    bool satisfied = false;
};

struct Plan {
    std::vector<const Descriptor*> order;
    std::vector<std::string> blocked;
    std::vector<std::string> diagnostics;
    std::vector<AlternativeDecision> alternatives;
    StringSet required_assets;
    bool user_asset = false;
};

void append_assets(const Descriptor& descriptor, Plan& plan) {
    plan.required_assets.insert(descriptor.assets.begin(), descriptor.assets.end());
}

void append_blocker(Plan& plan, const std::string& reason) {
    if (std::find(plan.blocked.begin(), plan.blocked.end(), reason) == plan.blocked.end()) plan.blocked.push_back(reason);
}

void append_diagnostic(Plan& plan, const std::string& message) {
    if (std::find(plan.diagnostics.begin(), plan.diagnostics.end(), message) == plan.diagnostics.end()) plan.diagnostics.push_back(message);
}

int architecture_rank(const Descriptor& descriptor, const std::string& host_architecture) {
    if (descriptor.architecture == host_architecture) return 2;
    if (descriptor.architecture == "any") return 1;
    return 0;
}

int availability_rank(const Descriptor& descriptor) {
    if (descriptor.availability == "available") return 2;
    if (descriptor.availability == "external") return 1;
    return 0;
}

int delivery_rank(const Descriptor& descriptor) {
    if (descriptor.delivery == "builtin" || descriptor.delivery == "embedded" || descriptor.delivery == "https") return 2;
    if (descriptor.delivery == "user-supplied") return 1;
    return 0;
}

const Descriptor* best_provider(const std::string& capability, const std::vector<Descriptor>& records, const std::string& host_architecture) {
    const Descriptor* best = nullptr;
    for (const auto& descriptor : records) {
        if (std::find(descriptor.provides.begin(), descriptor.provides.end(), capability) == descriptor.provides.end()) continue;
        if (!best) { best = &descriptor; continue; }
        const auto current_score = std::array<int, 3>{architecture_rank(descriptor, host_architecture), availability_rank(descriptor), delivery_rank(descriptor)};
        const auto best_score = std::array<int, 3>{architecture_rank(*best, host_architecture), availability_rank(*best), delivery_rank(*best)};
        if (current_score > best_score || (current_score == best_score && version_less(*best, descriptor)) ||
            (current_score == best_score && !version_less(descriptor, *best) && !version_less(*best, descriptor) && descriptor.id < best->id)) {
            best = &descriptor;
        }
    }
    return best;
}

void resolve_capability(const std::string& capability, const std::vector<Descriptor>& records,
                        const std::string& host_architecture, const StringSet& host,
                        StringSet& active, StringSet& emitted, Plan& plan);

void resolve_alternative(const std::string& expression, const std::vector<Descriptor>& records,
                         const std::string& host_architecture, const StringSet& host,
                         StringSet& active, StringSet& emitted, Plan& plan) {
    const auto options = parse_capability_alternatives(expression);
    const auto canonical = canonical_alternatives(expression);
    for (const auto& option : options) {
        if (host.count(option) || emitted.count(option)) {
            plan.alternatives.push_back(AlternativeDecision{canonical, option, true});
            return;
        }
    }

    struct Attempt {
        std::string option;
        StringSet active;
        StringSet emitted;
        Plan plan;
        std::size_t added_blockers = 0U;
        std::size_t added_packages = 0U;
    };
    std::vector<Attempt> attempts;
    for (const auto& option : options) {
        Attempt attempt{option, active, emitted, plan, 0U, 0U};
        const auto before_blockers = attempt.plan.blocked.size();
        const auto before_packages = attempt.plan.order.size();
        resolve_capability(option, records, host_architecture, host, attempt.active, attempt.emitted, attempt.plan);
        attempt.added_blockers = attempt.plan.blocked.size() - before_blockers;
        attempt.added_packages = attempt.plan.order.size() - before_packages;
        attempts.push_back(std::move(attempt));
    }
    const auto best = std::min_element(attempts.begin(), attempts.end(), [](const Attempt& left, const Attempt& right) {
        if (left.added_blockers != right.added_blockers) return left.added_blockers < right.added_blockers;
        if (left.added_packages != right.added_packages) return left.added_packages < right.added_packages;
        return left.option < right.option;
    });
    if (best != attempts.end() && best->added_blockers == 0U) {
        active = best->active;
        emitted = best->emitted;
        plan = best->plan;
        plan.alternatives.push_back(AlternativeDecision{canonical, best->option, true});
        return;
    }

    append_blocker(plan, "no satisfiable capability alternative: " + canonical);
    for (const auto& attempt : attempts) {
        std::string reason = "unresolved";
        const auto baseline = plan.blocked.size();
        (void)baseline;
        if (!attempt.plan.blocked.empty()) reason = attempt.plan.blocked.back();
        append_diagnostic(plan, "alternative " + attempt.option + " rejected: " + reason);
    }
    plan.alternatives.push_back(AlternativeDecision{canonical, "-", false});
}

void resolve_capability(const std::string& capability, const std::vector<Descriptor>& records,
                        const std::string& host_architecture, const StringSet& host,
                        StringSet& active, StringSet& emitted, Plan& plan) {
    if (host.count(capability) || emitted.count(capability)) return;
    if (!active.insert(capability).second) {
        append_blocker(plan, "dependency cycle at " + capability);
        return;
    }
    const Descriptor* provider = best_provider(capability, records, host_architecture);
    if (!provider) {
        append_blocker(plan, "missing capability provider: " + capability);
        active.erase(capability);
        return;
    }
    if (provider->architecture != "any" && provider->architecture != host_architecture) {
        append_blocker(plan, "provider architecture mismatch: provider=" + provider->id + " package=" + provider->architecture + " host=" + host_architecture);
    }
    if (provider->availability != "available") {
        append_blocker(plan, "provider not available yet: " + provider->id + " (" + provider->availability + ")");
    }
    append_assets(*provider, plan);
    if (capability != "runtime.native" && provider->kind == "runtime" && provider->runtime == "native") {
        resolve_capability("runtime.native", records, host_architecture, host, active, emitted, plan);
    }
    for (const auto& requirement : provider->requirements) {
        resolve_capability(requirement, records, host_architecture, host, active, emitted, plan);
    }
    for (const auto& expression : provider->requirement_any) {
        resolve_alternative(expression, records, host_architecture, host, active, emitted, plan);
    }
    if (!emitted.count(capability)) {
        if (std::find(plan.order.begin(), plan.order.end(), provider) == plan.order.end()) plan.order.push_back(provider);
        for (const auto& provided : provider->provides) emitted.insert(provided);
    }
    active.erase(capability);
}

Plan make_plan(const Descriptor& application, const std::vector<Descriptor>& records,
               const std::string& host_architecture, const StringSet& host) {
    Plan plan;
    if (application.architecture != "any" && application.architecture != host_architecture) {
        append_blocker(plan, "architecture mismatch: package=" + application.architecture + " host=" + host_architecture);
    }
    append_assets(application, plan);
    StringSet active;
    StringSet emitted = host;
    resolve_capability(runtime_capability(application), records, host_architecture, host, active, emitted, plan);
    for (const auto& requirement : application.requirements) {
        resolve_capability(requirement, records, host_architecture, host, active, emitted, plan);
    }
    for (const auto& expression : application.requirement_any) {
        resolve_alternative(expression, records, host_architecture, host, active, emitted, plan);
    }
    plan.order.push_back(&application);
    plan.user_asset = application.delivery == "user-supplied";
    return plan;
}

Plan make_runtime_plan(const std::string& runtime, const std::vector<Descriptor>& records,
                       const std::string& host_architecture, const StringSet& host) {
    Plan plan;
    StringSet active;
    StringSet emitted = host;
    resolve_capability("runtime." + runtime, records, host_architecture, host, active, emitted, plan);
    return plan;
}

bool provider_accepts_artifact(const Descriptor& provider, const std::string& artifact) {
    return std::find(provider.accepts.begin(), provider.accepts.end(), artifact) != provider.accepts.end();
}

std::string compile_catalog(const std::vector<Descriptor>& records) {
    std::vector<const Descriptor*> sorted;
    for (const auto& descriptor : records) sorted.push_back(&descriptor);
    std::sort(sorted.begin(), sorted.end(), [](const Descriptor* left, const Descriptor* right) {
        return left->id == right->id ? version_less(*left, *right) : left->id < right->id;
    });
    std::ostringstream payload;
    for (const auto* descriptor : sorted) {
        const auto record = canonical(*descriptor);
        payload << "record-sha256=" << sha256(record) << '\n' << record << ".\n";
    }
    const auto body = payload.str();
    std::ostringstream out;
    out << "ZENUNIVERSE1\ncount=" << sorted.size() << "\npayload-sha256=" << sha256(body) << "\n\n" << body;
    return out.str();
}

} // namespace zenuniverse
