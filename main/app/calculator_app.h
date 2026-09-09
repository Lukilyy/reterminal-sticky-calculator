#pragma once
#include "esp_err.h"

class CalculatorApp {
public:
    // Cold boots start in the same powered-off state as an AI-button shutdown:
    // show the compiled wallpaper once, power down the display, then sleep.
    [[noreturn]] static void enter_initial_low_power();
    esp_err_t start(bool suppress_ai_until_release = false);
};
