#include "iphone_calculator_view.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "assets/iphone_inter_font.h"
#include "canvas.h"

namespace {
constexpr int kDisplayX = 16;
constexpr int kDisplayY = 24;
constexpr int kDisplayWidth = 448;
constexpr int kDisplayHeight = 220;
constexpr int kDisplayRight = 452;
constexpr int kDisplayBottom = 224;
constexpr int kExpressionY = 104;
constexpr int kHistoryButtonX = 18;
constexpr int kHistoryButtonY = 28;
constexpr int kHistoryButtonSize = 52;
constexpr int kHistoryListY = 126;
constexpr int kHistoryRowHeight = 128;

constexpr int kKeyX = 28;
constexpr int kKeyY = 274;
constexpr int kKeySize = 88;
constexpr int kGapX = 24;
constexpr int kGapY = 18;

constexpr int key_x(int column)
{
    return kKeyX + column * (kKeySize + kGapX);
}

constexpr int key_y(int row)
{
    return kKeyY + row * (kKeySize + kGapY);
}

constexpr IphoneKeyLayout kKeys[] = {
    {IphoneKeyId::Delete, "", key_x(0), key_y(0), kKeySize, kKeySize},
    {IphoneKeyId::Clear, "AC", key_x(1), key_y(0), kKeySize, kKeySize},
    {IphoneKeyId::Percent, "%", key_x(2), key_y(0), kKeySize, kKeySize},
    {IphoneKeyId::Divide, "/", key_x(3), key_y(0), kKeySize, kKeySize},
    {IphoneKeyId::Seven, "7", key_x(0), key_y(1), kKeySize, kKeySize},
    {IphoneKeyId::Eight, "8", key_x(1), key_y(1), kKeySize, kKeySize},
    {IphoneKeyId::Nine, "9", key_x(2), key_y(1), kKeySize, kKeySize},
    {IphoneKeyId::Multiply, "x", key_x(3), key_y(1), kKeySize, kKeySize},
    {IphoneKeyId::Four, "4", key_x(0), key_y(2), kKeySize, kKeySize},
    {IphoneKeyId::Five, "5", key_x(1), key_y(2), kKeySize, kKeySize},
    {IphoneKeyId::Six, "6", key_x(2), key_y(2), kKeySize, kKeySize},
    {IphoneKeyId::Subtract, "-", key_x(3), key_y(2), kKeySize, kKeySize},
    {IphoneKeyId::One, "1", key_x(0), key_y(3), kKeySize, kKeySize},
    {IphoneKeyId::Two, "2", key_x(1), key_y(3), kKeySize, kKeySize},
    {IphoneKeyId::Three, "3", key_x(2), key_y(3), kKeySize, kKeySize},
    {IphoneKeyId::Add, "+", key_x(3), key_y(3), kKeySize, kKeySize},
    {IphoneKeyId::ToggleSign, "+_-", key_x(0), key_y(4), kKeySize, kKeySize},
    {IphoneKeyId::Zero, "0", key_x(1), key_y(4), kKeySize, kKeySize},
    {IphoneKeyId::Decimal, ".", key_x(2), key_y(4), kKeySize, kKeySize},
    {IphoneKeyId::Equals, "=", key_x(3), key_y(4), kKeySize, kKeySize},
};

bool is_function_key(IphoneKeyId id)
{
    return id == IphoneKeyId::Delete || id == IphoneKeyId::Clear ||
           id == IphoneKeyId::ToggleSign ||
           id == IphoneKeyId::Percent;
}

std::string grouped_number(const char *text)
{
    if (text == nullptr || *text == '\0') return {};
    std::string value(text);
    if (value.find('e') != std::string::npos ||
        value.find('E') != std::string::npos) return value;

    size_t start = value[0] == '-' ? 1U : 0U;
    const size_t decimal = value.find('.', start);
    const size_t integer_end = decimal == std::string::npos
                                   ? value.size() : decimal;
    if (integer_end <= start) return value;
    for (size_t i = start; i < value.size(); ++i) {
        if (i == decimal) continue;
        if (value[i] < '0' || value[i] > '9') return value;
    }
    for (size_t position = integer_end; position > start + 3U;) {
        position -= 3U;
        value.insert(position, 1, ',');
    }
    return value;
}

std::string grouped_expression(const char *expression)
{
    if (expression == nullptr) return {};
    std::string output;
    const std::string source(expression);
    size_t position = 0;
    while (position < source.size()) {
        if (source[position] == ' ') {
            output.push_back(' ');
            ++position;
            continue;
        }
        const size_t end = source.find(' ', position);
        const size_t count = end == std::string::npos
                                 ? source.size() - position : end - position;
        const std::string token = source.substr(position, count);
        output += grouped_number(token.c_str());
        position += count;
    }
    return output;
}

bool is_operator_key(IphoneKeyId id)
{
    return id == IphoneKeyId::Divide || id == IphoneKeyId::Multiply ||
           id == IphoneKeyId::Subtract || id == IphoneKeyId::Add ||
           id == IphoneKeyId::Equals;
}

void fill_circle(Canvas &canvas, int center_x, int center_y, int radius,
                 GrayLevel color)
{
    for (int y = -radius; y <= radius; ++y) {
        int x = radius;
        while (x > 0 && x * x + y * y > radius * radius) --x;
        canvas.fill_rect(center_x - x, center_y + y, x * 2 + 1, 1, color);
    }
}

void fill_capsule(Canvas &canvas, int x, int y, int width, int height,
                  GrayLevel color)
{
    const int radius = (height - 2) / 2;
    const int center_y = y + height / 2;
    const int left_center = x + height / 2;
    const int right_center = x + width - height / 2;
    canvas.fill_rect(left_center, y + 1,
                     right_center - left_center, height - 2, color);
    fill_circle(canvas, left_center, center_y, radius, color);
    fill_circle(canvas, right_center, center_y, radius, color);
}

void draw_thick_line(Canvas &canvas, int x0, int y0, int x1, int y1,
                     int thickness, GrayLevel color)
{
    const int first = -(thickness / 2);
    const int last = first + thickness - 1;
    for (int dx = first; dx <= last; ++dx) {
        for (int dy = first; dy <= last; ++dy) {
            canvas.draw_line(x0 + dx, y0 + dy, x1 + dx, y1 + dy, color);
        }
    }
}

void draw_history_icon(Canvas &canvas)
{
    const int center_x = kHistoryButtonX + kHistoryButtonSize / 2;
    const int center_y = kHistoryButtonY + kHistoryButtonSize / 2;
    fill_circle(canvas, center_x, center_y, 23, GrayLevel::White);
    fill_circle(canvas, center_x, center_y, 19, GrayLevel::Black);
    draw_thick_line(canvas, center_x, center_y,
                    center_x, center_y - 11, 3, GrayLevel::White);
    draw_thick_line(canvas, center_x, center_y,
                    center_x - 9, center_y + 5, 3, GrayLevel::White);
    canvas.fill_rect(center_x - 2, center_y - 2, 5, 5, GrayLevel::White);
}

bool point_in_key(const IphoneKeyLayout &key, int x, int y)
{
    if (x < key.x || x >= key.x + key.width ||
        y < key.y || y >= key.y + key.height) return false;
    const int radius = (key.height - 2) / 2;
    const int left_center = key.x + key.height / 2;
    const int right_center = key.x + key.width - key.height / 2;
    if (x >= left_center && x <= right_center &&
        y > key.y && y < key.y + key.height - 1) return true;
    const int center_x = x < key.x + key.width / 2
                             ? left_center : right_center;
    const int dx = x - center_x;
    const int dy = y - (key.y + key.height / 2);
    return dx * dx + dy * dy <= radius * radius;
}

int inter_text_width(const char *text, const IphoneFontStrike &strike)
{
    int width = 0;
    for (const char *character = text; character != nullptr && *character;
         ++character) {
        const IphoneFontGlyph *glyph = iphone_inter_glyph(strike, *character);
        if (glyph != nullptr) width += glyph->advance;
    }
    return width;
}

void draw_inter_text(Canvas &canvas, int x, int baseline, const char *text,
                     const IphoneFontStrike &strike, GrayLevel color)
{
    for (const char *character = text; character != nullptr && *character; ++character) {
        const IphoneFontGlyph *glyph = iphone_inter_glyph(strike, *character);
        if (glyph == nullptr) continue;
        const size_t stride = (glyph->width + 7U) / 8U;
        for (uint16_t row = 0; row < glyph->height; ++row) {
            for (uint16_t column = 0; column < glyph->width; ++column) {
                const uint8_t byte = strike.bitmap[
                    glyph->bitmap_offset + row * stride + column / 8U];
                if ((byte & (0x80U >> (column & 7U))) != 0U) {
                    canvas.draw_pixel(x + glyph->bearing_x + column,
                                      baseline + glyph->bearing_y + row,
                                      color);
                }
            }
        }
        x += glyph->advance;
    }
}

void draw_delete_icon(Canvas &canvas, int center_x, int center_y,
                      GrayLevel color)
{
    auto thick_line = [&](int x0, int y0, int x1, int y1) {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                canvas.draw_line(x0 + dx, y0 + dy,
                                 x1 + dx, y1 + dy, color);
            }
        }
    };
    thick_line(center_x - 22, center_y, center_x - 10, center_y - 13);
    thick_line(center_x - 10, center_y - 13, center_x + 19, center_y - 13);
    thick_line(center_x + 19, center_y - 13, center_x + 19, center_y + 13);
    thick_line(center_x + 19, center_y + 13, center_x - 10, center_y + 13);
    thick_line(center_x - 10, center_y + 13, center_x - 22, center_y);
    thick_line(center_x - 5, center_y - 6, center_x + 10, center_y + 6);
    thick_line(center_x + 10, center_y - 6, center_x - 5, center_y + 6);
}

const IphoneFontStrike &result_strike(const char *text)
{
    constexpr IphoneFontSize sizes[] = {
        IphoneFontSize::ResultLarge, IphoneFontSize::ResultMedium,
        IphoneFontSize::ResultSmall, IphoneFontSize::ResultCompact,
    };
    for (IphoneFontSize size : sizes) {
        const IphoneFontStrike &strike = iphone_inter_font(size);
        if (inter_text_width(text, strike) <= kDisplayRight - kDisplayX) {
            return strike;
        }
    }
    return iphone_inter_font(IphoneFontSize::ResultCompact);
}

}  // namespace

IphoneCalculatorView::IphoneCalculatorView(Canvas &canvas) : canvas_(canvas) {}

const IphoneKeyLayout *IphoneCalculatorView::keys(size_t &count)
{
    count = sizeof(kKeys) / sizeof(kKeys[0]);
    return kKeys;
}

const IphoneKeyLayout *IphoneCalculatorView::hit_test(int x, int y)
{
    for (const IphoneKeyLayout &key : kKeys) {
        if (point_in_key(key, x, y)) return &key;
    }
    return nullptr;
}

bool IphoneCalculatorView::history_button_hit_test(int x, int y)
{
    return x >= kHistoryButtonX && x < kHistoryButtonX + kHistoryButtonSize &&
           y >= kHistoryButtonY && y < kHistoryButtonY + kHistoryButtonSize;
}

bool IphoneCalculatorView::history_back_hit_test(int x, int y)
{
    return x >= 12 && x < 84 && y >= 18 && y < 88;
}

int IphoneCalculatorView::history_item_hit_test(int x, int y, size_t count)
{
    if (x < 16 || x >= 464 || y < kHistoryListY) return -1;
    const int index = (y - kHistoryListY) / kHistoryRowHeight;
    return index >= 0 && static_cast<size_t>(index) < count ? index : -1;
}

void IphoneCalculatorView::display_bounds(int &x, int &y,
                                          int &width, int &height)
{
    // Union of every dynamic text size/right-aligned position. The rest of
    // the black display area remains unchanged.
    x = 16;
    y = 96;
    width = 440;
    height = 132;
}

void IphoneCalculatorView::draw(const char *display_text,
                                const char *expression)
{
    canvas_.clear(GrayLevel::Black);
    draw_display(display_text, expression);
    for (const IphoneKeyLayout &key : kKeys) draw_key(key);
}

void IphoneCalculatorView::draw_display_value(const char *display_text,
                                              const char *expression)
{
    draw_display(display_text, expression);
}

void IphoneCalculatorView::draw_display(const char *display_text,
                                        const char *expression)
{
    canvas_.fill_rect(kDisplayX, kDisplayY, kDisplayWidth, kDisplayHeight,
                      GrayLevel::Black);
    if (display_text == nullptr) display_text = "0.";
    const std::string formatted_result = grouped_number(display_text);
    const IphoneFontStrike &result_font = result_strike(formatted_result.c_str());
    const int width = inter_text_width(formatted_result.c_str(), result_font);
    draw_inter_text(canvas_, kDisplayRight - width, kDisplayBottom - 1,
                    formatted_result.c_str(), result_font, GrayLevel::White);

    if (expression != nullptr && expression[0] != '\0') {
        const std::string formatted_expression = grouped_expression(expression);
        const IphoneFontStrike &expression_font =
            iphone_inter_font(IphoneFontSize::Expression);
        const char *visible = formatted_expression.c_str();
        while (*visible != '\0' &&
               inter_text_width(visible, expression_font) >
                   kDisplayRight - kDisplayX) {
            ++visible;
        }
        const int expression_width = inter_text_width(visible, expression_font);
        draw_inter_text(canvas_, kDisplayRight - expression_width,
                        kExpressionY + 24, visible, expression_font,
                        GrayLevel::White);
    }
    draw_history_icon(canvas_);
}

void IphoneCalculatorView::draw_history(const IphoneHistoryItem *items,
                                        size_t count)
{
    canvas_.clear(GrayLevel::Black);
    const IphoneFontStrike &title_font =
        iphone_inter_font(IphoneFontSize::Expression);
    const IphoneFontStrike &expression_font =
        iphone_inter_font(IphoneFontSize::ResultCompact);
    draw_thick_line(canvas_, 52, 38, 35, 53, 2, GrayLevel::White);
    draw_thick_line(canvas_, 35, 53, 52, 68, 2, GrayLevel::White);
    draw_thick_line(canvas_, 36, 53, 68, 53, 2, GrayLevel::White);
    draw_inter_text(canvas_, 90, 65, "History", title_font,
                    GrayLevel::White);

    for (size_t index = 0; index < count; ++index) {
        const int row_y = kHistoryListY +
                          static_cast<int>(index) * kHistoryRowHeight;
        const std::string expression = grouped_expression(items[index].expression);
        const std::string result = grouped_number(items[index].result);
        const char *visible_expression = expression.c_str();
        while (*visible_expression != '\0' &&
               inter_text_width(visible_expression, expression_font) > 424) {
            ++visible_expression;
        }
        const IphoneFontStrike *result_font =
            &iphone_inter_font(IphoneFontSize::KeyEmphasis);
        if (inter_text_width(result.c_str(), *result_font) > 424) {
            result_font = &iphone_inter_font(IphoneFontSize::ResultCompact);
        }
        const int expression_width =
            inter_text_width(visible_expression, expression_font);
        const int result_width = inter_text_width(result.c_str(), *result_font);
        draw_inter_text(canvas_, kDisplayRight - expression_width, row_y + 28,
                        visible_expression, expression_font, GrayLevel::White);
        draw_inter_text(canvas_, kDisplayRight - result_width, row_y + 82,
                        result.c_str(), *result_font, GrayLevel::White);
    }
}

void IphoneCalculatorView::draw_key(const IphoneKeyLayout &key)
{
    const bool function = is_function_key(key.id);
    const bool operation = is_operator_key(key.id);
    const bool special = function || operation;

    // Monochrome grouping: function/operator keys are solid white, while
    // number keys retain the iPhone circles as a white rim on black.
    fill_capsule(canvas_, key.x, key.y, key.width, key.height,
                 GrayLevel::White);
    if (!special) {
        // A one-pixel rim only hints at the dark iPhone key shape on a
        // monochrome panel without turning it into a prominent hollow button.
        constexpr int outline = 2;
        fill_capsule(canvas_, key.x + outline, key.y + outline,
                     key.width - 2 * outline, key.height - 2 * outline,
                     GrayLevel::Black);
    }
    const GrayLevel foreground = special ? GrayLevel::Black : GrayLevel::White;

    const int center_y = key.y + key.height / 2;
    if (key.id == IphoneKeyId::Delete) {
        draw_delete_icon(canvas_, key.x + key.width / 2, center_y, foreground);
        return;
    }
    const IphoneFontStrike &font = iphone_inter_font(
        special ? IphoneFontSize::KeyEmphasis : IphoneFontSize::KeyNumber);
    const int text_width = inter_text_width(key.label, font);
    // Center on the font's cap-height rather than each glyph's cropped box;
    // this keeps digits, decimal point, and operators on one optical baseline.
    const int baseline = center_y + static_cast<int>(font.pixel_size * 37U / 100U);
    draw_inter_text(canvas_, key.x + (key.width - text_width) / 2,
                    baseline, key.label, font, foreground);
}
