namespace rsa_pss {
constexpr uint32_t signature_bytes = 256U;
struct BigInteger { uint32_t limb[64]; };

void big_zero(BigInteger& value) { memset(value.limb, 0, sizeof(value.limb)); }
void big_from_be(BigInteger& value, const uint8_t input[signature_bytes]) {
    big_zero(value);
    for (uint32_t i = 0; i < signature_bytes; ++i) {
        const uint32_t reverse = signature_bytes - 1U - i;
        value.limb[reverse / 4U] |= static_cast<uint32_t>(input[i]) << ((reverse % 4U) * 8U);
    }
}
void big_to_be(const BigInteger& value, uint8_t output[signature_bytes]) {
    for (uint32_t i = 0; i < signature_bytes; ++i) {
        const uint32_t reverse = signature_bytes - 1U - i;
        output[i] = static_cast<uint8_t>(value.limb[reverse / 4U] >> ((reverse % 4U) * 8U));
    }
}
int32_t big_compare(const BigInteger& left, const BigInteger& right) {
    for (uint32_t i = 64U; i > 0; --i) {
        const uint32_t index = i - 1U;
        if (left.limb[index] < right.limb[index]) return -1;
        if (left.limb[index] > right.limb[index]) return 1;
    }
    return 0;
}
void big_subtract(BigInteger& left, const BigInteger& right) {
    unsigned long long borrow = 0;
    for (uint32_t i = 0; i < 64U; ++i) {
        const unsigned long long current = left.limb[i];
        const unsigned long long subtrahend = static_cast<unsigned long long>(right.limb[i]) + borrow;
        left.limb[i] = static_cast<uint32_t>(current - subtrahend);
        borrow = current < subtrahend ? 1ULL : 0ULL;
    }
}
void big_add_plain(BigInteger& output, const BigInteger& left, const BigInteger& right) {
    unsigned long long carry = 0;
    for (uint32_t i = 0; i < 64U; ++i) {
        const unsigned long long sum = static_cast<unsigned long long>(left.limb[i]) + right.limb[i] + carry;
        output.limb[i] = static_cast<uint32_t>(sum);
        carry = sum >> 32U;
    }
}
void big_add_mod(BigInteger& output, const BigInteger& left, const BigInteger& right, const BigInteger& modulus) {
    BigInteger threshold = modulus;
    big_subtract(threshold, right);
    if (big_compare(left, threshold) >= 0) {
        output = left;
        big_subtract(output, threshold);
    } else {
        big_add_plain(output, left, right);
    }
}
bool big_bit(const BigInteger& value, uint32_t bit) {
    return ((value.limb[bit / 32U] >> (bit % 32U)) & 1U) != 0;
}
void big_multiply_mod(BigInteger& output, const BigInteger& left, const BigInteger& right, const BigInteger& modulus) {
    BigInteger result{}, current = left, temporary{};
    for (uint32_t bit = 0; bit < 2048U; ++bit) {
        if (big_bit(right, bit)) {
            big_add_mod(temporary, result, current, modulus);
            result = temporary;
        }
        big_add_mod(temporary, current, current, modulus);
        current = temporary;
    }
    output = result;
}
void big_exp_65537(BigInteger& output, const BigInteger& signature, const BigInteger& modulus) {
    BigInteger result{}, temporary{};
    result.limb[0] = 1U;
    for (int32_t bit = 16; bit >= 0; --bit) {
        big_multiply_mod(temporary, result, result, modulus);
        result = temporary;
        if ((65537U >> static_cast<uint32_t>(bit)) & 1U) {
            big_multiply_mod(temporary, result, signature, modulus);
            result = temporary;
        }
    }
    output = result;
}
void mgf1_sha256(const uint8_t seed[32], uint8_t* output, uint32_t size) {
    uint32_t offset = 0, counter = 0;
    while (offset < size) {
        uint8_t counter_be[4] = {
            static_cast<uint8_t>(counter >> 24U), static_cast<uint8_t>(counter >> 16U),
            static_cast<uint8_t>(counter >> 8U), static_cast<uint8_t>(counter)
        };
        uint8_t block[32];
        security_guard::Sha256Context context{};
        security_guard::sha256_init(context);
        security_guard::sha256_update(context, seed, 32U);
        security_guard::sha256_update(context, counter_be, 4U);
        security_guard::sha256_final(context, block);
        uint32_t copy = size - offset;
        if (copy > 32U) copy = 32U;
        memcpy(output + offset, block, copy);
        offset += copy;
        ++counter;
    }
}
bool verify(const uint8_t signature[signature_bytes], const uint8_t digest[32], const uint8_t modulus_bytes[signature_bytes]) {
    BigInteger encoded_signature{}, modulus{}, decoded{};
    big_from_be(encoded_signature, signature);
    big_from_be(modulus, modulus_bytes);
    if ((modulus_bytes[0] & 0x80U) == 0 || big_compare(encoded_signature, modulus) >= 0) return false;
    big_exp_65537(decoded, encoded_signature, modulus);
    uint8_t encoded[signature_bytes];
    big_to_be(decoded, encoded);
    constexpr uint32_t database_bytes = 223U, padding_bytes = 190U;
    if (encoded[255] != 0xBCU || (encoded[0] & 0x80U) != 0) return false;
    const uint8_t* encoded_hash = encoded + database_bytes;
    uint8_t mask[database_bytes], database[database_bytes];
    mgf1_sha256(encoded_hash, mask, database_bytes);
    for (uint32_t i = 0; i < database_bytes; ++i) database[i] = static_cast<uint8_t>(encoded[i] ^ mask[i]);
    database[0] = static_cast<uint8_t>(database[0] & 0x7FU);
    uint8_t malformed = 0;
    for (uint32_t i = 0; i < padding_bytes; ++i) malformed = static_cast<uint8_t>(malformed | database[i]);
    malformed = static_cast<uint8_t>(malformed | static_cast<uint8_t>(database[padding_bytes] ^ 0x01U));
    uint8_t message[72]{};
    memcpy(message + 8U, digest, 32U);
    memcpy(message + 40U, database + padding_bytes + 1U, 32U);
    uint8_t expected_hash[32];
    security_guard::sha256(message, sizeof(message), expected_hash);
    return malformed == 0 && security_guard::digest_equal(encoded_hash, expected_hash);
}
} // namespace rsa_pss
