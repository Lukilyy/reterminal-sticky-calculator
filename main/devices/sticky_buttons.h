#pragma once

#include <atomic>

#include "esp_err.h"
#include "iot_button.h"

enum class StickyPageButtonEvent {
    None,
    Up,
    Down,
};

class StickyPageButtons {
public:
    esp_err_t init();
    StickyPageButtonEvent poll();

private:
    static void on_up(void *button_handle, void *user_data);
    static void on_down(void *button_handle, void *user_data);

    button_handle_t up_ = nullptr;
    button_handle_t down_ = nullptr;
    std::atomic<StickyPageButtonEvent> pending_{StickyPageButtonEvent::None};
};
