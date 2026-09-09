#include "calculator_board.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/rtc_io.h"
#include "esp_err.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_config.h"

namespace {
i2c_master_bus_handle_t s_sensor_bus = nullptr;

esp_err_t output(gpio_num_t pin, uint32_t level)
{
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << pin;
    config.mode = GPIO_MODE_OUTPUT;
    esp_err_t result = gpio_config(&config);
    return result == ESP_OK ? gpio_set_level(pin, level) : result;
}
}

esp_err_t calculator_board_init()
{
    const bool woke_from_deep_sleep =
        esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED;
    if (woke_from_deep_sleep) {
        gpio_set_direction(static_cast<gpio_num_t>(PIN_POWER_HOLD),
                           GPIO_MODE_OUTPUT);
        gpio_set_level(static_cast<gpio_num_t>(PIN_POWER_HOLD), 1);
        gpio_set_direction(static_cast<gpio_num_t>(PIN_POWER_LOCK),
                           GPIO_MODE_OUTPUT);
        gpio_set_level(static_cast<gpio_num_t>(PIN_POWER_LOCK), 1);
    }
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis(static_cast<gpio_num_t>(PIN_POWER_HOLD));
    gpio_hold_dis(static_cast<gpio_num_t>(PIN_POWER_LOCK));
    gpio_hold_dis(static_cast<gpio_num_t>(PIN_POWER_BTN));
    if (woke_from_deep_sleep) {
        // Deep sleep configures the AI key through the RTC GPIO domain. Return
        // it to the digital GPIO domain before the startup hold check/button
        // component uses it.
        (void)rtc_gpio_hold_dis(static_cast<gpio_num_t>(PIN_POWER_BTN));
        (void)rtc_gpio_deinit(static_cast<gpio_num_t>(PIN_POWER_BTN));
    }
    gpio_hold_dis(static_cast<gpio_num_t>(PIN_EPD_EN));
    gpio_hold_dis(static_cast<gpio_num_t>(PIN_TOUCH_EN));
    gpio_hold_dis(static_cast<gpio_num_t>(PIN_TOUCH_RST));
    gpio_hold_dis(static_cast<gpio_num_t>(PIN_MIC_EN));
    gpio_hold_dis(static_cast<gpio_num_t>(PIN_SD_EN));
    gpio_hold_dis(static_cast<gpio_num_t>(PIN_BUZZER));
    gpio_config_t power = {};
    power.pin_bit_mask = (1ULL << PIN_POWER_HOLD) | (1ULL << PIN_POWER_LOCK);
    power.mode = GPIO_MODE_OUTPUT;
    esp_err_t result = gpio_config(&power);
    if (result != ESP_OK) return result;
    result = gpio_set_level(static_cast<gpio_num_t>(PIN_POWER_HOLD), 1);
    if (result != ESP_OK) return result;
    result = gpio_set_level(static_cast<gpio_num_t>(PIN_POWER_LOCK), 1);
    if (result != ESP_OK) return result;
    result = gpio_install_isr_service(0);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;
    result = output(static_cast<gpio_num_t>(PIN_SD_CS), 1);
    if (result != ESP_OK) return result;
    result = output(static_cast<gpio_num_t>(PIN_SD_EN), 1);
    if (result != ESP_OK) return result;

    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_1;
    bus_config.sda_io_num = static_cast<gpio_num_t>(PIN_SENSOR_SDA);
    bus_config.scl_io_num = static_cast<gpio_num_t>(PIN_SENSOR_SCL);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = 1;
    result = i2c_new_master_bus(&bus_config, &s_sensor_bus);
    if (result != ESP_OK) return result;

    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

i2c_master_bus_handle_t calculator_board_sensor_i2c_bus()
{
    return s_sensor_bus;
}
