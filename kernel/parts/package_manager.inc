namespace package_manager {
constexpr uint32_t maximum_package_bytes = 64U * 1024U;
alignas(16) uint8_t package_storage[maximum_package_bytes]{};
#include "package_foreign_format.inc"
#include "package_manager/state.inc"
#include "package_manager/authorization.inc"
#include "package_manager/policy.inc"
#include "package_manager/transactions.inc"
#include "package_manager/cache.inc"
#include "package_manager/formats.inc"
#include "package_manager/commands_query.inc"
#include "package_manager/commands_lifecycle.inc"
#include "package_manager/commands_dispatch.inc"
#include "package_manager/init.inc"
} // namespace package_manager
