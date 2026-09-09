#include "calculator_wallpaper_view.h"

#include "assets/calculator_wallpaper.h"
#include "canvas.h"

namespace {

uint8_t mono_nibble_to_gray4(uint8_t nibble)
{
    uint8_t packed = 0;
    for (int pixel = 0; pixel < 4; ++pixel) {
        const bool white = (nibble & (0x08U >> pixel)) != 0U;
        packed |= static_cast<uint8_t>((white ? 0x03U : 0x00U)
                                       << ((3 - pixel) * 2));
    }
    return packed;
}

}  // namespace

void draw_calculator_wallpaper(Canvas &canvas)
{
    static_assert(kCalculatorWallpaperWidth % 8 == 0);
    constexpr int source_stride = kCalculatorWallpaperWidth / 8;

    uint8_t *destination = canvas.data();
    const size_t destination_stride = canvas.stride();
    for (int y = 0; y < kCalculatorWallpaperHeight; ++y) {
        const uint8_t *source = kCalculatorWallpaper + y * source_stride;
        uint8_t *row = destination + static_cast<size_t>(y) * destination_stride;
        for (int byte = 0; byte < source_stride; ++byte) {
            row[byte * 2] = mono_nibble_to_gray4(source[byte] >> 4);
            row[byte * 2 + 1] = mono_nibble_to_gray4(source[byte] & 0x0FU);
        }
    }
}
