#include <stdint.h>

#include "../kernel/parts/div64_runtime.inc"

extern "C" uint64_t div64_runtime_probe(uint64_t value, uint32_t divisor, uint64_t* remainder) {
    if (!divisor) {
        if (remainder) *remainder = 0U;
        return 0U;
    }
    if (remainder) *remainder = value % divisor;
    return value / divisor;
}

#ifndef DIV64_LINK_ONLY
#include <cassert>
#include <cstdio>
#include <limits>

namespace {
void verify(uint64_t numerator, uint64_t denominator) {
    uint64_t remainder = 0xFFFFFFFFFFFFFFFFULL;
    const uint64_t quotient = __udivmoddi4(numerator, denominator, &remainder);
    if (!denominator) {
        assert(quotient == 0U);
        assert(remainder == 0U);
        return;
    }
    assert(quotient == numerator / denominator);
    assert(remainder == numerator % denominator);
    assert(__udivdi3(numerator, denominator) == quotient);
    assert(__umoddi3(numerator, denominator) == remainder);
}
} // namespace

int main() {
    verify(0U, 1U);
    verify(1U, 1U);
    verify(0xFFFFFFFFULL, 10U);
    verify(0x100000000ULL, 3U);
    verify(0xFFFFFFFFFFFFFFFFULL, 10U);
    verify(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFULL);
    verify(0xFFFFFFFFFFFFFFFFULL, 0x100000001ULL);
    verify(0x8000000000000000ULL, 0x7FFFFFFFULL);
    verify(std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max());
    verify(123U, 0U);

    uint64_t remainder = 0U;
    assert(div64_runtime_probe(0x100000000ULL, 1000U, &remainder) == 4294967U);
    assert(remainder == 296U);

    std::puts("DIV64_RUNTIME_OK abi=udivdi3+udivmoddi4+umoddi3 edges=64bit freestanding=link-closed");
    return 0;
}
#endif
