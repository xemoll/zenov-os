static constexpr Glyph lowercase_glyphs[] = {
    {'a',{0,0,14,1,15,17,15}}, {'b',{16,16,30,17,17,17,30}}, {'c',{0,0,14,17,16,17,14}},
    {'d',{1,1,15,17,17,17,15}}, {'e',{0,0,14,17,31,16,14}}, {'f',{6,9,8,28,8,8,8}},
    {'g',{0,15,17,17,15,1,14}}, {'h',{16,16,30,17,17,17,17}}, {'i',{4,0,12,4,4,4,14}},
    {'j',{2,0,6,2,2,18,12}}, {'k',{16,16,18,20,24,20,18}}, {'l',{12,4,4,4,4,4,14}},
    {'m',{0,0,26,21,21,21,21}}, {'n',{0,0,30,17,17,17,17}}, {'o',{0,0,14,17,17,17,14}},
    {'p',{0,0,30,17,30,16,16}}, {'q',{0,0,15,17,15,1,1}}, {'r',{0,0,22,25,16,16,16}},
    {'s',{0,0,15,16,14,1,30}}, {'t',{8,8,28,8,8,9,6}}, {'u',{0,0,17,17,17,19,13}},
    {'v',{0,0,17,17,17,10,4}}, {'w',{0,0,17,17,21,21,10}}, {'x',{0,0,17,10,4,10,17}},
    {'y',{0,0,17,17,15,1,14}}, {'z',{0,0,31,2,4,8,31}}
};

const Glyph* lowercase_glyph_for(char value) {
    for (uint32_t index = 0; index < sizeof(lowercase_glyphs) / sizeof(lowercase_glyphs[0]); ++index)
        if (lowercase_glyphs[index].value == value) return &lowercase_glyphs[index];
    return nullptr;
}

bool font_cell_on(const Glyph* glyph, int32_t row, int32_t column) {
    if (!glyph || row < 0 || row >= 7 || column < 0 || column >= 5) return false;
    return (glyph->rows[static_cast<uint32_t>(row)] &
            (1U << static_cast<uint32_t>(4 - column))) != 0U;
}

uint8_t font_soft_coverage(const Glyph* glyph, int32_t row, int32_t column) {
    if (font_cell_on(glyph, row, column)) return 255U;
    const uint8_t diagonal = static_cast<uint8_t>(font_cell_on(glyph, row - 1, column - 1)) +
        static_cast<uint8_t>(font_cell_on(glyph, row - 1, column + 1)) +
        static_cast<uint8_t>(font_cell_on(glyph, row + 1, column - 1)) +
        static_cast<uint8_t>(font_cell_on(glyph, row + 1, column + 1));
    const uint8_t orthogonal = static_cast<uint8_t>(font_cell_on(glyph, row - 1, column)) +
        static_cast<uint8_t>(font_cell_on(glyph, row + 1, column)) +
        static_cast<uint8_t>(font_cell_on(glyph, row, column - 1)) +
        static_cast<uint8_t>(font_cell_on(glyph, row, column + 1));
    if (diagonal >= 2U && orthogonal == 0U) return 48U;
    if (diagonal >= 1U && orthogonal >= 1U) return 30U;
    return 0U;
}
struct FontGlyphMetrics {
    int32_t left;
    int32_t right;
    uint8_t width;
    uint8_t advance;
};

FontGlyphMetrics font_glyph_metrics(const Glyph* glyph) {
    if (!glyph) return FontGlyphMetrics{0, -1, 0U, 0U};
    int32_t left = 5;
    int32_t right = -1;
    for (int32_t row = 0; row < 7; ++row) {
        for (int32_t column = 0; column < 5; ++column) {
            if (!font_cell_on(glyph, row, column)) continue;
            if (column < left) left = column;
            if (column > right) right = column;
        }
    }
    if (right < left) return FontGlyphMetrics{0, -1, 0U, 4U};
    const uint8_t ink_width = static_cast<uint8_t>(right - left + 1);
    const uint8_t advance = ink_width >= 5U ? 6U : static_cast<uint8_t>(ink_width + 1U);
    return FontGlyphMetrics{static_cast<int32_t>(left), static_cast<int32_t>(right), ink_width, advance};
}
