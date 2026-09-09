#include "calculator_engine.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

CalculatorEngine::CalculatorEngine()
{
    clear_all();
}

size_t CalculatorEngine::digit_count(const std::string &text)
{
    size_t count = 0;
    for (char character : text) {
        if (character >= '0' && character <= '9') ++count;
    }
    return count;
}

void CalculatorEngine::begin_new_entry()
{
    entry_ = "0";
    display_ = entry_;
    entering_ = true;
    result_shown_ = false;
}

void CalculatorEngine::input_digit(char digit, size_t max_digits)
{
    if (digit < '0' || digit > '9' || error_) return;
    if (!entering_ || result_shown_) {
        if (result_shown_ && pending_ == CalculatorOperation::None) {
            accumulator_ = 0.0;
        }
        begin_new_entry();
    }
    if (entry_ == "0") {
        entry_[0] = digit;
    } else if (entry_ == "-0") {
        entry_ = "-";
        entry_ += digit;
    } else if (digit_count(entry_) < max_digits) {
        entry_ += digit;
    }
    display_ = entry_;
}

void CalculatorEngine::input_decimal()
{
    if (error_) return;
    if (!entering_ || result_shown_) {
        if (result_shown_ && pending_ == CalculatorOperation::None) {
            accumulator_ = 0.0;
        }
        begin_new_entry();
    }
    if (entry_.find('.') == std::string::npos) entry_ += '.';
    display_ = entry_;
}

bool CalculatorEngine::apply_pending(double right_operand)
{
    double result = accumulator_;
    switch (pending_) {
    case CalculatorOperation::Add: result += right_operand; break;
    case CalculatorOperation::Subtract: result -= right_operand; break;
    case CalculatorOperation::Multiply: result *= right_operand; break;
    case CalculatorOperation::Divide:
        if (right_operand == 0.0) {
            set_error();
            return false;
        }
        result /= right_operand;
        break;
    case CalculatorOperation::None: result = right_operand; break;
    }
    accumulator_ = result;
    return set_result(result);
}

void CalculatorEngine::input_operation(CalculatorOperation operation)
{
    if (error_ || operation == CalculatorOperation::None) return;
    if (entering_) {
        const double operand = std::strtod(entry_.c_str(), nullptr);
        if (pending_ == CalculatorOperation::None) {
            accumulator_ = operand;
            if (!set_result(accumulator_)) return;
        } else if (!apply_pending(operand)) {
            return;
        }
    }
    pending_ = operation;
    entering_ = false;
    result_shown_ = false;
}

void CalculatorEngine::input_equals()
{
    input_equals_impl(true);
}

void CalculatorEngine::input_equals_standard()
{
    input_equals_impl(false);
}

void CalculatorEngine::input_equals_impl(bool use_ti_constant_mode)
{
    if (error_ || !entering_) return;
    const double operand = std::strtod(entry_.c_str(), nullptr);

    if (pending_ != CalculatorOperation::None) {
        const CalculatorOperation completed_operation = pending_;
        if (!apply_pending(operand)) return;
        if (use_ti_constant_mode && mode() == CalculatorMode::Constant &&
            (completed_operation == CalculatorOperation::Multiply ||
             completed_operation == CalculatorOperation::Divide)) {
            constant_operation_ = completed_operation;
            constant_value_ = operand;
            constant_valid_ = true;
        }
    } else if (use_ti_constant_mode && mode() == CalculatorMode::Constant &&
               constant_valid_) {
        accumulator_ = operand;
        pending_ = constant_operation_;
        if (!apply_pending(constant_value_)) return;
    } else {
        return;
    }

    pending_ = CalculatorOperation::None;
    entering_ = false;
    result_shown_ = true;
}

void CalculatorEngine::input_toggle_sign()
{
    if (error_) return;
    if (!entering_) {
        if (result_shown_) {
            const double value = -std::strtod(display_.c_str(), nullptr);
            if (!set_result(value)) return;
            accumulator_ = value;
            result_shown_ = true;
            return;
        }
        begin_new_entry();
    }

    if (!entry_.empty() && entry_[0] == '-') {
        entry_.erase(0, 1);
    } else {
        entry_.insert(0, 1, '-');
    }
    display_ = entry_;
}

void CalculatorEngine::input_percent()
{
    if (error_) return;
    if (!entering_ && !result_shown_) return;

    double value = std::strtod(display_.c_str(), nullptr);
    if (entering_ && (pending_ == CalculatorOperation::Add ||
                      pending_ == CalculatorOperation::Subtract)) {
        value = accumulator_ * value / 100.0;
    } else {
        value /= 100.0;
    }
    if (!set_result(value)) return;
    if (pending_ == CalculatorOperation::None) accumulator_ = value;
    entering_ = true;
    result_shown_ = false;
}

void CalculatorEngine::input_contextual_clear()
{
    if (error_ || !entering_ || entry_ == "0") {
        clear_all();
    } else {
        clear_entry();
    }
}

void CalculatorEngine::delete_last_digit()
{
    if (error_) return;

    // A completed result becomes a fresh editable entry on the first delete.
    // The edited text will become the next accumulator if an operator is
    // pressed, exactly like a newly typed number.
    if (result_shown_) {
        entry_ = display_;
        entering_ = true;
        result_shown_ = false;
        pending_ = CalculatorOperation::None;
    }
    if (!entering_) return;

    if (!entry_.empty()) entry_.pop_back();
    if (entry_.empty() || entry_ == "-" || digit_count(entry_) == 0) {
        entry_ = "0";
    }
    display_ = entry_;
}

bool CalculatorEngine::restore_result(const char *value)
{
    if (value == nullptr || *value == '\0') return false;
    char *end = nullptr;
    const double restored = std::strtod(value, &end);
    if (end == value || *end != '\0' || !std::isfinite(restored)) return false;

    entry_ = value;
    display_ = value;
    accumulator_ = restored;
    pending_ = CalculatorOperation::None;
    constant_operation_ = CalculatorOperation::None;
    constant_valid_ = false;
    entering_ = false;
    result_shown_ = true;
    error_ = false;
    return true;
}

void CalculatorEngine::clear_entry()
{
    if (error_) {
        clear_all();
        return;
    }
    entry_ = "0";
    display_ = "0.";
    entering_ = true;
    result_shown_ = false;
}

void CalculatorEngine::clear_all()
{
    entry_ = "0";
    display_ = "0.";
    accumulator_ = 0.0;
    pending_ = CalculatorOperation::None;
    constant_operation_ = CalculatorOperation::None;
    constant_value_ = 0.0;
    constant_valid_ = false;
    entering_ = false;
    result_shown_ = false;
    error_ = false;
}

CalculatorMode CalculatorEngine::toggle_mode()
{
    const CalculatorMode next = mode_.load() == CalculatorMode::Chain
                                    ? CalculatorMode::Constant
                                    : CalculatorMode::Chain;
    mode_.store(next);
    return next;
}

bool CalculatorEngine::set_result(double value)
{
    std::string formatted;
    if (!format_result(value, formatted)) {
        set_overflow();
        return false;
    }
    entry_ = formatted;
    display_ = formatted;
    return true;
}

void CalculatorEngine::set_overflow()
{
    entry_.clear();
    display_ = "OVF";
    pending_ = CalculatorOperation::None;
    entering_ = false;
    result_shown_ = false;
    error_ = true;
}

void CalculatorEngine::set_error()
{
    entry_.clear();
    display_ = "Error";
    pending_ = CalculatorOperation::None;
    entering_ = false;
    result_shown_ = false;
    error_ = true;
}

bool CalculatorEngine::format_result(double value, std::string &result)
{
    if (!std::isfinite(value)) return false;
    if (value == 0.0) value = 0.0;

    char buffer[48] = {};
    // %g removes insignificant trailing zeroes and changes to scientific
    // notation when fixed-point text is no longer a sensible presentation.
    const int written = std::snprintf(buffer, sizeof(buffer), "%.*g",
                                      static_cast<int>(kMaxSignificantDigits),
                                      value);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(buffer)) {
        return false;
    }
    result = buffer;
    return digit_count(result) != 0;
}
