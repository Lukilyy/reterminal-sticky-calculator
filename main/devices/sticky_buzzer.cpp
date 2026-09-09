#include "sticky_buzzer.h"

#include "driver/ledc.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pin_config.h"

namespace {

constexpr ledc_mode_t kSpeedMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kTimer = LEDC_TIMER_0;
constexpr ledc_channel_t kChannel = LEDC_CHANNEL_0;
constexpr uint32_t kBeepFrequencyHz = 2400;
constexpr uint32_t kBeepDuty = 256;
constexpr TickType_t kBeepDuration = pdMS_TO_TICKS(20);
constexpr TickType_t kBeepGap = pdMS_TO_TICKS(35);
constexpr UBaseType_t kBeepQueueLength = 2;
constexpr uint32_t kTaskStackSize = 2048;
constexpr UBaseType_t kTaskPriority = 4;

struct ToneRequest {
    uint32_t frequency_hz;
    TickType_t duration;
    TickType_t gap;
};

QueueHandle_t s_beep_queue = nullptr;

esp_err_t set_duty(uint32_t duty)
{
    esp_err_t result = ledc_set_duty(kSpeedMode, kChannel, duty);
    if (result != ESP_OK) {
        return result;
    }
    return ledc_update_duty(kSpeedMode, kChannel);
}

void buzzer_task(void *)
{
    ToneRequest request = {};
    while (true) {
        if (xQueueReceive(s_beep_queue, &request, portMAX_DELAY) == pdTRUE) {
            (void)ledc_set_freq(kSpeedMode, kTimer, request.frequency_hz);
            (void)set_duty(kBeepDuty);
            vTaskDelay(request.duration);
            (void)set_duty(0);
            if (request.gap != 0) vTaskDelay(request.gap);
        }
    }
}

}  // namespace

esp_err_t sticky_buzzer_init()
{
    if (s_beep_queue != nullptr) return ESP_OK;

    ledc_timer_config_t timer_config = {};
    timer_config.speed_mode = kSpeedMode;
    timer_config.timer_num = kTimer;
    timer_config.duty_resolution = LEDC_TIMER_10_BIT;
    timer_config.freq_hz = kBeepFrequencyHz;
    timer_config.clk_cfg = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(
        ledc_timer_config(&timer_config), "sticky_buzzer", "configure timer");

    ledc_channel_config_t channel_config = {};
    channel_config.gpio_num = PIN_BUZZER;
    channel_config.speed_mode = kSpeedMode;
    channel_config.channel = kChannel;
    channel_config.intr_type = LEDC_INTR_DISABLE;
    channel_config.timer_sel = kTimer;
    channel_config.duty = 0;
    channel_config.hpoint = 0;
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config),
                        "sticky_buzzer", "configure channel");
    s_beep_queue = xQueueCreate(kBeepQueueLength, sizeof(ToneRequest));
    if (s_beep_queue == nullptr) return ESP_ERR_NO_MEM;
    if (xTaskCreate(buzzer_task, "calculator_buzzer", kTaskStackSize, nullptr,
                    kTaskPriority, nullptr) != pdPASS) {
        vQueueDelete(s_beep_queue);
        s_beep_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t sticky_buzzer_beep()
{
    if (s_beep_queue == nullptr) return ESP_ERR_INVALID_STATE;
    const ToneRequest request = {
        kBeepFrequencyHz, kBeepDuration, kBeepGap};
    // Never block input/engine processing. A short burst can queue several
    // distinct clicks; an excessive burst drops only sound feedback.
    return xQueueSend(s_beep_queue, &request, 0) == pdTRUE ? ESP_OK : ESP_FAIL;
}


namespace {
esp_err_t enqueue_power_tone(uint32_t frequency_hz)
{
    if (s_beep_queue == nullptr) return ESP_ERR_INVALID_STATE;
    (void)xQueueReset(s_beep_queue);
    const ToneRequest request = {
        frequency_hz, pdMS_TO_TICKS(60), 0};
    return xQueueSendToFront(s_beep_queue, &request, 0) == pdTRUE
               ? ESP_OK : ESP_FAIL;
}
}  // namespace

esp_err_t sticky_buzzer_power_on()
{
    return enqueue_power_tone(2400);
}

esp_err_t sticky_buzzer_power_off()
{
    return enqueue_power_tone(1400);
}
