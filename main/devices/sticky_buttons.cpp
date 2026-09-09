#include "sticky_buttons.h"

#include "button_gpio.h"
#include "pin_config.h"

namespace {
constexpr uint16_t kLongPressTimeMs = 2000;
constexpr uint16_t kShortPressTimeMs = 180;

esp_err_t create_button(int gpio, button_cb_t callback, void *context,
                        button_handle_t *handle)
{
    button_config_t config = {};
    config.long_press_time = kLongPressTimeMs;
    config.short_press_time = kShortPressTimeMs;

    button_gpio_config_t gpio_config = {};
    gpio_config.gpio_num = gpio;
    gpio_config.active_level = 0;
    gpio_config.enable_power_save = false;
    gpio_config.disable_pull = false;

    esp_err_t result =
        iot_button_new_gpio_device(&config, &gpio_config, handle);
    if (result != ESP_OK) return result;
    return iot_button_register_cb(
        *handle, BUTTON_SINGLE_CLICK, nullptr, callback, context);
}
}  // namespace

esp_err_t StickyPageButtons::init()
{
    esp_err_t result = create_button(PIN_BTN_UP, on_up, this, &up_);
    if (result != ESP_OK) return result;
    return create_button(PIN_BTN_DOWN, on_down, this, &down_);
}

StickyPageButtonEvent StickyPageButtons::poll()
{
    return pending_.exchange(StickyPageButtonEvent::None);
}

void StickyPageButtons::on_up(void *, void *user_data)
{
    static_cast<StickyPageButtons *>(user_data)->pending_.store(
        StickyPageButtonEvent::Up);
}

void StickyPageButtons::on_down(void *, void *user_data)
{
    static_cast<StickyPageButtons *>(user_data)->pending_.store(
        StickyPageButtonEvent::Down);
}
