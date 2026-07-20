extern "C" uint64_t __udivdi3(uint64_t numerator, uint64_t denominator) {
    if (!denominator) return 0;
    if ((numerator >> 32U) == 0U && (denominator >> 32U) == 0U) {
        return static_cast<uint32_t>(numerator) / static_cast<uint32_t>(denominator);
    }
    uint64_t quotient = 0;
    uint64_t remainder = 0;
    for (int32_t bit = 63; bit >= 0; --bit) {
        remainder = (remainder << 1U) | ((numerator >> static_cast<uint32_t>(bit)) & 1U);
        if (remainder >= denominator) {
            remainder -= denominator;
            quotient |= static_cast<uint64_t>(1U) << static_cast<uint32_t>(bit);
        }
    }
    return quotient;
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
