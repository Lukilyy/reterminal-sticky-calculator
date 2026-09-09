#include "sticky_power.h"

#include <cstdio>

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_config.h"

namespace {

constexpr char kTag[] = "sticky_power";
constexpr TickType_t kReleasePollInterval = pdMS_TO_TICKS(20);
constexpr uint32_t kSleepWakePinStableHighMs = 150;

esp_err_t hold_output(gpio_num_t pin, int level)
{
    gpio_hold_dis(pin);
    ESP_RETURN_ON_ERROR(gpio_set_direction(pin, GPIO_MODE_OUTPUT),
                        kTag, "configure held output");
    ESP_RETURN_ON_ERROR(gpio_set_level(pin, level),
                        kTag, "set held output");
    return gpio_hold_en(pin);
}

void configure_ai_input()
{
    const gpio_num_t wake_pin = static_cast<gpio_num_t>(PIN_POWER_BTN);
    gpio_hold_dis(wake_pin);
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << PIN_POWER_BTN;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&config));
}

void configure_ai_rtc_wakeup_input()
{
    const gpio_num_t wake_pin = static_cast<gpio_num_t>(PIN_POWER_BTN);
    ESP_ERROR_CHECK(rtc_gpio_hold_dis(wake_pin));
    ESP_ERROR_CHECK(rtc_gpio_init(wake_pin));
    ESP_ERROR_CHECK(rtc_gpio_set_direction(wake_pin, RTC_GPIO_MODE_INPUT_ONLY));
    ESP_ERROR_CHECK(rtc_gpio_set_direction_in_sleep(
        wake_pin, RTC_GPIO_MODE_INPUT_ONLY));
    ESP_ERROR_CHECK(rtc_gpio_pullup_en(wake_pin));
    ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(wake_pin));
    ESP_ERROR_CHECK(rtc_gpio_hold_en(wake_pin));
}

void wait_for_ai_stable_high(uint32_t stable_time_ms, bool rtc_gpio)
{
    const TickType_t stable_ticks = pdMS_TO_TICKS(stable_time_ms);
    TickType_t high_since = 0;
    bool timing_high = false;
    while (true) {
        const int level = rtc_gpio
                              ? static_cast<int>(rtc_gpio_get_level(
                                    static_cast<gpio_num_t>(PIN_POWER_BTN)))
                              : gpio_get_level(
                                    static_cast<gpio_num_t>(PIN_POWER_BTN));
        if (level == 0) {
            timing_high = false;
            high_since = 0;
        } else if (!timing_high) {
            timing_high = true;
            high_since = xTaskGetTickCount();
        } else if (xTaskGetTickCount() - high_since >= stable_ticks) {
            return;
        }
        vTaskDelay(kReleasePollInterval);
    }
}

}  // namespace

bool sticky_power_woke_by_ai_button()
{
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1 &&
           (esp_sleep_get_ext1_wakeup_status() &
            (1ULL << PIN_POWER_BTN)) != 0;
}

bool sticky_power_confirm_ai_button_hold(uint32_t hold_duration_ms)
{
    configure_ai_input();
    if (gpio_get_level(static_cast<gpio_num_t>(PIN_POWER_BTN)) != 0) {
        return false;
    }

    const TickType_t hold_ticks = pdMS_TO_TICKS(hold_duration_ms);
    const TickType_t started_at = xTaskGetTickCount();
    while (xTaskGetTickCount() - started_at < hold_ticks) {
        if (gpio_get_level(static_cast<gpio_num_t>(PIN_POWER_BTN)) != 0) {
            return false;
        }
        vTaskDelay(kReleasePollInterval);
    }
    return gpio_get_level(static_cast<gpio_num_t>(PIN_POWER_BTN)) == 0;
}

[[noreturn]] void sticky_power_enter_deep_sleep()
{
    const gpio_num_t wake_pin = static_cast<gpio_num_t>(PIN_POWER_BTN);
    configure_ai_input();

    ESP_ERROR_CHECK(hold_output(
        static_cast<gpio_num_t>(PIN_POWER_HOLD), 1));
    ESP_ERROR_CHECK(hold_output(
        static_cast<gpio_num_t>(PIN_POWER_LOCK), 1));
    ESP_ERROR_CHECK(hold_output(static_cast<gpio_num_t>(PIN_EPD_EN), 0));
    ESP_ERROR_CHECK(hold_output(static_cast<gpio_num_t>(PIN_TOUCH_EN), 0));
    ESP_ERROR_CHECK(hold_output(static_cast<gpio_num_t>(PIN_TOUCH_RST), 0));
    ESP_ERROR_CHECK(hold_output(static_cast<gpio_num_t>(PIN_MIC_EN), 0));
    ESP_ERROR_CHECK(hold_output(static_cast<gpio_num_t>(PIN_SD_EN), 0));
    ESP_ERROR_CHECK(hold_output(static_cast<gpio_num_t>(PIN_BUZZER), 0));

    // Power down the peripherals immediately at the one-second threshold.
    // EXT1 is level-triggered, so arm it only after this same press is released.
    wait_for_ai_stable_high(kSleepWakePinStableHighMs, false);

    ESP_ERROR_CHECK(esp_sleep_pd_config(
        ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON));
    configure_ai_rtc_wakeup_input();
    wait_for_ai_stable_high(kSleepWakePinStableHighMs, true);
    ESP_ERROR_CHECK(esp_sleep_disable_ext1_wakeup_io(0));
    ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(
        1ULL << PIN_POWER_BTN, ESP_EXT1_WAKEUP_ANY_LOW));
    gpio_deep_sleep_hold_en();

    std::fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_deep_sleep_start();
}
