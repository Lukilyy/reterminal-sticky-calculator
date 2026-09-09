#pragma once

#include "esp_err.h"

// Initializes the onboard buzzer for a single short feedback tone.
esp_err_t sticky_buzzer_init();

// Non-blocking enqueue; only feedback is dropped when the small queue is full.
esp_err_t sticky_buzzer_beep();

// Distinct non-blocking power transition tones. These replace any queued
// calculator clicks so shutdown/startup feedback is not delayed.
esp_err_t sticky_buzzer_power_on();
esp_err_t sticky_buzzer_power_off();
