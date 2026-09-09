#pragma once

#include <cstdint>

// True only when this boot was caused by the active-low AI-button EXT1 source.
bool sticky_power_woke_by_ai_button();

// After an EXT1 edge has started the chip, verifies that the AI button remains
// continuously pressed for the requested power-on hold time.
bool sticky_power_confirm_ai_button_hold(uint32_t hold_duration_ms = 1000);

// Waits for the AI button release, configures it as the wake source, preserves
// the board power latch, and enters deep sleep. This function does not return.
[[noreturn]] void sticky_power_enter_deep_sleep();
