#include "app/calculator_app.h"
#include "board/calculator_board.h"
#include "devices/sticky_power.h"
#include "devices/sticky_buzzer.h"
#include "esp_log.h"
#include "esp_sleep.h"

namespace {
constexpr char kTag[] = "calculator";
}  // namespace

extern "C" void app_main()
{
    const esp_err_t board_result = calculator_board_init();
    if (board_result != ESP_OK) {
        ESP_LOGE(kTag, "Board initialization failed: %s",
                 esp_err_to_name(board_result));
        return;
    }

    const esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();
    if (wake_cause == ESP_SLEEP_WAKEUP_UNDEFINED) {
        CalculatorApp::enter_initial_low_power();
    }

    bool suppress_ai_until_release = false;
    if (wake_cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
        if (!sticky_power_woke_by_ai_button()) {
            ESP_LOGW(kTag, "Unexpected deep-sleep wake; returning to sleep");
            sticky_power_enter_deep_sleep();
        }
        if (!sticky_power_confirm_ai_button_hold(500)) {
            sticky_power_enter_deep_sleep();
        }
        suppress_ai_until_release = true;
    }

    const esp_err_t buzzer_result = sticky_buzzer_init();
    if (buzzer_result != ESP_OK) {
        ESP_LOGE(kTag, "Buzzer initialization failed: %s",
                 esp_err_to_name(buzzer_result));
    } else {
        (void)sticky_buzzer_power_on();
    }
    ESP_LOGI(kTag, "Power: ON");

    CalculatorApp app;
    const esp_err_t result = app.start(suppress_ai_until_release);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Calculator startup failed: %s", esp_err_to_name(result));
    }
}
