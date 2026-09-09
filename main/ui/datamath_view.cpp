#include "datamath_view.h"

#include <cstring>

#include "assets/ti_logo.h"
#include "canvas.h"
#include "font.h"

namespace {
constexpr int kPanelX = 8;
constexpr int kPanelY = 8;
constexpr int kPanelWidth = 464;
constexpr int kPanelHeight = 784;
constexpr int kPanelTopRadius = 28;
constexpr int kPanelBottomRadius = 14;

constexpr int kDisplayX = 26;
constexpr int kDisplayY = 24;
constexpr int kDisplayWidth = 428;
constexpr int kDisplayHeight = 132;
constexpr int kDisplayRadius = 6;
constexpr int kDigitWidth = 32;
constexpr int kDigitHeight = 56;
constexpr int kSegmentThickness = 6;
constexpr int kDigitGap = 8;
constexpr int kDisplaySlots = 8;
constexpr int kDisplayRightPadding = 24;
constexpr int kDecimalSize = 6;

constexpr int kBrandCenterY = 199;
constexpr int kKeyboardX = 40;
constexpr int kKeyboardY = 238;
constexpr int kKeyWidth = 88;
constexpr int kKeyHeight = 84;
constexpr int kKeyGapX = 16;
constexpr int kKeyGapY = 20;
constexpr int kBottomRowX = kKeyboardX;
constexpr int kWideKeyWidth = 140;
constexpr int kKeyRadius = 5;
constexpr int kKeyOutline = 2;

constexpr int key_x(int column)
{
    return kKeyboardX + column * (kKeyWidth + kKeyGapX);
}

constexpr int key_y(int row)
{
    return kKeyboardY + row * (kKeyHeight + kKeyGapY);
}

constexpr CalculatorKeyLayout kKeys[] = {
    {CalculatorKeyId::ClearEntry, "CE/D", key_x(0), key_y(0), kKeyWidth, kKeyHeight},
    {CalculatorKeyId::Divide, "", key_x(1), key_y(0), kKeyWidth, kKeyHeight},
    {CalculatorKeyId::Multiply, "", key_x(2), key_y(0), kKeyWidth, kKeyHeight},
    {CalculatorKeyId::Switch, "", key_x(3), key_y(0), kKeyWidth, kKeyHeight},
    {CalculatorKeyId::Seven, "7", key_x(0), key_y(1), kKeyWidth, kKeyHeight},
    {CalculatorKeyId::Eight, "8", key_x(1), key_y(1), kKeyWidth, kKeyHeight},
    {CalculatorKeyId::Nine, "9", key_x(2), key_y(1), kKeyWidth, kKeyHeight},
    {CalculatorKeyId::Clear, "C", key_x(3), key_y(1), kKeyWidth, kKeyHeight},
    {CalculatorKeyId::Four, "4", key_x(0), key_y(2), kKeyWidth, kKeyHeight},
    {CalculatorKeyId::Five, "5", key_x(1), key_y(2), kKeyWidth, kKeyHeight},
    {CalculatorKeyId::Six, "6", key_x(2), key_y(2), kKeyWidth, kKeyHeight},
    {CalculatorKeyId::Subtract, "", key_x(3), key_y(2), kKeyWidth, kKeyHeight},
    {CalculatorKeyId::One, "1", key_x(0), key_y(3), kKeyWidth, kKeyHeight},
    {CalculatorKeyId::Two, "2", key_x(1), key_y(3), kKeyWidth, kKeyHeight},
    {CalculatorKeyId::Three, "3", key_x(2), key_y(3), kKeyWidth, kKeyHeight},
    {CalculatorKeyId::Add, "", key_x(3), key_y(3), kKeyWidth, kKeyHeight},
    {CalculatorKeyId::Zero, "0", kBottomRowX, key_y(4),
     kWideKeyWidth, kKeyHeight},
    {CalculatorKeyId::Decimal, ".", kBottomRowX + kWideKeyWidth + kKeyGapX,
     key_y(4), kKeyWidth, kKeyHeight},
    {CalculatorKeyId::Equals, "",
     kBottomRowX + kWideKeyWidth + kKeyGapX + kKeyWidth + kKeyGapX,
     key_y(4), kWideKeyWidth, kKeyHeight},
};

int corner_inset(int radius, int row)
{
    const int dy = radius - row - 1;
    int inset = 0;
    while (inset < radius) {
        const int dx = radius - inset;
        if (dx * dx + dy * dy <= radius * radius) break;
        ++inset;
    }
    return inset;
}

void fill_rounded_rect(Canvas &canvas, int x, int y, int width, int height,
                       int radius, GrayLevel color)
{
    if (width <= 0 || height <= 0) return;
    for (int row = 0; row < height; ++row) {
        const int edge_row = row < radius ? row : height - row - 1;
        const int inset = edge_row < radius ? corner_inset(radius, edge_row) : 0;
        canvas.fill_rect(x + inset, y + row, width - 2 * inset, 1, color);
    }
}

void fill_panel(Canvas &canvas)
{
    for (int row = 0; row < kPanelHeight; ++row) {
        int inset = 0;
        if (row < kPanelTopRadius) {
            inset = corner_inset(kPanelTopRadius, row);
        } else if (row >= kPanelHeight - kPanelBottomRadius) {
            inset = corner_inset(kPanelBottomRadius, kPanelHeight - row - 1);
        }
        canvas.fill_rect(kPanelX + inset, kPanelY + row,
                         kPanelWidth - 2 * inset, 1, GrayLevel::Black);
    }
}

void horizontal_segment(Canvas &canvas, int x, int y, bool white)
{
    canvas.fill_rect(x + kSegmentThickness / 2, y,
                     kDigitWidth - kSegmentThickness, kSegmentThickness,
                     white ? GrayLevel::White : GrayLevel::Black);
}

void vertical_segment(Canvas &canvas, int x, int y, bool white)
{
    canvas.fill_rect(x, y + kSegmentThickness / 2, kSegmentThickness,
                     kDigitHeight / 2 - kSegmentThickness,
                     white ? GrayLevel::White : GrayLevel::Black);
}

void draw_wordmark(Canvas &canvas, int x, int y, const char *text)
{
    // Dedicated 16x20 nearest-neighbour glyphs: larger than 2x text, but still
    // crisp and drawn exactly once so adjacent letters cannot blur together.
    constexpr int glyph_width = 16;
    constexpr int glyph_height = 20;
    constexpr int advance = 19;
    int cursor_x = x;
    for (const char *character = text; *character != '\0'; ++character) {
        const FontGlyph &glyph = font_get_glyph(*character);
        for (int dest_x = 0; dest_x < glyph_width; ++dest_x) {
            const int source_x = dest_x * kFontWidth / glyph_width;
            for (int dest_y = 0; dest_y < glyph_height; ++dest_y) {
                const int source_y = dest_y * kFontHeight / glyph_height;
                if ((glyph.columns[source_x] & (1U << source_y)) != 0U) {
                    canvas.draw_pixel(cursor_x + dest_x, y + dest_y,
                                      GrayLevel::White);
                }
            }
        }
        cursor_x += advance;
    }
}

void draw_ti_logo(Canvas &canvas, int x, int y)
{
    for (int row = 0; row < kTiLogoHeight; ++row) {
        for (int column = 0; column < kTiLogoWidth; ++column) {
            const uint8_t byte =
                kTiLogoBitmap[row * kTiLogoStride + column / 8];
            if ((byte & (0x80U >> (column & 7))) != 0U) {
                canvas.draw_pixel(x + column, y + row, GrayLevel::White);
            }
        }
    }
}
}  // namespace

DatamathView::DatamathView(Canvas &canvas) : canvas_(canvas) {}

const CalculatorKeyLayout *DatamathView::keys(size_t &count)
{
    count = sizeof(kKeys) / sizeof(kKeys[0]);
    return kKeys;
}

const CalculatorKeyLayout *DatamathView::hit_test(int x, int y)
{
    for (const CalculatorKeyLayout &key : kKeys) {
        if (x >= key.x && x < key.x + key.width &&
            y >= key.y && y < key.y + key.height) {
            return &key;
        }
    }
    return nullptr;
}

void DatamathView::display_bounds(int &x, int &y, int &width, int &height)
{
    // Union of all eight digit slots, decimal points, minus, and Error text.
    // The surrounding window and border never change during calculation.
    x = 104;
    y = 58;
    width = 326;
    height = 66;
}

void DatamathView::switch_bounds(int &x, int &y, int &width, int &height)
{
    const CalculatorKeyLayout &key = kKeys[3];
    x = key.x;
    y = key.y;
    width = key.width;
    height = key.height;
}

void DatamathView::draw(const char *display_text, CalculatorMode mode)
{
    canvas_.clear(GrayLevel::Black);
    draw_panel();
    draw_display(display_text);
    draw_branding();
    for (const CalculatorKeyLayout &key : kKeys) {
        if (key.id == CalculatorKeyId::Switch) {
            draw_switch(key, mode);
        } else {
            draw_key(key);
        }
    }
}

void DatamathView::draw_display_value(const char *display_text)
{
    draw_display(display_text);
}

void DatamathView::draw_switch_state(CalculatorMode mode)
{
    const CalculatorKeyLayout &key = kKeys[3];
    canvas_.fill_rect(key.x, key.y, key.width, key.height, GrayLevel::Black);
    draw_switch(key, mode);
}

void DatamathView::draw_panel()
{
    fill_panel(canvas_);
}

void DatamathView::draw_display(const char *display_text)
{
    fill_rounded_rect(canvas_, kDisplayX, kDisplayY,
                      kDisplayWidth, kDisplayHeight,
                      kDisplayRadius, GrayLevel::White);
    fill_rounded_rect(canvas_, kDisplayX + 2, kDisplayY + 2,
                      kDisplayWidth - 4, kDisplayHeight - 4,
                      kDisplayRadius - 2, GrayLevel::Black);

    constexpr int reserved_width =
        kDisplaySlots * kDigitWidth + (kDisplaySlots - 1) * kDigitGap;
    constexpr int slots_x = kDisplayX + kDisplayWidth - kDisplayRightPadding
                            - kDecimalSize - 5 - reserved_width;
    constexpr int digit_y = kDisplayY + (kDisplayHeight - kDigitHeight) / 2;
    if (display_text == nullptr) display_text = "0.";
    if (std::strcmp(display_text, "Error") == 0) {
        draw_centered_text(kDisplayX + kDisplayWidth / 2,
                           kDisplayY + kDisplayHeight / 2,
                           "Error", 4, true);
        return;
    }

    int symbol_count = 0;
    for (const char *character = display_text; *character != '\0'; ++character) {
        if (*character != '.') ++symbol_count;
    }
    if (std::strcmp(display_text, "OVF") == 0 ||
        symbol_count > kDisplaySlots) {
        draw_centered_text(kDisplayX + kDisplayWidth / 2,
                           kDisplayY + kDisplayHeight / 2,
                           "OVF", 5, true);
        return;
    }

    int slot = kDisplaySlots - symbol_count;
    int previous_digit_x = -1;
    for (const char *character = display_text; *character != '\0'; ++character) {
        if (*character == '.') {
            if (previous_digit_x >= 0) {
                canvas_.fill_rect(previous_digit_x + kDigitWidth + 1,
                                  digit_y + kDigitHeight - kDecimalSize,
                                  kDecimalSize, kDecimalSize, GrayLevel::White);
            }
            continue;
        }
        const int digit_x = slots_x + slot * (kDigitWidth + kDigitGap);
        draw_seven_segment_digit(digit_x, digit_y, *character);
        previous_digit_x = (*character >= '0' && *character <= '9') ? digit_x : -1;
        ++slot;
    }
}

void DatamathView::draw_branding()
{
    draw_ti_logo(canvas_, 47, 179);
    draw_wordmark(canvas_, 281, kBrandCenterY - 10, "Datamath");
}

void DatamathView::draw_key(const CalculatorKeyLayout &key)
{
    const bool emphasized = key.id == CalculatorKeyId::Equals;
    // One clean key face with a narrow contrasting rim; no separate pedestal.
    if (emphasized) {
        fill_rounded_rect(canvas_, key.x, key.y, key.width, key.height,
                          kKeyRadius, GrayLevel::White);
        fill_rounded_rect(canvas_, key.x + kKeyOutline,
                          key.y + kKeyOutline,
                          key.width - 2 * kKeyOutline,
                          key.height - 2 * kKeyOutline,
                          kKeyRadius - kKeyOutline, GrayLevel::Black);
    } else {
        fill_rounded_rect(canvas_, key.x, key.y, key.width, key.height,
                          kKeyRadius, GrayLevel::Black);
        fill_rounded_rect(canvas_, key.x + kKeyOutline,
                          key.y + kKeyOutline,
                          key.width - 2 * kKeyOutline,
                          key.height - 2 * kKeyOutline,
                          kKeyRadius - kKeyOutline, GrayLevel::White);
    }

    const int center_x = key.x + key.width / 2;
    const int center_y = key.y + key.height / 2;
    if (key.label[0] != '\0') {
        draw_centered_text(center_x, center_y, key.label,
                           std::strlen(key.label) > 1 ? 3 : 4, emphasized);
    } else {
        draw_key_symbol(key, center_x, center_y, emphasized);
    }
}

void DatamathView::draw_switch(const CalculatorKeyLayout &key,
                               CalculatorMode mode)
{
    // Vertical recessed selector: CHAIN above, CONST below, no keycap.
    draw_centered_text(key.x + key.width / 2, key.y + 7,
                       "CHAIN", 1, true);
    draw_centered_text(key.x + key.width / 2, key.y + key.height - 5,
                       "CONST", 1, true);
    const int slot_x = key.x + key.width / 2 - 7;
    const int slot_y = key.y + 17;
    canvas_.draw_rect(slot_x, slot_y, 14, 40, GrayLevel::White);
    canvas_.draw_rect(slot_x + 1, slot_y + 1, 12, 38, GrayLevel::White);
    const int slider_y = mode == CalculatorMode::Chain ? slot_y + 5 : slot_y + 23;
    canvas_.fill_rect(slot_x - 5, slider_y, 24, 12, GrayLevel::White);
    canvas_.draw_rect(slot_x - 3, slider_y + 2, 20, 8, GrayLevel::Black);
}

void DatamathView::draw_key_symbol(const CalculatorKeyLayout &key, int center_x,
                                   int center_y, bool white)
{
    const GrayLevel color = white ? GrayLevel::White : GrayLevel::Black;
    constexpr int length = 28;
    constexpr int thickness = 5;
    switch (key.id) {
    case CalculatorKeyId::Divide:
        canvas_.fill_rect(center_x - length / 2, center_y - thickness / 2,
                          length, thickness, color);
        canvas_.fill_rect(center_x - 3, center_y - 18, 6, 6, color);
        canvas_.fill_rect(center_x - 3, center_y + 12, 6, 6, color);
        break;
    case CalculatorKeyId::Multiply:
        for (int offset = -2; offset <= 2; ++offset) {
            canvas_.draw_line(center_x - 13 + offset, center_y - 13,
                              center_x + 13 + offset, center_y + 13, color);
            canvas_.draw_line(center_x + 13 + offset, center_y - 13,
                              center_x - 13 + offset, center_y + 13, color);
        }
        break;
    case CalculatorKeyId::Subtract:
        canvas_.fill_rect(center_x - length / 2, center_y - thickness / 2,
                          length, thickness, color);
        break;
    case CalculatorKeyId::Add:
        canvas_.fill_rect(center_x - length / 2, center_y - thickness / 2,
                          length, thickness, color);
        canvas_.fill_rect(center_x - thickness / 2, center_y - length / 2,
                          thickness, length, color);
        break;
    case CalculatorKeyId::Equals:
        canvas_.fill_rect(center_x - length / 2, center_y - 8,
                          length, thickness, color);
        canvas_.fill_rect(center_x - length / 2, center_y + 4,
                          length, thickness, color);
        break;
    default:
        break;
    }
}

void DatamathView::draw_seven_segment_digit(int x, int y, char digit)
{
    constexpr uint8_t digit_segments[10] = {
        0x3F, 0x06, 0x5B, 0x4F, 0x66,
        0x6D, 0x7D, 0x07, 0x7F, 0x6F,
    };
    const uint8_t segments = digit == '-' ? 0x40 :
        (digit >= '0' && digit <= '9' ? digit_segments[digit - '0'] : 0);
    if (segments & (1U << 0)) horizontal_segment(canvas_, x, y, true);
    if (segments & (1U << 1)) vertical_segment(canvas_, x + kDigitWidth - kSegmentThickness, y, true);
    if (segments & (1U << 2)) vertical_segment(canvas_, x + kDigitWidth - kSegmentThickness, y + kDigitHeight / 2, true);
    if (segments & (1U << 3)) horizontal_segment(canvas_, x, y + kDigitHeight - kSegmentThickness, true);
    if (segments & (1U << 4)) vertical_segment(canvas_, x, y + kDigitHeight / 2, true);
    if (segments & (1U << 5)) vertical_segment(canvas_, x, y, true);
    if (segments & (1U << 6)) horizontal_segment(canvas_, x, y + kDigitHeight / 2 - kSegmentThickness / 2, true);
}

void DatamathView::draw_centered_text(int center_x, int center_y, const char *text,
                                      uint8_t scale, bool white)
{
    const int width = static_cast<int>(std::strlen(text)) * 6 * scale - scale;
    canvas_.draw_text(center_x - width / 2, center_y - (7 * scale) / 2,
                      text, scale, white ? GrayLevel::White : GrayLevel::Black);
}
