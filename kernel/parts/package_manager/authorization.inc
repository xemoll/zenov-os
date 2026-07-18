struct AuthorizedPackage {
    const char* name;
    const char* version;
    const char* entrypoint;
    uint8_t payload_type;
    uint8_t package_digest[32];
    uint8_t payload_digest[32];
};

static constexpr AuthorizedPackage authorized_packages[] = {
    {
        "hello-native", "0.1.0", "/data/apps/pkg-hello-native-0.1.0.zex", native_package::payload_zex1,
        {0xe0,0x4e,0x67,0x52,0xf9,0xe5,0xc2,0x1a,0x3b,0xa9,0x46,0x95,0x81,0x6d,0xb5,0x7c,
         0xa8,0xe9,0xf9,0x5a,0x28,0x28,0x9c,0x94,0x22,0x6c,0x75,0xdb,0xf2,0x25,0x99,0xdf},
        {0x2b,0x7b,0xa0,0x11,0x4d,0x52,0x28,0x82,0x5b,0x30,0xac,0xa3,0x0e,0x0e,0x97,0x8f,
         0x2f,0xaf,0x9b,0x79,0x8c,0xf7,0xf5,0x49,0x47,0x42,0xd7,0xa1,0xd3,0x30,0x95,0x6a}
    },
    {
        "hello-native", "0.2.0", "/data/apps/pkg-hello-native-0.2.0.zex", native_package::payload_zex1,
        {0x79,0x59,0x52,0xac,0xca,0xd1,0x19,0x5e,0x3d,0x48,0xbe,0xe9,0x95,0x95,0xb5,0x78,
         0x74,0x3a,0xbd,0x0d,0x49,0xe8,0x72,0xa3,0xf3,0xb2,0xdf,0xb2,0x53,0x41,0x37,0x0d},
        {0x2b,0x7b,0xa0,0x11,0x4d,0x52,0x28,0x82,0x5b,0x30,0xac,0xa3,0x0e,0x0e,0x97,0x8f,
         0x2f,0xaf,0x9b,0x79,0x8c,0xf7,0xf5,0x49,0x47,0x42,0xd7,0xa1,0xd3,0x30,0x95,0x6a}
    },
};

uint32_t authorized_package_count() {
    return static_cast<uint32_t>(sizeof(authorized_packages) / sizeof(authorized_packages[0]));
}

const AuthorizedPackage* find_authorized(const char* name, const char* version, const char* entrypoint, uint8_t payload_type) {
    for (const auto& trusted : authorized_packages) {
        if (string_equal(name, trusted.name) && string_equal(version, trusted.version) &&
            string_equal(entrypoint, trusted.entrypoint) && payload_type == trusted.payload_type) return &trusted;
    }
    return nullptr;
}

bool authorized_package(const native_package::VerifiedPackage& package) {
    if (package.manifest.dependency_count != 0U || package.manifest.conflict_count != 0U) return false;
    const AuthorizedPackage* trusted = find_authorized(package.manifest.name, package.manifest.version,
        package.manifest.entrypoint, package.manifest.payload_type);
    if (!trusted || !crypto::digest_equal(package.package_digest, trusted->package_digest)) return false;
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
