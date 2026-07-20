const MetaReference* find_reference(const Envelope& envelope, const char* name) {
    if (envelope.header.body_size < sizeof(ReferenceHead)) return nullptr;
    const auto* head = reinterpret_cast<const ReferenceHead*>(envelope.body);
    if (!head->meta_count || head->meta_count > 8U || head->reserved ||
        sizeof(ReferenceHead) + head->meta_count * sizeof(MetaReference) != envelope.header.body_size) return nullptr;
    const auto* references = reinterpret_cast<const MetaReference*>(envelope.body + sizeof(ReferenceHead));
    for (uint32_t i = 0; i < head->meta_count; ++i) {
        if (!fixed_text_valid(references[i].name, sizeof(references[i].name))) return nullptr;
        if (string_equal(references[i].name, name)) return &references[i];
    }
    return nullptr;
}
bool reference_matches(const MetaReference& reference, const Envelope& envelope) {
    return reference.version == envelope.header.version && reference.length == envelope.file_size &&
        security_guard::digest_equal(reference.digest, envelope.file_digest);
}
Validation enforce_rollback(uint32_t root, uint32_t targets, uint32_t delegated, uint32_t snapshot, uint32_t timestamp) {
    if (persistent_state.root && (root < persistent_state.root || targets < persistent_state.targets ||
        delegated < persistent_state.delegated || snapshot < persistent_state.snapshot ||
        timestamp < persistent_state.timestamp)) return Validation::rollback;
    return Validation::okay;
}
Validation verify_repository(Target output[maximum_targets], uint32_t& output_count, State& next_state) {
    output_count = 0;
    RootTrust root_v1{}, root_v2{};
    Validation validation = bootstrap_root(root_v1);
    if (validation != Validation::okay) return validation;
    validation = update_root(root_v1, root_v2);
    if (validation != Validation::okay) return validation;

    Envelope timestamp{};
    validation = verify_role_file("/repo/timestamp.zrm", role_timestamp, root_v2, timestamp);
    if (validation != Validation::okay) return validation;
    const MetaReference* snapshot_reference_pointer = find_reference(timestamp, "snapshot.zrm");
    if (!snapshot_reference_pointer) return Validation::reference;
    const MetaReference snapshot_reference = *snapshot_reference_pointer;

    Envelope snapshot{};
    validation = verify_role_file("/repo/snapshot.zrm", role_snapshot, root_v2, snapshot);
    if (validation != Validation::okay || !reference_matches(snapshot_reference, snapshot)) return validation == Validation::okay ? Validation::reference : validation;
    const MetaReference* targets_reference_pointer = find_reference(snapshot, "targets.zrm");
    const MetaReference* delegated_reference_pointer = find_reference(snapshot, "native-apps.zrm");
    if (!targets_reference_pointer || !delegated_reference_pointer) return Validation::reference;
    const MetaReference targets_reference = *targets_reference_pointer;
    const MetaReference delegated_reference = *delegated_reference_pointer;

    Envelope targets{};
    validation = verify_role_file("/repo/targets.zrm", role_targets, root_v2, targets);
    if (validation != Validation::okay || !reference_matches(targets_reference, targets)) return validation == Validation::okay ? Validation::reference : validation;

    if (targets.header.body_size != sizeof(DelegationHead) + sizeof(Delegation)) return Validation::delegation;
    const auto* delegation_head_pointer = reinterpret_cast<const DelegationHead*>(targets.body);
    const auto* delegation_pointer = reinterpret_cast<const Delegation*>(targets.body + sizeof(DelegationHead));
    const DelegationHead delegation_head = *delegation_head_pointer;
    const Delegation delegation = *delegation_pointer;
    if (delegation_head.target_count || delegation_head.delegation_count != 1U ||
        !fixed_text_valid(delegation.role_name, sizeof(delegation.role_name)) ||
        !fixed_text_valid(delegation.prefix, sizeof(delegation.prefix)) ||
        !string_equal(delegation.role_name, "native-apps") || !string_equal(delegation.prefix, "native/") ||
        delegation.threshold != 1U || !delegation.terminating || delegation.reserved) return Validation::delegation;
    const RoleSpec* delegated_role = find_role(root_v2, role_delegated);
    if (!delegated_role || delegated_role->key_count != 1U || !key_id_equal(delegated_role->key_ids, delegation.key_id)) return Validation::delegation;

    Envelope delegated{};
    validation = verify_role_file("/repo/native-apps.zrm", role_delegated, root_v2, delegated);
    if (validation != Validation::okay || !reference_matches(delegated_reference, delegated)) return validation == Validation::okay ? Validation::reference : validation;

    if (delegated.header.body_size < sizeof(TargetsHead)) return Validation::target;
    const auto* target_head = reinterpret_cast<const TargetsHead*>(delegated.body);
    if (!target_head->target_count || target_head->target_count > maximum_targets || target_head->reserved ||
        sizeof(TargetsHead) + target_head->target_count * sizeof(Target) != delegated.header.body_size) return Validation::target;
    const auto* targets_data = reinterpret_cast<const Target*>(delegated.body + sizeof(TargetsHead));
    for (uint32_t i = 0; i < target_head->target_count; ++i) {
        const Target& target = targets_data[i];
        if (!fixed_text_valid(target.path, sizeof(target.path)) || !fixed_text_valid(target.name, sizeof(target.name)) ||
            !fixed_text_valid(target.version, sizeof(target.version)) || !fixed_text_valid(target.entrypoint, sizeof(target.entrypoint)) ||
            !fixed_text_valid(target.read_scope, sizeof(target.read_scope), true) || !fixed_text_valid(target.write_scope, sizeof(target.write_scope), true) ||
            !string_starts_with(target.path, "native/") || !target.package_length || !target.payload_type ||
            (target.syscall_mask & ~process::capability_all) != 0U) return Validation::target;
        const bool needs_read_scope = (target.syscall_mask & (process::capability_file_read | process::capability_file_stat)) != 0U;
        const bool needs_write_scope = (target.syscall_mask & process::capability_file_write) != 0U;
        if (needs_read_scope != (target.read_scope[0] != 0) || needs_write_scope != (target.write_scope[0] != 0)) return Validation::target;
        for (uint8_t value : target.reserved0) if (value) return Validation::target;
        for (uint32_t prior = 0; prior < i; ++prior) if (string_equal(target.path, output[prior].path)) return Validation::target;
        output[i] = target;
    }
    output_count = target_head->target_count;
    validation = enforce_rollback(root_v2.version, targets.header.version, delegated.header.version, snapshot.header.version, timestamp.header.version);
    if (validation != Validation::okay) return validation;
    state_initialize(next_state);
    next_state.root = root_v2.version;
    next_state.targets = targets.header.version;
    next_state.delegated = delegated.header.version;
    next_state.snapshot = snapshot.header.version;
    next_state.timestamp = timestamp.header.version;
    next_state.checksum = state_checksum(next_state);
    active_root = root_v2;
    return Validation::okay;
}
const char* validation_text(Validation validation) {
    switch (validation) {
        case Validation::okay: return "okay";
        case Validation::storage: return "storage";
        case Validation::malformed: return "malformed";
        case Validation::digest: return "digest";
        case Validation::signature: return "signature";
        case Validation::threshold: return "threshold";
        case Validation::expired: return "expired";
        case Validation::rollback: return "rollback";
        case Validation::root_sequence: return "root-sequence";
        case Validation::reference: return "mix-and-match";
        case Validation::delegation: return "delegation";
        case Validation::target: return "target";
        case Validation::state: return "state";
    }
    return "unknown";
}
