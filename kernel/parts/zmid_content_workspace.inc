namespace zmid_content_workspace_allocator {
uint32_t allocations = 0U;

void* allocate(uint32_t bytes, uint32_t alignment) {
    if (bytes != supervisor_layout::zmid_workspace_bytes || !alignment ||
        (alignment & (alignment - 1U)) != 0U || alignment > 4096U || allocations >= 2U) {
        return nullptr;
    }
    const uintptr_t address = allocations++ == 0U
        ? supervisor_layout::zmid_workspace_a
        : supervisor_layout::zmid_workspace_b;
    if ((address & (alignment - 1U)) != 0U) return nullptr;
    memset(reinterpret_cast<void*>(address), 0, bytes);
    return reinterpret_cast<void*>(address);
}
} // namespace zmid_content_workspace_allocator