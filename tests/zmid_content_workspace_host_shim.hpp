#pragma once
#include <cstdint>

namespace paging {
inline constexpr std::uint32_t supervisor_hole_directory_index = 2U;
inline constexpr std::uint32_t user_directory_index = 256U;
static_assert(supervisor_hole_directory_index != user_directory_index);
} // namespace paging

namespace zmid_content_workspace_allocator {
inline constexpr std::uint32_t directory_index = 2U;
static_assert(directory_index == paging::supervisor_hole_directory_index);
} // namespace zmid_content_workspace_allocator

namespace heap {
inline bool acquire_mapping() { return true; }
inline void release_mapping() {}
inline const char* failure_name() { return "host"; }
inline constexpr std::uint32_t failure_index = 0U;
inline constexpr std::uint32_t failure_actual = 0U;
inline constexpr std::uint32_t failure_expected = 0U;
} // namespace heap

namespace serial {
inline void put(char) {}
inline void write(const char*) {}
} // namespace serial
