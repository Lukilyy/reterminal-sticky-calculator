#include "sticky_ai_button.h"

#include "button_gpio.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "pin_config.h"

namespace {
constexpr char kTag[] = "sticky_ai_button";
constexpr uint32_t kLongPressMs = 1000;
constexpr uint32_t kShortPressMs = 180;
}

bool StickyAiButton::init(bool suppress_until_release)
{
    suppress_until_release_.store(suppress_until_release);
    button_config_t button_config = {};
    button_config.long_press_time = kLongPressMs;
    button_config.short_press_time = kShortPressMs;

    button_gpio_config_t gpio_config = {};
    gpio_config.gpio_num = PIN_BTN_OK;
    gpio_config.active_level = 0;
    gpio_config.enable_power_save = false;
    gpio_config.disable_pull = false;

    esp_err_t result =
        iot_button_new_gpio_device(&button_config, &gpio_config, &handle_);
    if (result != ESP_OK || handle_ == nullptr) {
        ESP_LOGE(kTag, "AI button init failed: %s", esp_err_to_name(result));
        return false;
    }

    result = iot_button_register_cb(handle_, BUTTON_SINGLE_CLICK, nullptr,
                                    on_short_press, this);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "AI button callback failed: %s",
                 esp_err_to_name(result));
        return false;
    }
    result = iot_button_register_cb(handle_, BUTTON_LONG_PRESS_START, nullptr,
                                    on_long_press, this);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "AI long-press callback failed: %s",
                 esp_err_to_name(result));
        return false;
    }

    ESP_LOGI(kTag, "AI button ready on GPIO%d", PIN_BTN_OK);
    return true;
}

bool StickyAiButton::poll_short_press()
{
    if (suppress_until_release_.load() &&
        gpio_get_level(static_cast<gpio_num_t>(PIN_BTN_OK)) != 0) {
        suppress_until_release_.store(false);
        short_press_pending_.store(false);
        long_press_pending_.store(false);
    }
    return short_press_pending_.exchange(false);
}

bool StickyAiButton::poll_long_press()
{
    if (suppress_until_release_.load()) {
        if (gpio_get_level(static_cast<gpio_num_t>(PIN_BTN_OK)) != 0) {
            suppress_until_release_.store(false);
            short_press_pending_.store(false);
            long_press_pending_.store(false);
        }
        return false;
    }
    return long_press_pending_.exchange(false);
}

void StickyAiButton::on_short_press(void *, void *user_data)
{
    auto *button = static_cast<StickyAiButton *>(user_data);
    if (!button->suppress_until_release_.load()) {
        button->short_press_pending_.store(true);
    }
}

void StickyAiButton::on_long_press(void *, void *user_data)
{
    auto *button = static_cast<StickyAiButton *>(user_data);
    if (!button->suppress_until_release_.load()) {
        button->long_press_pending_.store(true);
    }
}
