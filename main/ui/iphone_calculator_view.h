#pragma once

#include <cstddef>
#include <cstdint>

class Canvas;

enum class IphoneKeyId : uint8_t {
    Delete, Clear, Percent, Divide,
    Seven, Eight, Nine, Multiply,
    Four, Five, Six, Subtract,
    One, Two, Three, Add,
    ToggleSign, Zero, Decimal, Equals,
};

struct IphoneKeyLayout {
    IphoneKeyId id;
    const char *label;
    int x;
    int y;
    int width;
    int height;
};

struct IphoneHistoryItem {
    const char *expression;
    const char *result;
};

class IphoneCalculatorView {
public:
    explicit IphoneCalculatorView(Canvas &canvas);

    void draw(const char *display_text, const char *expression = nullptr);
    void draw_display_value(const char *display_text,
                            const char *expression = nullptr);
    void draw_history(const IphoneHistoryItem *items, size_t count);

    static const IphoneKeyLayout *keys(size_t &count);
    static const IphoneKeyLayout *hit_test(int x, int y);
    static bool history_button_hit_test(int x, int y);
    static bool history_back_hit_test(int x, int y);
    static int history_item_hit_test(int x, int y, size_t count);
    static void display_bounds(int &x, int &y, int &width, int &height);

private:
    void draw_display(const char *display_text, const char *expression);
    void draw_key(const IphoneKeyLayout &key);
    Canvas &canvas_;
};
