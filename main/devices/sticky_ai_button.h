#pragma once

#include <atomic>

#include "iot_button.h"

class StickyAiButton {
public:
    bool init(bool suppress_until_release = false);
    bool poll_short_press();
    bool poll_long_press();

private:
    static void on_short_press(void *button_handle, void *user_data);
    static void on_long_press(void *button_handle, void *user_data);

    button_handle_t handle_ = nullptr;
    std::atomic<bool> short_press_pending_{false};
    std::atomic<bool> long_press_pending_{false};
    std::atomic<bool> suppress_until_release_{false};
};
