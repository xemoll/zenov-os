Validation read_envelope(const char* path, uint16_t expected_role, Envelope& envelope) {
    if (!metadata_buffer) return Validation::storage;
    uint32_t size = 0;
    if (!storage::read_file(path, metadata_buffer, maximum_metadata_bytes, size)) return Validation::storage;
    if (size < sizeof(Header) + sizeof(Signature) || size > maximum_metadata_bytes) return Validation::malformed;
    const auto* header = reinterpret_cast<const Header*>(metadata_buffer);
    const char magic[4] = {'Z','R','M','1'};
    if (!storage::bytes_equal(header->magic, magic, sizeof(magic)) || header->schema != schema_version ||
        header->role != expected_role || !header->version || !header->body_size ||
        !header->signature_count || header->signature_count > maximum_signatures ||
        !header->threshold || header->threshold > header->signature_count) return Validation::malformed;
    for (uint8_t value : header->reserved) if (value) return Validation::malformed;
    const uint32_t signatures_size = static_cast<uint32_t>(header->signature_count) * sizeof(Signature);
    if (sizeof(Header) + header->body_size + signatures_size != size) return Validation::malformed;
    uint8_t digest[32];
    security_guard::sha256(metadata_buffer + sizeof(Header), header->body_size, digest);
    if (!security_guard::digest_equal(digest, header->body_sha256)) return Validation::digest;
    if (header->expires <= kZenRepoMinimumTrustedEpoch) return Validation::expired;
    envelope.header = *header;
    envelope.file_size = size;
    envelope.body = metadata_buffer + sizeof(Header);
    envelope.signatures = reinterpret_cast<const Signature*>(envelope.body + header->body_size);
    security_guard::sha256(metadata_buffer, size, envelope.file_digest);
    return Validation::okay;
}
void signed_digest(const Envelope& envelope, uint8_t digest[32]) {
    security_guard::sha256(metadata_buffer, sizeof(Header) + envelope.header.body_size, digest);
}
bool verify_threshold(const Envelope& envelope, const RootTrust& trust, uint16_t role) {
    const RoleSpec* spec = find_role(trust, role);
    if (!spec || !spec->threshold || spec->threshold > spec->key_count || spec->key_count > 2U) return false;
    uint8_t digest[32];
    signed_digest(envelope, digest);
    uint8_t accepted_ids[2][8]{};
    uint32_t accepted = 0;
    for (uint32_t i = 0; i < envelope.header.signature_count; ++i) {
        const Signature& signature = envelope.signatures[i];
        bool authorized = false;
        for (uint32_t key = 0; key < spec->key_count; ++key) {
            if (key_id_equal(signature.key_id, spec->key_ids + key * 8U)) authorized = true;
        }
        if (!authorized) continue;
        bool duplicate = false;
        for (uint32_t prior = 0; prior < accepted; ++prior) if (key_id_equal(accepted_ids[prior], signature.key_id)) duplicate = true;
        if (duplicate) continue;
        const KeyRecord* public_key = find_key(trust, signature.key_id);
        if (public_key && rsa_pss::verify(signature.bytes, digest, public_key->modulus)) {
            memcpy(accepted_ids[accepted], signature.key_id, 8U);
            ++accepted;
        }
    }
    return accepted >= spec->threshold;
}

Validation parse_root(const Envelope& envelope, RootTrust& output) {
    if (envelope.header.body_size < sizeof(RootHead)) return Validation::malformed;
    const auto* root = reinterpret_cast<const RootHead*>(envelope.body);
    if (!root->consistent_snapshot || !root->key_count || root->key_count > maximum_keys ||
        !root->role_count || root->role_count > maximum_roles || root->reserved) return Validation::malformed;
    const uint32_t expected = sizeof(RootHead) + root->key_count * sizeof(KeyRecord) + root->role_count * sizeof(RoleSpec);
    if (expected != envelope.header.body_size) return Validation::malformed;
    output = RootTrust{};
    output.key_count = root->key_count;
    output.role_count = root->role_count;
    output.version = envelope.header.version;
    const auto* keys = reinterpret_cast<const KeyRecord*>(envelope.body + sizeof(RootHead));
    const auto* roles = reinterpret_cast<const RoleSpec*>(reinterpret_cast<const uint8_t*>(keys + root->key_count));
    for (uint32_t i = 0; i < output.key_count; ++i) {
        bool empty = true;
        for (uint8_t value : keys[i].key_id) if (value) empty = false;
        if (empty || (keys[i].modulus[0] & 0x80U) == 0) return Validation::malformed;
        for (uint32_t prior = 0; prior < i; ++prior) if (key_id_equal(keys[i].key_id, keys[prior].key_id)) return Validation::malformed;
        output.keys[i] = keys[i];
    }
    for (uint32_t i = 0; i < output.role_count; ++i) {
        if (!roles[i].role || roles[i].role > role_delegated || !roles[i].threshold ||
            !roles[i].key_count || roles[i].key_count > 2U || roles[i].threshold > roles[i].key_count ||
            roles[i].reserved) return Validation::malformed;
        for (uint32_t key = 0; key < roles[i].key_count; ++key) if (!find_key(output, roles[i].key_ids + key * 8U)) return Validation::malformed;
        output.roles[i] = roles[i];
    }
    return Validation::okay;
}
Validation bootstrap_root(RootTrust& output) {
    Envelope envelope{};
    Validation validation = read_envelope("/repo/root-bootstrap.zrm", role_root, envelope);
    if (validation != Validation::okay || envelope.header.version != 1U) return validation == Validation::okay ? Validation::root_sequence : validation;
    RootTrust compiled{};
    compiled.key_count = compiled.role_count = 1U;
    compiled.version = 0U;
    memcpy(compiled.keys[0].key_id, kZenRepoBootstrapRootKeyId, 8U);
    memcpy(compiled.keys[0].modulus, kZenRepoBootstrapRootModulus, 256U);
    compiled.roles[0].role = role_root;
    compiled.roles[0].threshold = compiled.roles[0].key_count = 1U;
    memcpy(compiled.roles[0].key_ids, kZenRepoBootstrapRootKeyId, 8U);
    if (!verify_threshold(envelope, compiled, role_root)) return Validation::signature;
    return parse_root(envelope, output);
}
Validation update_root(const RootTrust& old, RootTrust& output) {
    Envelope envelope{};
    Validation validation = read_envelope("/repo/root.zrm", role_root, envelope);
    if (validation != Validation::okay) return validation;
    if (envelope.header.version != old.version + 1U) return Validation::root_sequence;
    RootTrust next{};
    validation = parse_root(envelope, next);
    if (validation != Validation::okay) return validation;
    if (!verify_threshold(envelope, old, role_root) || !verify_threshold(envelope, next, role_root)) return Validation::threshold;
    output = next;
    return Validation::okay;
}
Validation verify_role_file(const char* path, uint16_t role, const RootTrust& trust, Envelope& envelope) {
    Validation validation = read_envelope(path, role, envelope);
    if (validation != Validation::okay) return validation;
    return verify_threshold(envelope, trust, role) ? Validation::okay : Validation::signature;
}
