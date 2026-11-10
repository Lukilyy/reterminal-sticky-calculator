#include "sticky_display.h"

#include <algorithm>
#include <cstring>
#include <new>

#include "canvas.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "epaper_panel.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_config.h"

namespace {

constexpr size_t kFramebufferStride = kStickyDisplayWidth / 4U;
constexpr size_t kFramebufferSize = kFramebufferStride * kStickyDisplayHeight;
constexpr int kNativeWidth = 800;
constexpr int kNativeHeight = 480;
constexpr size_t kMonochromeStride = kNativeWidth / 8U;
constexpr size_t kMonochromeSize = kMonochromeStride * kNativeHeight;
constexpr size_t kGray4Stride = kNativeWidth / 4U;
constexpr size_t kGray4Size = kGray4Stride * kNativeHeight;

seeed_epaper_panel_handle_t s_panel = nullptr;
spi_device_handle_t s_spi_device = nullptr;
uint8_t *s_framebuffer = nullptr;
uint8_t *s_rotated_framebuffer = nullptr;
uint8_t *s_gray4_framebuffer = nullptr;
Canvas *s_canvas = nullptr;
bool s_first_refresh_pending = true;

esp_err_t report_first_refresh(esp_err_t result)
{
    if (s_first_refresh_pending) {
        if (result == ESP_OK) {
            ESP_LOGI("sticky_display", "First display refresh completed");
            s_first_refresh_pending = false;
        } else {
            ESP_LOGE("sticky_display", "First display refresh failed: %s",
                     esp_err_to_name(result));
        }
    }
    return result;
}

void portrait_to_monochrome(const uint8_t *source, uint8_t *destination)
{
    std::memset(destination, 0xFF, kMonochromeSize);
    for (int y = 0; y < kStickyDisplayHeight; ++y) {
        for (int x = 0; x < kStickyDisplayWidth; ++x) {
            const uint8_t packed = source[y * kFramebufferStride + x / 4];
            const uint8_t shift = static_cast<uint8_t>((3 - (x & 3)) * 2);
            const uint8_t gray = static_cast<uint8_t>((packed >> shift) & 0x03U);
            if (gray < 2U) {
                // Same logical-to-native 270-degree mapping verified by 2048.
                const int native_x = kNativeWidth - y - 1;
                const int native_y = x;
                destination[native_y * kMonochromeStride + native_x / 8] &=
                    static_cast<uint8_t>(~(0x80U >> (native_x & 7)));
            }
        }
    }
}

void portrait_to_gray4(const uint8_t *source, uint8_t *destination)
{
    std::memset(destination, 0xFF, kGray4Size);
    for (int y = 0; y < kStickyDisplayHeight; ++y) {
        for (int x = 0; x < kStickyDisplayWidth; ++x) {
            const uint8_t packed = source[y * kFramebufferStride + x / 4];
            const uint8_t source_shift =
                static_cast<uint8_t>((3 - (x & 3)) * 2);
            const uint8_t gray =
                static_cast<uint8_t>((packed >> source_shift) & 0x03U);
            const int native_x = kNativeWidth - y - 1;
            const int native_y = x;
            const uint8_t destination_shift =
                static_cast<uint8_t>((3 - (native_x & 3)) * 2);
            uint8_t &target =
                destination[native_y * kGray4Stride + native_x / 4];
            target = static_cast<uint8_t>(
                (target & ~(0x03U << destination_shift)) |
                (gray << destination_shift));
        }
    }
}

}  // namespace

esp_err_t sticky_display_init()
{
    if (s_panel != nullptr && s_canvas != nullptr) {
        return ESP_OK;
    }

    gpio_config_t power_config = {};
    // Keep control lines idle high before enabling the panel, as in the
    // official board setup and the verified 2048 adapter.
    power_config.pin_bit_mask = (1ULL << PIN_EPD_EN) | (1ULL << PIN_EPD_RST) |
                                (1ULL << PIN_EPD_DC) | (1ULL << PIN_EPD_CS);
    power_config.mode = GPIO_MODE_OUTPUT;
    power_config.pull_up_en = GPIO_PULLUP_DISABLE;
    power_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    power_config.intr_type = GPIO_INTR_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&power_config), "sticky_display", "configure display power");
    const gpio_num_t idle_high_pins[] = {
        static_cast<gpio_num_t>(PIN_EPD_RST),
        static_cast<gpio_num_t>(PIN_EPD_DC),
        static_cast<gpio_num_t>(PIN_EPD_CS),
    };
    for (gpio_num_t pin : idle_high_pins) {
        ESP_RETURN_ON_ERROR(gpio_set_level(pin, 1),
                            "sticky_display", "set display control idle level");
    }
    ESP_RETURN_ON_ERROR(gpio_set_level(static_cast<gpio_num_t>(PIN_EPD_EN), 1),
                        "sticky_display", "enable display power");
    vTaskDelay(pdMS_TO_TICKS(100));

    spi_bus_config_t bus_config = {};
    bus_config.mosi_io_num = PIN_EPD_MOSI;
    bus_config.miso_io_num = PIN_EPD_MISO;
    bus_config.sclk_io_num = PIN_EPD_CLK;
    // ESP-IDF requires every unused SPI data pin to be -1. Leaving these
    // zero-initialized assigns GPIO0 to the SPI bus, but GPIO0 is also the
    // sensor I2C SCL pin. That made SHT40 work before display init and fail
    // immediately afterwards.
    bus_config.quadwp_io_num = -1;
    bus_config.quadhd_io_num = -1;
    bus_config.data4_io_num = -1;
    bus_config.data5_io_num = -1;
    bus_config.data6_io_num = -1;
    bus_config.data7_io_num = -1;
    bus_config.max_transfer_sz = kMonochromeSize;
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO),
                        "sticky_display", "initialize SPI2");

    spi_device_interface_config_t device_config = {};
    device_config.clock_speed_hz = 10 * 1000 * 1000;
    device_config.mode = 0;
    device_config.spics_io_num = PIN_EPD_CS;
    // SSD-family register reads use the bidirectional MOSI line.
    device_config.flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_3WIRE;
    device_config.queue_size = 1;
    ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI2_HOST, &device_config, &s_spi_device),
                        "sticky_display", "add display to SPI2");

    seeed_epaper_panel_config_t panel_config = {};
    panel_config.spi_handle = s_spi_device;
    panel_config.pin_cs = static_cast<gpio_num_t>(PIN_EPD_CS);
    panel_config.pin_dc = static_cast<gpio_num_t>(PIN_EPD_DC);
    panel_config.pin_rst = static_cast<gpio_num_t>(PIN_EPD_RST);
    panel_config.pin_busy = static_cast<gpio_num_t>(PIN_EPD_BUSY);
    panel_config.pin_enable = GPIO_NUM_NC;
    // The component applies the detected controller's final BUSY/reset timing.
    panel_config.busy_timeout_ms = 30000;
    panel_config.reset_low_ms = 20;
    panel_config.reset_high_ms = 10;
    panel_config.busy_level = 0;
    panel_config.busy_level_valid = true;
    panel_config.enable_level = 1;
    panel_config.enable_level_valid = true;
    panel_config.manual_cs = false;
    panel_config.mirror_x = true;
    ESP_RETURN_ON_ERROR(
        seeed_epaper_new_panel(SEEED_EPAPER_PANEL_SSD1677, &panel_config, &s_panel),
        "sticky_display", "create SSD-family panel");

    seeed_epaper_panel_model_t detected_model;
    ESP_RETURN_ON_ERROR(seeed_epaper_panel_get_model(s_panel, &detected_model),
                        "sticky_display", "read detected panel model");
    ESP_LOGI("sticky_display", "Detected e-paper controller: %s",
             detected_model == SEEED_EPAPER_PANEL_SSD2677 ? "SSD2677" : "SSD1677");

    s_framebuffer = static_cast<uint8_t *>(
        heap_caps_malloc(kFramebufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    s_rotated_framebuffer = static_cast<uint8_t *>(
        heap_caps_malloc(kMonochromeSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    s_gray4_framebuffer = static_cast<uint8_t *>(
        heap_caps_malloc(kGray4Size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (s_framebuffer == nullptr || s_rotated_framebuffer == nullptr ||
        s_gray4_framebuffer == nullptr) {
        heap_caps_free(s_framebuffer);
        heap_caps_free(s_rotated_framebuffer);
        heap_caps_free(s_gray4_framebuffer);
        s_framebuffer = nullptr;
        s_rotated_framebuffer = nullptr;
        s_gray4_framebuffer = nullptr;
        return ESP_ERR_NO_MEM;
    }

    s_canvas = new (std::nothrow)
        Canvas(kStickyDisplayWidth, kStickyDisplayHeight, s_framebuffer, kFramebufferSize);
    if (s_canvas == nullptr) {
        heap_caps_free(s_framebuffer);
        heap_caps_free(s_rotated_framebuffer);
        heap_caps_free(s_gray4_framebuffer);
        s_framebuffer = nullptr;
        s_rotated_framebuffer = nullptr;
        s_gray4_framebuffer = nullptr;
        return ESP_ERR_NO_MEM;
    }
    s_canvas->clear();
    return ESP_OK;
}

Canvas *sticky_display_canvas()
{
    return s_canvas;
}

esp_err_t sticky_display_refresh()
{
    return sticky_display_refresh_monochrome();
}

esp_err_t sticky_display_refresh_partial()
{
    if (s_panel == nullptr || s_canvas == nullptr ||
        s_rotated_framebuffer == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    // SSD1677 applies its partial waveform across the panel even when only a
    // small RAM window is written. Send a complete monochrome comparison frame
    // so unchanged pixels have identical previous/current values and remain
    // visually untouched. Callers should change only the intended Canvas area.
    portrait_to_monochrome(s_canvas->data(), s_rotated_framebuffer);

    const seeed_epaper_area_t full_screen = {
        0, 0, kNativeWidth, kNativeHeight,
    };
    return report_first_refresh(seeed_epaper_panel_refresh_area(
        s_panel,
        &full_screen,
        s_rotated_framebuffer,
        kMonochromeStride,
        SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB,
        SEEED_EPAPER_REFRESH_PARTIAL));
}

esp_err_t sticky_display_refresh_partial_area(int x, int y, int width, int height)
{
    if (s_panel == nullptr || s_canvas == nullptr ||
        s_rotated_framebuffer == nullptr || width <= 0 || height <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const int logical_x1 = std::max(0, x);
    const int logical_y1 = std::max(0, y);
    const int logical_x2 = std::min<int>(kStickyDisplayWidth, x + width);
    const int logical_y2 = std::min<int>(kStickyDisplayHeight, y + height);
    if (logical_x1 >= logical_x2 || logical_y1 >= logical_y2) {
        return ESP_ERR_INVALID_ARG;
    }

    // SSD1677's partial waveform is panel-wide even when its RAM write window
    // is small. A window-only previous/current update leaves unrelated pixels
    // driven by stale plane data (the SWITCH was visibly affected by Display
    // refreshes). Send the complete comparison frame, as the verified 2048 and
    // integrated-Demo paths do. Only the caller-redrawn Canvas pixels differ,
    // so the requested logical area remains the only visual change.
    return sticky_display_refresh_partial();
}

esp_err_t sticky_display_refresh_monochrome()
{
    if (s_panel == nullptr || s_canvas == nullptr ||
        s_rotated_framebuffer == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    portrait_to_monochrome(s_canvas->data(), s_rotated_framebuffer);

    const seeed_epaper_area_t full_screen = {
        0, 0, kNativeWidth, kNativeHeight,
    };
    return report_first_refresh(seeed_epaper_panel_refresh_area(
        s_panel,
        &full_screen,
        s_rotated_framebuffer,
        kMonochromeStride,
        SEEED_EPAPER_PIXEL_FORMAT_MONO1_MSB,
        SEEED_EPAPER_REFRESH_FULL));
}

esp_err_t sticky_display_refresh_gray4()
{
    if (s_panel == nullptr || s_canvas == nullptr ||
        s_gray4_framebuffer == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    portrait_to_gray4(s_canvas->data(), s_gray4_framebuffer);
    const seeed_epaper_area_t full_screen = {
        0, 0, kNativeWidth, kNativeHeight,
    };
    ESP_RETURN_ON_ERROR(
        seeed_epaper_panel_write_bitmap_gray4(
            s_panel, &full_screen, s_gray4_framebuffer, kGray4Stride),
        "sticky_display", "write gray4 framebuffer");
    return report_first_refresh(seeed_epaper_panel_commit(
        s_panel, &full_screen, SEEED_EPAPER_REFRESH_GRAY4));
}

esp_err_t sticky_display_clear()
{
    if (s_panel == nullptr || s_canvas == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    s_canvas->clear();
    return report_first_refresh(
        seeed_epaper_panel_clear(s_panel, true, SEEED_EPAPER_REFRESH_FULL));
}

esp_err_t sticky_display_sleep()
{
    if (s_panel == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(
        seeed_epaper_panel_sleep(s_panel),
        "sticky_display", "put panel to sleep");
    return gpio_set_level(static_cast<gpio_num_t>(PIN_EPD_EN), 0);
}
