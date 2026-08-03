namespace supervisor_layout {
constexpr uintptr_t heap_start = 0x00100000U;
constexpr uintptr_t heap_end = 0x00300000U;
constexpr uintptr_t scan_workspace = 0x00300000U;
constexpr uint32_t scan_workspace_bytes = 0x00010000U;
constexpr uintptr_t zvrt_records = 0x00310000U;
constexpr uint32_t zvrt_records_bytes = 0x00001000U;
constexpr uintptr_t pmm_bitmap = 0x00311000U;
constexpr uint32_t pmm_bitmap_bytes = 0x00001000U;
constexpr uintptr_t policy_transaction = 0x00312000U;
constexpr uint32_t policy_transaction_bytes = 0x00004000U;
constexpr uintptr_t live_image_workspace = 0x00316000U;
constexpr uint32_t live_image_workspace_bytes = 0x00010000U;
constexpr uintptr_t live_overlay_map = 0x00326000U;
constexpr uint32_t live_overlay_map_bytes = 0x00010000U;
constexpr uintptr_t live_overlay_data = 0x00336000U;
constexpr uint32_t live_overlay_data_bytes = 0x00080000U;
constexpr uintptr_t live_storage_end = live_overlay_data + live_overlay_data_bytes;
constexpr uintptr_t user_base = 0x00400000U;
constexpr uintptr_t pmm_reserved_end = 0x00800000U;

static_assert(heap_start < heap_end);
static_assert(heap_end == scan_workspace);
static_assert(scan_workspace + scan_workspace_bytes == zvrt_records);
static_assert(zvrt_records + zvrt_records_bytes == pmm_bitmap);
static_assert(pmm_bitmap + pmm_bitmap_bytes == policy_transaction);
static_assert(policy_transaction + policy_transaction_bytes == live_image_workspace);
static_assert(live_image_workspace + live_image_workspace_bytes == live_overlay_map);
static_assert(live_overlay_map + live_overlay_map_bytes == live_overlay_data);
static_assert(live_storage_end <= user_base);
static_assert(user_base < pmm_reserved_end);
} // namespace supervisor_layout
