namespace crypto {

struct Sha256Context {
    uint32_t state[8];
    uint8_t block[64];
    unsigned long long total_bytes;
    uint32_t block_size;
};

constexpr uint32_t sha256_constants[64] = {
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
    0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
    0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
    0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
    0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
    0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
    0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
    0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
};

uint32_t rotate_right(uint32_t value, uint32_t count) {
    return (value >> count) | (value << (32U - count));
}

void sha256_transform(Sha256Context& context, const uint8_t* input) {
    uint32_t words[64]{};
    for (uint32_t i = 0; i < 16U; ++i) {
        words[i] = (static_cast<uint32_t>(input[i * 4U]) << 24U) |
                   (static_cast<uint32_t>(input[i * 4U + 1U]) << 16U) |
                   (static_cast<uint32_t>(input[i * 4U + 2U]) << 8U) |
                   static_cast<uint32_t>(input[i * 4U + 3U]);
    }
    for (uint32_t i = 16U; i < 64U; ++i) {
        const uint32_t s0 = rotate_right(words[i - 15U], 7U) ^ rotate_right(words[i - 15U], 18U) ^ (words[i - 15U] >> 3U);
        const uint32_t s1 = rotate_right(words[i - 2U], 17U) ^ rotate_right(words[i - 2U], 19U) ^ (words[i - 2U] >> 10U);
        words[i] = words[i - 16U] + s0 + words[i - 7U] + s1;
    }

    uint32_t a = context.state[0], b = context.state[1], c = context.state[2], d = context.state[3];
    uint32_t e = context.state[4], f = context.state[5], g = context.state[6], h = context.state[7];
    for (uint32_t i = 0; i < 64U; ++i) {
        const uint32_t s1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
        const uint32_t choose = (e & f) ^ ((~e) & g);
        const uint32_t temp1 = h + s1 + choose + sha256_constants[i] + words[i];
        const uint32_t s0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = s0 + majority;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    context.state[0] += a; context.state[1] += b; context.state[2] += c; context.state[3] += d;
    context.state[4] += e; context.state[5] += f; context.state[6] += g; context.state[7] += h;
}

void sha256_init(Sha256Context& context) {
    context.state[0] = 0x6a09e667U; context.state[1] = 0xbb67ae85U;
    context.state[2] = 0x3c6ef372U; context.state[3] = 0xa54ff53aU;
    context.state[4] = 0x510e527fU; context.state[5] = 0x9b05688cU;
    context.state[6] = 0x1f83d9abU; context.state[7] = 0x5be0cd19U;
    memset(context.block, 0, sizeof(context.block));
    context.total_bytes = 0;
    context.block_size = 0;
}

void sha256_update(Sha256Context& context, const uint8_t* data, uint32_t size) {
    if (!data && size) return;
    context.total_bytes += size;
    while (size) {
        uint32_t available = 64U - context.block_size;
        uint32_t chunk = size < available ? size : available;
        memcpy(context.block + context.block_size, data, chunk);
        context.block_size += chunk;
        data += chunk;
        size -= chunk;
        if (context.block_size == 64U) {
            sha256_transform(context, context.block);
            context.block_size = 0;
        }
    }
}

void sha256_final(Sha256Context& context, uint8_t output[32]) {
    const unsigned long long total_bits = context.total_bytes * 8ULL;
    context.block[context.block_size++] = 0x80U;
    if (context.block_size > 56U) {
        while (context.block_size < 64U) context.block[context.block_size++] = 0;
        sha256_transform(context, context.block);
        context.block_size = 0;
    }
    while (context.block_size < 56U) context.block[context.block_size++] = 0;
    for (int shift = 56; shift >= 0; shift -= 8) context.block[context.block_size++] = static_cast<uint8_t>(total_bits >> shift);
    sha256_transform(context, context.block);
    for (uint32_t i = 0; i < 8U; ++i) {
        output[i * 4U] = static_cast<uint8_t>(context.state[i] >> 24U);
        output[i * 4U + 1U] = static_cast<uint8_t>(context.state[i] >> 16U);
        output[i * 4U + 2U] = static_cast<uint8_t>(context.state[i] >> 8U);
        output[i * 4U + 3U] = static_cast<uint8_t>(context.state[i]);
    }
}

void sha256(const uint8_t* data, uint32_t size, uint8_t output[32]) {
    Sha256Context context{};
    sha256_init(context);
    sha256_update(context, data, size);
    sha256_final(context, output);
}

bool digest_equal(const uint8_t left[32], const uint8_t right[32]) {
    uint8_t difference = 0;
    for (uint32_t i = 0; i < 32U; ++i) difference |= static_cast<uint8_t>(left[i] ^ right[i]);
    return difference == 0;
}

bool sha256_self_test() {
    static const uint8_t expected[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
    };
    const uint8_t sample[3] = {'a','b','c'};
    uint8_t actual[32]{};
    sha256(sample, sizeof(sample), actual);
    return digest_equal(actual, expected);
}

} // namespace crypto
