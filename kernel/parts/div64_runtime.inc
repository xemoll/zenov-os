extern "C" uint64_t __udivmoddi4(uint64_t numerator, uint64_t denominator, uint64_t* remainder_output) {
    if (!denominator) {
        if (remainder_output) *remainder_output = 0U;
        return 0U;
    }
    if ((numerator >> 32U) == 0U && (denominator >> 32U) == 0U) {
        const uint32_t numerator32 = static_cast<uint32_t>(numerator);
        const uint32_t denominator32 = static_cast<uint32_t>(denominator);
        if (remainder_output) *remainder_output = numerator32 % denominator32;
        return numerator32 / denominator32;
    }
    uint64_t quotient = 0U;
    uint64_t remainder = 0U;
    for (int32_t bit = 63; bit >= 0; --bit) {
        remainder = (remainder << 1U) | ((numerator >> static_cast<uint32_t>(bit)) & 1U);
        if (remainder >= denominator) {
            remainder -= denominator;
            quotient |= static_cast<uint64_t>(1U) << static_cast<uint32_t>(bit);
        }
    }
    if (remainder_output) *remainder_output = remainder;
    return quotient;
}

extern "C" uint64_t __udivdi3(uint64_t numerator, uint64_t denominator) {
    return __udivmoddi4(numerator, denominator, nullptr);
}

extern "C" uint64_t __umoddi3(uint64_t numerator, uint64_t denominator) {
    uint64_t remainder = 0U;
    (void)__udivmoddi4(numerator, denominator, &remainder);
    return remainder;
}

extern "C" int64_t __divdi3(int64_t numerator, int64_t denominator) {
    if (!denominator) return 0;
    const bool negative = (numerator < 0) != (denominator < 0);
    const uint64_t unsigned_numerator = numerator < 0
        ? static_cast<uint64_t>(-(numerator + 1)) + 1U
        : static_cast<uint64_t>(numerator);
    const uint64_t unsigned_denominator = denominator < 0
        ? static_cast<uint64_t>(-(denominator + 1)) + 1U
        : static_cast<uint64_t>(denominator);
    const uint64_t quotient = __udivdi3(unsigned_numerator, unsigned_denominator);
    if (!negative) return static_cast<int64_t>(quotient);
    return -static_cast<int64_t>(quotient - 1U) - 1;
}
