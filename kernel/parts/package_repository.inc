#include "../../security/zenrepo_crypto_material.hpp"

namespace package_repository {
#include "package_repository/format_state.inc"
alignas(16) uint8_t metadata_storage[maximum_metadata_bytes]{};
#include "package_repository/envelope_root.inc"
#include "package_repository/chain.inc"
#include "package_repository/runtime.inc"
} // namespace package_repository
