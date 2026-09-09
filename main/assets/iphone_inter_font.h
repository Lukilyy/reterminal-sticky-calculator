#pragma once

#include <cstddef>
#include <cstdint>

struct IphoneFontGlyph {
    char code;
    uint32_t bitmap_offset;
    uint16_t width;
    uint16_t height;
    int16_t bearing_x;
    int16_t bearing_y;
    uint16_t advance;
};

struct IphoneFontStrike {
    uint16_t pixel_size;
    const IphoneFontGlyph *glyphs;
    size_t glyph_count;
    const uint8_t *bitmap;
};

enum class IphoneFontSize : uint8_t {
    ResultLarge, ResultMedium, ResultSmall, ResultCompact, Expression,
    KeyNumber, KeyEmphasis,
};

const IphoneFontStrike &iphone_inter_font(IphoneFontSize size);
const IphoneFontGlyph *iphone_inter_glyph(const IphoneFontStrike &strike,
                                          char code);
