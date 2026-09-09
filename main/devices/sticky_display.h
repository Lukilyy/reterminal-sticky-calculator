#pragma once

#include "esp_err.h"

class Canvas;

constexpr uint16_t kStickyDisplayWidth = 480;
constexpr uint16_t kStickyDisplayHeight = 800;

// Initializes SPI2, powers the SSD1677 panel, and creates the framebuffer.
esp_err_t sticky_display_init();

// Returns the drawing surface after sticky_display_init() succeeds.
Canvas *sticky_display_canvas();

// Rotates the logical portrait framebuffer 270 degrees into the native panel.
// Canvas coordinates remain a normal 480 x 800, top-left-origin system.
esp_err_t sticky_display_refresh();

// Refreshes black-and-white changes made to Canvas since the previous refresh.
// SSD1677 requires a full comparison frame to preserve unchanged pixels; that
// transfer and the physical 180-degree rotation are handled internally.
esp_err_t sticky_display_refresh_partial();

// Requests a logical portrait dirty rectangle. SSD1677 needs a complete
// comparison frame for a stable partial waveform, so unchanged Canvas pixels
// are also transferred with identical previous/current values.
esp_err_t sticky_display_refresh_partial_area(int x, int y, int width, int height);

// Refreshes the full screen with the panel's black-and-white waveform.
// Intended for pages that contain only Black and White canvas pixels.
esp_err_t sticky_display_refresh_monochrome();

// Refreshes the complete 2bpp Canvas with SSD1677's four-gray waveform.
esp_err_t sticky_display_refresh_gray4();

// Clears the physical panel to white and clears the framebuffer.
esp_err_t sticky_display_clear();

// Puts the panel controller to sleep and removes display power. The last
// e-paper image remains visible without power.
esp_err_t sticky_display_sleep();
