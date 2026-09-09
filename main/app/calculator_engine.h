#pragma once

#include <atomic>
#include <cstddef>
#include <string>

enum class CalculatorOperation {
    None,
    Add,
    Subtract,
    Multiply,
    Divide,
};

enum class CalculatorMode {
    Chain,
    Constant,
};

class CalculatorEngine {
public:
    CalculatorEngine();

    void input_digit(char digit, size_t max_digits = 8);
    void input_decimal();
    void input_operation(CalculatorOperation operation);
    void input_equals();
    void input_equals_standard();
    void input_toggle_sign();
    void input_percent();
    void input_contextual_clear();
    void delete_last_digit();
    bool restore_result(const char *value);
    void clear_entry();
    void clear_all();

    const char *display() const { return display_.c_str(); }
    bool has_error() const { return error_; }
    bool is_entering() const { return entering_; }
    CalculatorMode mode() const { return mode_.load(); }
    CalculatorMode toggle_mode();

private:
    static constexpr size_t kMaxSignificantDigits = 15;

    void begin_new_entry();
    bool apply_pending(double right_operand);
    void input_equals_impl(bool use_ti_constant_mode);
    bool set_result(double value);
    void set_error();
    void set_overflow();
    static size_t digit_count(const std::string &text);
    static bool format_result(double value, std::string &result);

    std::string entry_;
    std::string display_;
    double accumulator_ = 0.0;
    CalculatorOperation pending_ = CalculatorOperation::None;
    std::atomic<CalculatorMode> mode_{CalculatorMode::Chain};
    CalculatorOperation constant_operation_ = CalculatorOperation::None;
    double constant_value_ = 0.0;
    bool constant_valid_ = false;
    bool entering_ = false;
    bool result_shown_ = false;
    bool error_ = false;
};
