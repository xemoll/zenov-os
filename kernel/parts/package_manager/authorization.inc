using AuthorizedPackage = package_repository::Target;

uint32_t authorized_package_count() {
    return package_repository::target_count();
}
const AuthorizedPackage* find_authorized(const char* name, const char* version, const char* entrypoint, uint8_t payload_type) {
    return package_repository::find_target(name, version, entrypoint, payload_type);
}
bool authorized_package(const native_package::VerifiedPackage& package, uint32_t package_size) {
    const AuthorizedPackage* trusted = find_authorized(package.manifest.name, package.manifest.version,
        package.manifest.entrypoint, package.manifest.payload_type);
    if (!trusted || trusted->package_length != package_size ||
        !crypto::digest_equal(package.package_digest, trusted->package_digest)) return false;
    uint8_t payload_digest[32]{};
    crypto::sha256(package.payload, package.payload_size, payload_digest);
    return crypto::digest_equal(payload_digest, trusted->payload_digest);
}
bool authorized_reference(const char* name, const PackageReference& reference) {
    if (!reference.valid) return false;
    const AuthorizedPackage* trusted = find_authorized(name, reference.version, reference.entrypoint, reference.payload_type);
    return trusted && crypto::digest_equal(reference.package_digest, trusted->package_digest) &&
        crypto::digest_equal(reference.payload_digest, trusted->payload_digest);
}
bool parse_release_version(const char* input, uint32_t output[3]) {
    if (!input) return false;
    const char* cursor = input;
    for (uint32_t part = 0; part < 3U; ++part) {
        if (*cursor < '0' || *cursor > '9') return false;
        uint32_t value = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            const uint32_t digit = static_cast<uint32_t>(*cursor - '0');
            if (value > (0xFFFFFFFFU - digit) / 10U) return false;
            value = value * 10U + digit;
            ++cursor;
        }
        output[part] = value;
        if (part < 2U) { if (*cursor != '.') return false; ++cursor; }
    }
    return *cursor == 0 || *cursor == '-' || *cursor == '+';
}
int version_compare(const char* left, const char* right) {
    uint32_t a[3]{}, b[3]{};
    if (!parse_release_version(left, a) || !parse_release_version(right, b)) return string_compare(left, right);
    for (uint32_t i = 0; i < 3U; ++i) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}
