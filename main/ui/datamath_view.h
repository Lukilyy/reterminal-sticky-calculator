#pragma once
#include <cstddef>
#include <cstdint>

#include "app/calculator_engine.h"

class Canvas;

enum class CalculatorKeyId : uint8_t {
    ClearEntry, Divide, Multiply, Switch,
    Seven, Eight, Nine, Clear,
    Four, Five, Six, Subtract,
    One, Two, Three, Add,
    Zero, Decimal, Equals,
};

struct CalculatorKeyLayout {
    CalculatorKeyId id;
    const char *label;
    int x;
    int y;
    int width;
    int height;
};

class DatamathView {
public:
    explicit DatamathView(Canvas &canvas);
    void draw(const char *display_text,
              CalculatorMode mode = CalculatorMode::Chain);
    void draw_display_value(const char *display_text);
    void draw_switch_state(CalculatorMode mode);
    static const CalculatorKeyLayout *keys(size_t &count);
    static const CalculatorKeyLayout *hit_test(int x, int y);
    static void display_bounds(int &x, int &y, int &width, int &height);
    static void switch_bounds(int &x, int &y, int &width, int &height);

private:
    void draw_panel();
    void draw_display(const char *display_text);
    void draw_branding();
    void draw_key(const CalculatorKeyLayout &key);
    void draw_switch(const CalculatorKeyLayout &key, CalculatorMode mode);
    void draw_key_symbol(const CalculatorKeyLayout &key, int center_x, int center_y,
                         bool white);
    void draw_seven_segment_digit(int x, int y, char digit);
    void draw_centered_text(int center_x, int center_y, const char *text,
                            uint8_t scale, bool white = false);
    Canvas &canvas_;
};
