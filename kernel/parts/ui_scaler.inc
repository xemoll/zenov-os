constexpr uint32_t scaler_legacy_max_width = 1600U;
constexpr uint32_t scaler_legacy_max_height = 1200U;
constexpr uint32_t scaler_max_width = 4096U;
constexpr uint32_t scaler_max_height = 2160U;
constexpr uint32_t high_scaler_x_axis_bytes = scaler_max_width * 5U;
constexpr uint32_t high_scaler_y_axis_bytes = scaler_max_height * 5U;
constexpr uint32_t high_scaler_cache_bytes = high_scaler_x_axis_bytes + high_scaler_y_axis_bytes;

uint16_t scaler_x0[scaler_legacy_max_width]{};
uint16_t scaler_x1[scaler_legacy_max_width]{};
uint8_t scaler_x_weight[scaler_legacy_max_width]{};
uint16_t scaler_y0[scaler_legacy_max_height]{};
uint16_t scaler_y1[scaler_legacy_max_height]{};
uint8_t scaler_y_weight[scaler_legacy_max_height]{};

uint8_t* high_scaler_cache_storage = nullptr;
uint16_t* high_scaler_x0 = nullptr;
uint16_t* high_scaler_x1 = nullptr;
uint8_t* high_scaler_x_weight = nullptr;
uint16_t* high_scaler_y0 = nullptr;
uint16_t* high_scaler_y1 = nullptr;
uint8_t* high_scaler_y_weight = nullptr;
bool scaler_linear = false;
bool scaler_high_resolution = false;

bool initialize_scaler_cache() {
    if (available_framebuffer_bytes <= fallback_framebuffer_bytes) return true;
    if (high_scaler_cache_storage) return true;
    high_scaler_cache_storage = static_cast<uint8_t*>(heap::allocate(high_scaler_cache_bytes, 16U));
    if (!high_scaler_cache_storage) return false;
    uint8_t* cursor = high_scaler_cache_storage;
    high_scaler_x0 = reinterpret_cast<uint16_t*>(cursor);
    cursor += scaler_max_width * static_cast<uint32_t>(sizeof(uint16_t));
    high_scaler_x1 = reinterpret_cast<uint16_t*>(cursor);
    cursor += scaler_max_width * static_cast<uint32_t>(sizeof(uint16_t));
    high_scaler_x_weight = cursor;
    cursor += scaler_max_width;
    high_scaler_y0 = reinterpret_cast<uint16_t*>(cursor);
    cursor += scaler_max_height * static_cast<uint32_t>(sizeof(uint16_t));
    high_scaler_y1 = reinterpret_cast<uint16_t*>(cursor);
    cursor += scaler_max_height * static_cast<uint32_t>(sizeof(uint16_t));
    high_scaler_y_weight = cursor;
    cursor += scaler_max_height;
    return static_cast<uint32_t>(cursor - high_scaler_cache_storage) == high_scaler_cache_bytes;
}

bool configure_scaler() {
    scaler_linear = viewport_width < static_cast<int32_t>(width) ||
        viewport_height < static_cast<int32_t>(height) ||
        (viewport_width % static_cast<int32_t>(width)) != 0 ||
        (viewport_height % static_cast<int32_t>(height)) != 0;
    const uint32_t physical_width = static_cast<uint32_t>(viewport_width);
    const uint32_t physical_height = static_cast<uint32_t>(viewport_height);
    if (!physical_width || !physical_height ||
        physical_width > scaler_max_width || physical_height > scaler_max_height) return false;
    scaler_high_resolution = physical_width > scaler_legacy_max_width ||
        physical_height > scaler_legacy_max_height;
    if (!scaler_linear) return true;
    if (scaler_high_resolution &&
        (!high_scaler_cache_storage || !high_scaler_x0 || !high_scaler_x1 ||
         !high_scaler_x_weight || !high_scaler_y0 || !high_scaler_y1 ||
         !high_scaler_y_weight)) return false;

    for (uint32_t x = 0U; x < physical_width; ++x) {
        const uint64_t numerator = static_cast<uint64_t>(x) *
            static_cast<uint64_t>(width - 1U) * 256U;
        const uint32_t fixed = physical_width > 1U
            ? static_cast<uint32_t>(numerator / (physical_width - 1U))
            : 0U;
        const uint16_t first = static_cast<uint16_t>(fixed >> 8U);
        const uint16_t second = static_cast<uint16_t>(
            static_cast<uint32_t>(first) + 1U < width ? first + 1U : first);
        const uint8_t weight = static_cast<uint8_t>(fixed & 0xFFU);
        if (scaler_high_resolution) {
            high_scaler_x0[x] = first;
            high_scaler_x1[x] = second;
            high_scaler_x_weight[x] = weight;
        } else {
            scaler_x0[x] = first;
            scaler_x1[x] = second;
            scaler_x_weight[x] = weight;
        }
    }
    for (uint32_t y = 0U; y < physical_height; ++y) {
        const uint64_t numerator = static_cast<uint64_t>(y) *
            static_cast<uint64_t>(height - 1U) * 256U;
        const uint32_t fixed = physical_height > 1U
            ? static_cast<uint32_t>(numerator / (physical_height - 1U))
            : 0U;
        const uint16_t first = static_cast<uint16_t>(fixed >> 8U);
        const uint16_t second = static_cast<uint16_t>(
            static_cast<uint32_t>(first) + 1U < height ? first + 1U : first);
        const uint8_t weight = static_cast<uint8_t>(fixed & 0xFFU);
        if (scaler_high_resolution) {
            high_scaler_y0[y] = first;
            high_scaler_y1[y] = second;
            high_scaler_y_weight[y] = weight;
        } else {
            scaler_y0[y] = first;
            scaler_y1[y] = second;
            scaler_y_weight[y] = weight;
        }
    }
    return true;
}

uint32_t scaler_lerp(uint32_t first, uint32_t second, uint8_t weight) {
    const uint32_t inverse = 256U - static_cast<uint32_t>(weight);
    const uint32_t amount = static_cast<uint32_t>(weight);
    const uint32_t red = ((((first >> 16U) & 0xFFU) * inverse) +
        (((second >> 16U) & 0xFFU) * amount) + 128U) >> 8U;
    const uint32_t green = ((((first >> 8U) & 0xFFU) * inverse) +
        (((second >> 8U) & 0xFFU) * amount) + 128U) >> 8U;
    const uint32_t blue = (((first & 0xFFU) * inverse) +
        ((second & 0xFFU) * amount) + 128U) >> 8U;
    return (red << 16U) | (green << 8U) | blue;
}

uint32_t scaler_edge_mask(uint32_t top_left, uint32_t top_right,
                          uint32_t bottom_left, uint32_t bottom_right) {
    return (top_left ^ top_right) | (top_left ^ bottom_left) |
        (top_right ^ bottom_right) | (bottom_left ^ bottom_right);
}

bool scaler_edge_contract_ok() {
    const uint32_t hard = scaler_edge_mask(
        0x000000U, 0xFFFFFFU, 0x000000U, 0xFFFFFFU);
    const uint32_t calm = scaler_edge_mask(
        0x101112U, 0x121314U, 0x111213U, 0x131415U);
    return (hard & 0xC0C0C0U) != 0U &&
        (calm & 0x303030U) == 0U;
}

uint32_t finish_scaled_sample(uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1,
                              uint8_t wx, uint8_t wy) {
    const uint32_t top_left = backbuffer[y0 * width + x0];
    const uint32_t top_right = backbuffer[y0 * width + x1];
    const uint32_t bottom_left = backbuffer[y1 * width + x0];
    const uint32_t bottom_right = backbuffer[y1 * width + x1];
    const uint32_t top = scaler_lerp(top_left, top_right, wx);
    const uint32_t bottom = scaler_lerp(bottom_left, bottom_right, wx);
    const uint32_t linear = scaler_lerp(top, bottom, wy);
    const uint32_t edge_mask = scaler_edge_mask(
        top_left, top_right, bottom_left, bottom_right);
    if ((edge_mask & 0x303030U) == 0U) return linear;
    const uint32_t nearest_x = wx < 128U ? x0 : x1;
    const uint32_t nearest_y = wy < 128U ? y0 : y1;
    const uint32_t nearest = backbuffer[nearest_y * width + nearest_x];
    return (edge_mask & 0xC0C0C0U) != 0U
        ? mix(linear, nearest, 3U, 4U)
        : mix(linear, nearest, 1U, 2U);
}

uint32_t sample_scaled_pixel_legacy(uint32_t local_x, uint32_t local_y) {
    return finish_scaled_sample(
        scaler_x0[local_x], scaler_x1[local_x],
        scaler_y0[local_y], scaler_y1[local_y],
        scaler_x_weight[local_x], scaler_y_weight[local_y]);
}

uint32_t sample_scaled_pixel_high(uint32_t local_x, uint32_t local_y) {
    return finish_scaled_sample(
        high_scaler_x0[local_x], high_scaler_x1[local_x],
        high_scaler_y0[local_y], high_scaler_y1[local_y],
        high_scaler_x_weight[local_x], high_scaler_y_weight[local_y]);
}

uint32_t sample_scaled_pixel(uint32_t local_x, uint32_t local_y) {
    if (!scaler_linear) {
        uint32_t source_x = local_x * width / static_cast<uint32_t>(viewport_width);
        uint32_t source_y = local_y * height / static_cast<uint32_t>(viewport_height);
        if (source_x >= width) source_x = width - 1U;
        if (source_y >= height) source_y = height - 1U;
        return backbuffer[source_y * width + source_x];
    }
    if (local_x >= static_cast<uint32_t>(viewport_width) ||
        local_y >= static_cast<uint32_t>(viewport_height)) return current_theme.canvas_top;
    return scaler_high_resolution
        ? sample_scaled_pixel_high(local_x, local_y)
        : sample_scaled_pixel_legacy(local_x, local_y);
}
