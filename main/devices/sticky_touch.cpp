#include "sticky_touch.h"

#include <algorithm>
#include <cstdlib>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_config.h"

namespace {
constexpr char kTag[] = "sticky_touch";
constexpr uint16_t kNativeWidth = 800;
constexpr uint16_t kNativeHeight = 480;
constexpr uint16_t kLogicalWidth = 480;
constexpr uint16_t kLogicalHeight = 800;
constexpr int kMaximumTapMovement = 40;
constexpr TickType_t kPollInterval = pdMS_TO_TICKS(20);
constexpr uint32_t kTaskStackSize = 4096;
constexpr UBaseType_t kTaskPriority = 5;

uint16_t scale_coordinate(uint16_t value, uint16_t source_max,
                          uint16_t target_max)
{
    const uint16_t clamped = std::min(value, source_max);
    return static_cast<uint16_t>(
        (static_cast<uint32_t>(clamped) * target_max + source_max / 2U) /
        source_max);
}
}  // namespace

bool StickyTouch::init()
{
    gpio_config_t power_config = {};
    power_config.pin_bit_mask = 1ULL << PIN_TOUCH_EN;
    power_config.mode = GPIO_MODE_OUTPUT;
    power_config.intr_type = GPIO_INTR_DISABLE;
    esp_err_t result = gpio_config(&power_config);
    if (result != ESP_OK) return false;
    result = gpio_set_level(static_cast<gpio_num_t>(PIN_TOUCH_EN), 1);
    if (result != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(250));

    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = static_cast<gpio_num_t>(PIN_TOUCH_SDA);
    bus_config.scl_io_num = static_cast<gpio_num_t>(PIN_TOUCH_SCL);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = 1;
    result = i2c_new_master_bus(&bus_config, &i2c_bus_);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Touch I2C init failed: %s", esp_err_to_name(result));
        return false;
    }

    if (!controller_.begin(PIN_TOUCH_INT, PIN_TOUCH_RST,
                           kNativeWidth, kNativeHeight, i2c_bus_)) {
        ESP_LOGE(kTag, "GT911 init failed");
        return false;
    }

    uint16_t sensor_width = 0;
    uint16_t sensor_height = 0;
    controller_.readResolution(sensor_width, sensor_height);
    ESP_LOGI(kTag, "GT911 ready: sensor=%ux%u logical=%ux%u address=0x%02X",
             sensor_width, sensor_height, kLogicalWidth, kLogicalHeight,
             controller_.address());
    event_queue_ = xQueueCreate(8, sizeof(StickyTouchPoint));
    if (event_queue_ == nullptr) return false;
    stop_requested_.store(false);
    task_stopped_.store(false);
    if (xTaskCreate(touch_task_entry, "calculator_touch", kTaskStackSize,
                    this, kTaskPriority, &task_handle_) != pdPASS) {
        vQueueDelete(event_queue_);
        event_queue_ = nullptr;
        task_stopped_.store(true);
        return false;
    }
    return true;
}

void StickyTouch::stop()
{
    if (task_handle_ != nullptr) {
        stop_requested_.store(true);
        // Let an in-flight GT911 transaction finish before the caller powers
        // down touch. The worker acknowledges only after its final access.
        while (!task_stopped_.load()) {
            vTaskDelay(1);
        }
        task_handle_ = nullptr;
    }
    touching_ = false;
    if (event_queue_ != nullptr) {
        (void)xQueueReset(event_queue_);
    }
}

bool StickyTouch::poll_tap(StickyTouchPoint &point)
{
    return event_queue_ != nullptr &&
           xQueueReceive(event_queue_, &point, 0) == pdTRUE;
}

void StickyTouch::touch_task_entry(void *context)
{
    static_cast<StickyTouch *>(context)->touch_task();
}

void StickyTouch::touch_task()
{
    TickType_t next_poll = xTaskGetTickCount();
    while (!stop_requested_.load()) {
        StickyTouchPoint point;
        if (read_tap(point) && xQueueSend(event_queue_, &point, 0) != pdTRUE) {
            ESP_LOGW(kTag, "Touch event queue full");
        }
        vTaskDelayUntil(&next_poll, kPollInterval);
    }
    task_stopped_.store(true);
    vTaskDelete(nullptr);
}

bool StickyTouch::read_tap(StickyTouchPoint &point)
{
    GTPoint raw = {};
    const int8_t count = controller_.read_points(&raw, 1);
    if (count > 0) {
        // Exact portrait mapping used by the hardware-verified Sticky 2048 demo.
        last_.x = scale_coordinate(raw.x, kNativeWidth, kLogicalWidth - 1);
        const uint16_t mapped_y = std::min<uint16_t>(raw.y, kNativeHeight);
        last_.y = scale_coordinate(kNativeHeight - mapped_y,
                                   kNativeHeight, kLogicalHeight - 1);
        if (!touching_) start_ = last_;
        touching_ = true;
        return false;
    }
    if (count < 0) {
        ESP_LOGW(kTag, "GT911 read failed");
        touching_ = false;
        return false;
    }
    if (!touching_) return false;

    touching_ = false;
    const int movement_x = std::abs(static_cast<int>(last_.x) - start_.x);
    const int movement_y = std::abs(static_cast<int>(last_.y) - start_.y);
    if (movement_x >= kMaximumTapMovement || movement_y >= kMaximumTapMovement) {
        return false;
    }
    point = last_;
    return true;
}
