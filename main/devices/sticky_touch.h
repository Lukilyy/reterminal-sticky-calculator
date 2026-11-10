#pragma once

#include <cstdint>
#include <atomic>

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gt911.h"

struct StickyTouchPoint {
    uint16_t x = 0;
    uint16_t y = 0;
};

class StickyTouch {
public:
    bool init();
    void stop();

    // Returns one tap only after a complete press/release cycle.
    bool poll_tap(StickyTouchPoint &point);

private:
    static void touch_task_entry(void *context);
    void touch_task();
    bool read_tap(StickyTouchPoint &point);

    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    GT911 controller_;
    QueueHandle_t event_queue_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> task_stopped_{true};
    bool touching_ = false;
    StickyTouchPoint start_ = {};
    StickyTouchPoint last_ = {};
};
