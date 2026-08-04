#pragma once
#include <cstdint>
namespace heap {
inline bool acquire_mapping() { return true; }
inline void release_mapping() {}
inline const char* failure_name() { return "host"; }
inline constexpr std::uint32_t failure_index = 0U;
inline constexpr std::uint32_t failure_actual = 0U;
inline constexpr std::uint32_t failure_expected = 0U;
} // namespace heap
