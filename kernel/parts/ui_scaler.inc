constexpr uint32_t scaler_max_width = 1600U;
constexpr uint32_t scaler_max_height = 1200U;
uint16_t scaler_x0[scaler_max_width]{};
uint16_t scaler_x1[scaler_max_width]{};
uint8_t scaler_x_weight[scaler_max_width]{};
uint16_t scaler_y0[scaler_max_height]{};
uint16_t scaler_y1[scaler_max_height]{};
uint8_t scaler_y_weight[scaler_max_height]{};
bool scaler_linear = false;

void configure_scaler() {
    scaler_linear = viewport_width < static_cast<int32_t>(width) || viewport_height < static_cast<int32_t>(height) ||
        (viewport_width % static_cast<int32_t>(width)) != 0 || (viewport_height % static_cast<int32_t>(height)) != 0;
    if (!scaler_linear) return;

    const uint32_t physical_width = static_cast<uint32_t>(viewport_width);
    const uint32_t physical_height = static_cast<uint32_t>(viewport_height);
    for (uint32_t x = 0; x < physical_width && x < scaler_max_width; ++x) {
        const uint32_t fixed = physical_width > 1U
            ? (x * (width - 1U) * 256U) / (physical_width - 1U)
            : 0U;
        const uint32_t first = fixed >> 8U;
        scaler_x0[x] = static_cast<uint16_t>(first);
        scaler_x1[x] = static_cast<uint16_t>(first + 1U < width ? first + 1U : first);
        scaler_x_weight[x] = static_cast<uint8_t>(fixed & 0xFFU);
    }
    for (uint32_t y = 0; y < physical_height && y < scaler_max_height; ++y) {
        const uint32_t fixed = physical_height > 1U
            ? (y * (height - 1U) * 256U) / (physical_height - 1U)
            : 0U;
        const uint32_t first = fixed >> 8U;
        scaler_y0[y] = static_cast<uint16_t>(first);
        scaler_y1[y] = static_cast<uint16_t>(first + 1U < height ? first + 1U : first);
        scaler_y_weight[y] = static_cast<uint8_t>(fixed & 0xFFU);
    }
}

uint32_t scaler_lerp(uint32_t first, uint32_t second, uint8_t weight) {
    const uint32_t inverse = 256U - static_cast<uint32_t>(weight);
    const uint32_t amount = static_cast<uint32_t>(weight);
    const uint32_t red = ((((first >> 16U) & 0xFFU) * inverse) + (((second >> 16U) & 0xFFU) * amount) + 128U) >> 8U;
    const uint32_t green = ((((first >> 8U) & 0xFFU) * inverse) + (((second >> 8U) & 0xFFU) * amount) + 128U) >> 8U;
    const uint32_t blue = (((first & 0xFFU) * inverse) + ((second & 0xFFU) * amount) + 128U) >> 8U;
    return (red << 16U) | (green << 8U) | blue;
}

uint32_t scaler_abs_delta(uint32_t first, uint32_t second) { return first > second ? first - second : second - first; }

uint32_t scaler_color_delta(uint32_t first, uint32_t second) {
    const uint32_t red = scaler_abs_delta((first >> 16U) & 0xFFU, (second >> 16U) & 0xFFU);
    const uint32_t green = scaler_abs_delta((first >> 8U) & 0xFFU, (second >> 8U) & 0xFFU);
    const uint32_t blue = scaler_abs_delta(first & 0xFFU, second & 0xFFU);
    const uint32_t red_green = red > green ? red : green;
    return red_green > blue ? red_green : blue;
}

uint32_t scaler_edge_strength(uint32_t top_left, uint32_t top_right, uint32_t bottom_left, uint32_t bottom_right) {
    uint32_t strength = scaler_color_delta(top_left, top_right);
    const uint32_t left = scaler_color_delta(top_left, bottom_left);
    const uint32_t right = scaler_color_delta(top_right, bottom_right);
    const uint32_t bottom = scaler_color_delta(bottom_left, bottom_right);
    if (left > strength) strength = left;
    if (right > strength) strength = right;
    if (bottom > strength) strength = bottom;
    return strength;
}

bool scaler_edge_contract_ok() {
    return scaler_edge_strength(0x000000U, 0xFFFFFFU, 0x000000U, 0xFFFFFFU) == 255U &&
        scaler_edge_strength(0x101112U, 0x121314U, 0x111213U, 0x131415U) < 8U;
}

uint32_t sample_scaled_pixel(uint32_t local_x, uint32_t local_y) {
    if (!scaler_linear) {
        uint32_t source_x = local_x * width / static_cast<uint32_t>(viewport_width);
        uint32_t source_y = local_y * height / static_cast<uint32_t>(viewport_height);
        if (source_x >= width) source_x = width - 1U;
        if (source_y >= height) source_y = height - 1U;
        return backbuffer[source_y * width + source_x];
    }
    if (local_x >= scaler_max_width) local_x = scaler_max_width - 1U;
    if (local_y >= scaler_max_height) local_y = scaler_max_height - 1U;
    const uint32_t x0 = scaler_x0[local_x], x1 = scaler_x1[local_x];
    const uint32_t y0 = scaler_y0[local_y], y1 = scaler_y1[local_y];
    const uint8_t wx = scaler_x_weight[local_x], wy = scaler_y_weight[local_y];
    const uint32_t top_left = backbuffer[y0 * width + x0];
    const uint32_t top_right = backbuffer[y0 * width + x1];
    const uint32_t bottom_left = backbuffer[y1 * width + x0];
    const uint32_t bottom_right = backbuffer[y1 * width + x1];
    const uint32_t top = scaler_lerp(top_left, top_right, wx);
    const uint32_t bottom = scaler_lerp(bottom_left, bottom_right, wx);
    const uint32_t linear = scaler_lerp(top, bottom, wy);
    const uint32_t nearest_x = wx < 128U ? x0 : x1;
    const uint32_t nearest_y = wy < 128U ? y0 : y1;
    const uint32_t nearest = backbuffer[nearest_y * width + nearest_x];
    const uint32_t edge = scaler_edge_strength(top_left, top_right, bottom_left, bottom_right);
    if (edge >= 96U) return mix(linear, nearest, 3U, 4U);
    if (edge >= 48U) return mix(linear, nearest, 1U, 2U);
    return linear;
}
