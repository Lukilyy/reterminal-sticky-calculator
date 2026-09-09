#include "calculator_app.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cstring>
#include <string>
#include <utility>

#include "board/calculator_board.h"
#include "calculator_engine.h"
#include "devices/sticky_ai_button.h"
#include "devices/sticky_battery.h"
#include "devices/sticky_buttons.h"
#include "devices/sticky_buzzer.h"
#include "devices/sticky_charger.h"
#include "devices/sticky_display.h"
#include "devices/sticky_power.h"
#include "devices/sticky_touch.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ui/canvas.h"
#include "ui/calculator_wallpaper_view.h"
#include "ui/datamath_view.h"
#include "ui/iphone_calculator_view.h"

namespace {
constexpr char kTag[] = "calculator_app";
constexpr uint8_t kFullRefreshInterval = 15;
constexpr TickType_t kDisplayCoalesceDelay = pdMS_TO_TICKS(30);
constexpr uint32_t kDisplayTaskStackSize = 4096;
constexpr UBaseType_t kDisplayTaskPriority = 4;
constexpr uint32_t kSavedPageMagic = 0x43414C43U;  // "CALC"

RTC_NOINIT_ATTR uint32_t s_saved_page_magic;
RTC_NOINIT_ATTR uint8_t s_saved_page;

enum class CalculatorPage : uint8_t {
    Datamath,
    Iphone,
};

struct DisplayUpdate {
    char text[32];
    char expression[64];
    CalculatorPage page;
    bool low_power;
};

struct IphoneExpressionState {
    std::string prefix;
    std::string text;
    bool equals_shown = false;

    void clear()
    {
        prefix.clear();
        text.clear();
        equals_shown = false;
    }
};

constexpr size_t kIphoneHistoryCapacity = 5;

struct IphoneHistoryEntry {
    std::string expression;
    std::string result;
};

struct IphoneHistoryModel {
    std::array<IphoneHistoryEntry, kIphoneHistoryCapacity> entries;
    size_t count = 0;

    void add(std::string expression, const char *result)
    {
        if (expression.size() >= 2 &&
            expression.compare(expression.size() - 2, 2, " =") == 0) {
            expression.erase(expression.size() - 2);
        }
        const size_t last = std::min(count, kIphoneHistoryCapacity - 1);
        for (size_t index = last; index > 0; --index) {
            entries[index] = entries[index - 1];
        }
        entries[0] = {std::move(expression), result == nullptr ? "0" : result};
        if (count < kIphoneHistoryCapacity) ++count;
    }
};

const char *iphone_operator_label(IphoneKeyId id)
{
    switch (id) {
    case IphoneKeyId::Add: return "+";
    case IphoneKeyId::Subtract: return "-";
    case IphoneKeyId::Multiply: return "x";
    case IphoneKeyId::Divide: return "/";
    default: return nullptr;
    }
}

const char *iphone_display_text(const CalculatorEngine &engine)
{
    return !engine.is_entering() && std::strcmp(engine.display(), "0.") == 0
               ? "0" : engine.display();
}

void update_iphone_expression_before(IphoneExpressionState &state,
                                     IphoneKeyId id)
{
    const bool starts_entry = id == IphoneKeyId::Zero ||
        id == IphoneKeyId::One || id == IphoneKeyId::Two ||
        id == IphoneKeyId::Three || id == IphoneKeyId::Four ||
        id == IphoneKeyId::Five || id == IphoneKeyId::Six ||
        id == IphoneKeyId::Seven || id == IphoneKeyId::Eight ||
        id == IphoneKeyId::Nine || id == IphoneKeyId::Decimal;
    if (starts_entry && state.equals_shown) state.clear();
    if (id == IphoneKeyId::Delete && state.equals_shown) state.clear();
    if (id == IphoneKeyId::Clear) state.clear();
}

void update_iphone_expression_after(IphoneExpressionState &state,
                                    IphoneKeyId id,
                                    const char *before,
                                    const char *after)
{
    if (id == IphoneKeyId::Clear) return;
    if (const char *operation = iphone_operator_label(id)) {
        state.prefix = std::string(after) + " " + operation + " ";
        state.text = state.prefix;
        state.equals_shown = false;
        return;
    }
    if (id == IphoneKeyId::Equals) {
        if (!state.prefix.empty()) {
            state.text = state.prefix + before + " =";
            state.prefix.clear();
            state.equals_shown = true;
        }
        return;
    }
    if (!state.prefix.empty()) state.text = state.prefix + after;
}

struct DisplayTaskContext {
    Canvas *canvas = nullptr;
    CalculatorEngine *engine = nullptr;
    std::atomic<CalculatorPage> *current_page = nullptr;
    std::atomic<bool> *history_open = nullptr;
    IphoneHistoryModel *history = nullptr;
    QueueHandle_t display_queue = nullptr;
    QueueHandle_t switch_queue = nullptr;
    QueueHandle_t full_queue = nullptr;
    SemaphoreHandle_t low_power_ready = nullptr;
    TaskHandle_t task = nullptr;
};

struct BatteryTaskContext {
    bool battery_ready;
    bool charger_ready;
};

void battery_task(void *context_pointer)
{
    const auto &context = *static_cast<BatteryTaskContext *>(context_pointer);
    while (true) {
        int percent = -1;
        const char *charging_text = "unavailable";
        if (context.battery_ready) {
            BatteryReading reading = {};
            if (sticky_battery_read(reading) == ESP_OK) {
                percent = reading.percent;
            }
        }
        if (context.charger_ready) {
            bool charging = false;
            if (sticky_charger_read(charging) == ESP_OK) {
                charging_text = charging ? "YES" : "NO";
            }
        }
        if (percent >= 0) {
            ESP_LOGI(kTag, "Battery: %d%% | Charging: %s",
                     percent, charging_text);
        } else {
            ESP_LOGW(kTag, "Battery: unavailable | Charging: %s",
                     charging_text);
        }
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

CalculatorPage restored_page()
{
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1 &&
        s_saved_page_magic == kSavedPageMagic &&
        s_saved_page <= static_cast<uint8_t>(CalculatorPage::Iphone)) {
        s_saved_page_magic = 0;
        return static_cast<CalculatorPage>(s_saved_page);
    }
    return CalculatorPage::Datamath;
}

void save_page(CalculatorPage page)
{
    s_saved_page = static_cast<uint8_t>(page);
    s_saved_page_magic = kSavedPageMagic;
}

esp_err_t draw_and_refresh_full(DisplayTaskContext &context,
                                CalculatorPage page, const char *text,
                                const char *expression)
{
    if (page == CalculatorPage::Datamath) {
        DatamathView(*context.canvas).draw(text, context.engine->mode());
        return sticky_display_refresh_monochrome();
    }
    IphoneCalculatorView iphone(*context.canvas);
    if (context.history_open->load()) {
        std::array<IphoneHistoryItem, kIphoneHistoryCapacity> items = {};
        for (size_t index = 0; index < context.history->count; ++index) {
            items[index] = {context.history->entries[index].expression.c_str(),
                            context.history->entries[index].result.c_str()};
        }
        iphone.draw_history(items.data(), context.history->count);
    } else {
        iphone.draw(text, expression);
    }
    return sticky_display_refresh_monochrome();
}

void display_task(void *context_pointer)
{
    auto &context = *static_cast<DisplayTaskContext *>(context_pointer);
    DatamathView datamath(*context.canvas);
    IphoneCalculatorView iphone(*context.canvas);
    uint8_t partial_refresh_count = 0;

    while (true) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        DisplayUpdate full_update = {};
        if (xQueueReceive(context.full_queue, &full_update, 0) == pdTRUE) {
            esp_err_t result = ESP_OK;
            if (full_update.low_power) {
                draw_calculator_wallpaper(*context.canvas);
                result = sticky_display_refresh_monochrome();
                xSemaphoreGive(context.low_power_ready);
            } else if (full_update.page == context.current_page->load()) {
                result = draw_and_refresh_full(
                    context, full_update.page, full_update.text,
                    full_update.expression);
            }
            if (result == ESP_OK) {
                partial_refresh_count = 0;
            } else {
                ESP_LOGE(kTag, "Page full refresh failed: %s",
                         esp_err_to_name(result));
            }
        }

        uint8_t switch_request = 0;
        if (xQueueReceive(context.switch_queue, &switch_request, 0) == pdTRUE &&
            context.current_page->load() == CalculatorPage::Datamath) {
            datamath.draw_switch_state(context.engine->mode());
            int x = 0, y = 0, width = 0, height = 0;
            DatamathView::switch_bounds(x, y, width, height);
            const esp_err_t result =
                sticky_display_refresh_partial_area(x, y, width, height);
            if (result != ESP_OK) {
                ESP_LOGE(kTag, "Switch refresh failed: %s",
                         esp_err_to_name(result));
            }
        }

        DisplayUpdate update = {};
        if (xQueueReceive(context.display_queue, &update, 0) != pdTRUE) continue;
        // Give a fast key burst a brief opportunity to replace this value
        // before starting the non-cancellable E-Ink waveform.
        vTaskDelay(kDisplayCoalesceDelay);
        if (uxQueueMessagesWaiting(context.full_queue) != 0U) {
            continue;
        }
        DisplayUpdate newer = {};
        while (xQueueReceive(context.display_queue, &newer, 0) == pdTRUE) {
            update = newer;
        }
        if (update.page != context.current_page->load()) continue;

        ++partial_refresh_count;
        esp_err_t result = ESP_OK;
        if (partial_refresh_count >= kFullRefreshInterval) {
            result = draw_and_refresh_full(context, update.page, update.text,
                                           update.expression);
            if (result == ESP_OK) partial_refresh_count = 0;
        } else {
            int x = 0, y = 0, width = 0, height = 0;
            if (update.page == CalculatorPage::Datamath) {
                datamath.draw_display_value(update.text);
                DatamathView::display_bounds(x, y, width, height);
            } else {
                iphone.draw_display_value(update.text, update.expression);
                IphoneCalculatorView::display_bounds(x, y, width, height);
            }
            result = sticky_display_refresh_partial_area(x, y, width, height);
        }
        if (result != ESP_OK) {
            ESP_LOGE(kTag, "Display refresh failed: %s",
                     esp_err_to_name(result));
        }
    }
}

void copy_display(DisplayUpdate &update, CalculatorPage page, const char *text,
                  const char *expression = nullptr)
{
    update = {};
    update.page = page;
    update.low_power = false;
    std::strncpy(update.text, text, sizeof(update.text) - 1);
    if (expression != nullptr) {
        std::strncpy(update.expression, expression,
                     sizeof(update.expression) - 1);
    }
}

void post_low_power_page(DisplayTaskContext &context, CalculatorPage page)
{
    DisplayUpdate update = {};
    copy_display(update, page, "");
    update.low_power = true;
    (void)xQueueReset(context.display_queue);
    (void)xQueueReset(context.switch_queue);
    (void)xQueueOverwrite(context.full_queue, &update);
    xTaskNotifyGive(context.task);
}

void post_latest_display(DisplayTaskContext &context, CalculatorPage page,
                         const char *text, const char *expression = nullptr)
{
    DisplayUpdate update = {};
    copy_display(update, page, text, expression);
    (void)xQueueOverwrite(context.display_queue, &update);
    xTaskNotifyGive(context.task);
}

void post_switch_refresh(DisplayTaskContext &context)
{
    uint8_t request = 1;
    (void)xQueueOverwrite(context.switch_queue, &request);
    xTaskNotifyGive(context.task);
}

void post_full_refresh(DisplayTaskContext &context, CalculatorPage page,
                       const char *text, const char *expression = nullptr)
{
    DisplayUpdate update = {};
    copy_display(update, page, text, expression);
    (void)xQueueReset(context.display_queue);
    (void)xQueueReset(context.switch_queue);
    (void)xQueueOverwrite(context.full_queue, &update);
    xTaskNotifyGive(context.task);
}

void dispatch_datamath_key(CalculatorEngine &engine, CalculatorKeyId id)
{
    switch (id) {
    case CalculatorKeyId::Zero: engine.input_digit('0', 8); break;
    case CalculatorKeyId::One: engine.input_digit('1', 8); break;
    case CalculatorKeyId::Two: engine.input_digit('2', 8); break;
    case CalculatorKeyId::Three: engine.input_digit('3', 8); break;
    case CalculatorKeyId::Four: engine.input_digit('4', 8); break;
    case CalculatorKeyId::Five: engine.input_digit('5', 8); break;
    case CalculatorKeyId::Six: engine.input_digit('6', 8); break;
    case CalculatorKeyId::Seven: engine.input_digit('7', 8); break;
    case CalculatorKeyId::Eight: engine.input_digit('8', 8); break;
    case CalculatorKeyId::Nine: engine.input_digit('9', 8); break;
    case CalculatorKeyId::Decimal: engine.input_decimal(); break;
    case CalculatorKeyId::Add:
        engine.input_operation(CalculatorOperation::Add); break;
    case CalculatorKeyId::Subtract:
        engine.input_operation(CalculatorOperation::Subtract); break;
    case CalculatorKeyId::Multiply:
        engine.input_operation(CalculatorOperation::Multiply); break;
    case CalculatorKeyId::Divide:
        engine.input_operation(CalculatorOperation::Divide); break;
    case CalculatorKeyId::Equals: engine.input_equals(); break;
    case CalculatorKeyId::ClearEntry: engine.clear_entry(); break;
    case CalculatorKeyId::Clear: engine.clear_all(); break;
    case CalculatorKeyId::Switch: {
        const CalculatorMode old_mode = engine.mode();
        const CalculatorMode new_mode = engine.toggle_mode();
        ESP_LOGI(kTag, "mode: %s -> %s",
                 old_mode == CalculatorMode::Chain ? "CHAIN" : "CONST",
                 new_mode == CalculatorMode::Chain ? "CHAIN" : "CONST");
        break;
    }
    }
}

void dispatch_iphone_key(CalculatorEngine &engine, IphoneKeyId id)
{
    switch (id) {
    case IphoneKeyId::Delete: engine.delete_last_digit(); break;
    case IphoneKeyId::Zero: engine.input_digit('0', 15); break;
    case IphoneKeyId::One: engine.input_digit('1', 15); break;
    case IphoneKeyId::Two: engine.input_digit('2', 15); break;
    case IphoneKeyId::Three: engine.input_digit('3', 15); break;
    case IphoneKeyId::Four: engine.input_digit('4', 15); break;
    case IphoneKeyId::Five: engine.input_digit('5', 15); break;
    case IphoneKeyId::Six: engine.input_digit('6', 15); break;
    case IphoneKeyId::Seven: engine.input_digit('7', 15); break;
    case IphoneKeyId::Eight: engine.input_digit('8', 15); break;
    case IphoneKeyId::Nine: engine.input_digit('9', 15); break;
    case IphoneKeyId::Decimal: engine.input_decimal(); break;
    case IphoneKeyId::Add:
        engine.input_operation(CalculatorOperation::Add); break;
    case IphoneKeyId::Subtract:
        engine.input_operation(CalculatorOperation::Subtract); break;
    case IphoneKeyId::Multiply:
        engine.input_operation(CalculatorOperation::Multiply); break;
    case IphoneKeyId::Divide:
        engine.input_operation(CalculatorOperation::Divide); break;
    case IphoneKeyId::Equals: engine.input_equals_standard(); break;
    case IphoneKeyId::Clear: engine.clear_all(); break;
    case IphoneKeyId::ToggleSign: engine.input_toggle_sign(); break;
    case IphoneKeyId::Percent: engine.input_percent(); break;
    }
}
}  // namespace

[[noreturn]] void CalculatorApp::enter_initial_low_power()
{
    esp_err_t result = sticky_display_init();
    if (result == ESP_OK) {
        Canvas *canvas = sticky_display_canvas();
        if (canvas != nullptr) {
            draw_calculator_wallpaper(*canvas);
            result = sticky_display_refresh_monochrome();
        } else {
            result = ESP_ERR_INVALID_STATE;
        }
    }
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Power-off wallpaper refresh failed: %s",
                 esp_err_to_name(result));
    } else {
        const esp_err_t sleep_result = sticky_display_sleep();
        if (sleep_result != ESP_OK) {
            ESP_LOGE(kTag, "Display sleep failed: %s",
                     esp_err_to_name(sleep_result));
        }
    }
    ESP_LOGI(kTag, "Power: OFF");
    sticky_power_enter_deep_sleep();
}

esp_err_t CalculatorApp::start(bool suppress_ai_until_release)
{
    esp_err_t result = sticky_display_init();
    if (result != ESP_OK) return result;
    Canvas *canvas = sticky_display_canvas();
    if (canvas == nullptr) return ESP_ERR_INVALID_STATE;

    CalculatorEngine engine;
    IphoneExpressionState iphone_expression;
    IphoneHistoryModel iphone_history;
    const CalculatorPage initial_page = restored_page();
    std::atomic<CalculatorPage> current_page{initial_page};
    std::atomic<bool> history_open{false};
    if (initial_page == CalculatorPage::Datamath) {
        DatamathView(*canvas).draw(engine.display(), engine.mode());
    } else {
        IphoneCalculatorView(*canvas).draw(iphone_display_text(engine));
    }
    result = sticky_display_refresh_monochrome();
    if (result != ESP_OK) return result;

    result = sticky_buzzer_init();
    if (result != ESP_OK) return result;

    QueueHandle_t display_queue = xQueueCreate(1, sizeof(DisplayUpdate));
    QueueHandle_t switch_queue = xQueueCreate(1, sizeof(uint8_t));
    QueueHandle_t full_queue = xQueueCreate(1, sizeof(DisplayUpdate));
    SemaphoreHandle_t low_power_ready = xSemaphoreCreateBinary();
    if (display_queue == nullptr || switch_queue == nullptr ||
        full_queue == nullptr || low_power_ready == nullptr) return ESP_ERR_NO_MEM;

    static DisplayTaskContext display_context;
    display_context = {canvas, &engine, &current_page, &history_open,
                       &iphone_history, display_queue, switch_queue,
                       full_queue, low_power_ready, nullptr};
    if (xTaskCreate(display_task, "calculator_display", kDisplayTaskStackSize,
                    &display_context, kDisplayTaskPriority,
                    &display_context.task) != pdPASS) return ESP_ERR_NO_MEM;

    static StickyTouch touch;
    if (!touch.init()) return ESP_FAIL;
    static StickyAiButton ai_button;
    const bool ai_button_ready = ai_button.init(suppress_ai_until_release);
    static StickyPageButtons page_buttons;
    result = page_buttons.init();
    if (result != ESP_OK) return result;

    const bool battery_ready =
        sticky_battery_init(calculator_board_sensor_i2c_bus()) == ESP_OK;
    const bool charger_ready = sticky_charger_init() == ESP_OK;
    static BatteryTaskContext battery_context;
    battery_context = {battery_ready, charger_ready};
    if (xTaskCreate(battery_task, "calculator_battery", 3072,
                    &battery_context, 2, nullptr) != pdPASS) {
        ESP_LOGW(kTag, "Battery logger task was not started");
    }

    while (true) {
        if (ai_button_ready && ai_button.poll_long_press()) {
            const CalculatorPage page = current_page.load();
            save_page(page);
            touch.stop();
            (void)sticky_buzzer_power_off();
            post_low_power_page(display_context, page);
            (void)xSemaphoreTake(low_power_ready, portMAX_DELAY);
            (void)sticky_display_sleep();
            ESP_LOGI(kTag, "Power: OFF");
            sticky_power_enter_deep_sleep();
        }
        if (ai_button_ready && ai_button.poll_short_press()) {
            engine.clear_all();
            iphone_expression.clear();
            history_open.store(false);
            const CalculatorPage page = current_page.load();
            ESP_LOGI(kTag, "AI clear; full refresh page=%s",
                     page == CalculatorPage::Datamath ? "TI" : "iPhone");
            post_full_refresh(display_context, page,
                              page == CalculatorPage::Iphone
                                  ? iphone_display_text(engine)
                                  : engine.display());
        }

        const StickyPageButtonEvent page_event = page_buttons.poll();
        CalculatorPage page = current_page.load();
        if (page_event == StickyPageButtonEvent::Down ||
            page_event == StickyPageButtonEvent::Up) {
            history_open.store(false);
            page = page == CalculatorPage::Datamath
                       ? CalculatorPage::Iphone : CalculatorPage::Datamath;
            current_page.store(page);
            ESP_LOGI(kTag, "page=%s",
                     page == CalculatorPage::Datamath ? "TI" : "iPhone");
            post_full_refresh(display_context, page,
                              page == CalculatorPage::Iphone
                                  ? iphone_display_text(engine)
                                  : engine.display(),
                              page == CalculatorPage::Iphone
                                  ? iphone_expression.text.c_str() : nullptr);
        }

        StickyTouchPoint point;
        if (touch.poll_tap(point)) {
            page = current_page.load();
            if (page == CalculatorPage::Datamath) {
                const CalculatorKeyLayout *key =
                    DatamathView::hit_test(point.x, point.y);
                if (key != nullptr) {
                    dispatch_datamath_key(engine, key->id);
                    if (key->id != CalculatorKeyId::Switch) {
                        iphone_expression.clear();
                    }
                    (void)sticky_buzzer_beep();
                    if (key->id == CalculatorKeyId::Switch) {
                        post_switch_refresh(display_context);
                    } else {
                        post_latest_display(display_context, page,
                                            engine.display());
                    }
                }
            } else {
                if (history_open.load()) {
                    const int history_index =
                        IphoneCalculatorView::history_item_hit_test(
                            point.x, point.y, iphone_history.count);
                    if (history_index >= 0) {
                        const IphoneHistoryEntry &entry =
                            iphone_history.entries[history_index];
                        if (engine.restore_result(entry.result.c_str())) {
                            iphone_expression.clear();
                            history_open.store(false);
                            (void)sticky_buzzer_beep();
                            post_full_refresh(display_context, page,
                                              iphone_display_text(engine));
                        }
                    } else if (IphoneCalculatorView::history_back_hit_test(
                                   point.x, point.y)) {
                        history_open.store(false);
                        (void)sticky_buzzer_beep();
                        post_full_refresh(display_context, page,
                                          iphone_display_text(engine),
                                          iphone_expression.text.c_str());
                    }
                    vTaskDelay(pdMS_TO_TICKS(5));
                    continue;
                }
                if (IphoneCalculatorView::history_button_hit_test(
                        point.x, point.y)) {
                    history_open.store(true);
                    (void)sticky_buzzer_beep();
                    post_full_refresh(display_context, page,
                                      iphone_display_text(engine),
                                      iphone_expression.text.c_str());
                    vTaskDelay(pdMS_TO_TICKS(5));
                    continue;
                }
                const IphoneKeyLayout *key =
                    IphoneCalculatorView::hit_test(point.x, point.y);
                if (key != nullptr) {
                    const std::string before = engine.display();
                    const bool completes_new_expression =
                        key->id == IphoneKeyId::Equals &&
                        !iphone_expression.prefix.empty() &&
                        engine.is_entering() && !engine.has_error();
                    update_iphone_expression_before(iphone_expression, key->id);
                    dispatch_iphone_key(engine, key->id);
                    update_iphone_expression_after(
                        iphone_expression, key->id, before.c_str(),
                        engine.display());
                    if (completes_new_expression &&
                        !iphone_expression.text.empty() &&
                        !engine.has_error()) {
                        iphone_history.add(iphone_expression.text,
                                           engine.display());
                    }
                    (void)sticky_buzzer_beep();
                    post_latest_display(display_context, page,
                                        iphone_display_text(engine),
                                        iphone_expression.text.c_str());
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
