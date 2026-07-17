namespace storage {
bool bytes_equal(const uint8_t* left, const uint8_t* right, uint32_t count) {
    uint8_t difference = 0;
    for (uint32_t i = 0; i < count; ++i) difference = static_cast<uint8_t>(difference | (left[i] ^ right[i]));
    return difference == 0;
}
} // namespace storage
