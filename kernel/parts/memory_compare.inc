extern "C" int memcmp(const void* left, const void* right, size_t count) {
    const auto* first = static_cast<const unsigned char*>(left);
    const auto* second = static_cast<const unsigned char*>(right);
    for (size_t i = 0; i < count; ++i) {
        if (first[i] < second[i]) return -1;
        if (first[i] > second[i]) return 1;
    }
    return 0;
}
