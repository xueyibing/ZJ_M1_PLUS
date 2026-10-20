#include "ui_debug_stress_test.h"

#include "buzzer.h"
#include "data_manager.h"
#include "ui.h"
#include "ui_alert.h"
#include "ui_common.h"
#include "ui_debug.h"
#include "ui_debug_internal.h"
#include "ui_uart_bridge.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#define UI_DEBUG_STRESS_ROW_X              4
#define UI_DEBUG_STRESS_ROW_W              312
#define UI_DEBUG_STRESS_ROW_H              26
#define UI_DEBUG_STRESS_ROW_GAP            2
#define UI_DEBUG_STRESS_HEADER_Y           46
#define UI_DEBUG_STRESS_LIST_Y             62
#define UI_DEBUG_STRESS_COL_GAP            4
#define UI_DEBUG_STRESS_PROJECT_W          40
#define UI_DEBUG_STRESS_COUNT_W            90
#define UI_DEBUG_STRESS_INTERVAL_W         72
#define UI_DEBUG_STRESS_EXECUTED_W         54
#define UI_DEBUG_STRESS_ACTION_W           40
#define UI_DEBUG_STRESS_SMALL_BTN_W        22
#define UI_DEBUG_STRESS_COUNT_BOX_W        42
#define UI_DEBUG_STRESS_INTERVAL_BOX_W     24
#define UI_DEBUG_STRESS_CTRL_H             20
#define UI_DEBUG_STRESS_COUNT_X            (UI_DEBUG_STRESS_PROJECT_W + UI_DEBUG_STRESS_COL_GAP)
#define UI_DEBUG_STRESS_INTERVAL_X         (UI_DEBUG_STRESS_COUNT_X + UI_DEBUG_STRESS_COUNT_W + UI_DEBUG_STRESS_COL_GAP)
#define UI_DEBUG_STRESS_EXECUTED_X         (UI_DEBUG_STRESS_INTERVAL_X + UI_DEBUG_STRESS_INTERVAL_W + UI_DEBUG_STRESS_COL_GAP)
#define UI_DEBUG_STRESS_ACTION_X           (UI_DEBUG_STRESS_EXECUTED_X + UI_DEBUG_STRESS_EXECUTED_W + UI_DEBUG_STRESS_COL_GAP)
#define UI_DEBUG_STRESS_COUNT_MIN          500
#define UI_DEBUG_STRESS_COUNT_MAX          50000
#define UI_DEBUG_STRESS_COUNT_STEP         500
#define UI_DEBUG_STRESS_COUNT_DEFAULT      5000
#define UI_DEBUG_STRESS_INTERVAL_MIN       10
#define UI_DEBUG_STRESS_INTERVAL_MAX       100
#define UI_DEBUG_STRESS_INTERVAL_STEP      10
#define UI_DEBUG_STRESS_INTERVAL_DEFAULT   30
#define UI_DEBUG_STRESS_ACK_TIMEOUT_MS     1000
#define UI_DEBUG_STRESS_ITEM_COUNT         6U
#define UI_DEBUG_STRESS_ACTION_STRIDE      8U
#define UI_DEBUG_STRESS_TIMER_MS           100U

static const char *TAG = "ui_debug_stress";

typedef enum {
    UI_DEBUG_STRESS_ACTION_COUNT_DEC = 0,
    UI_DEBUG_STRESS_ACTION_COUNT_INC,
    UI_DEBUG_STRESS_ACTION_INTERVAL_DEC,
    UI_DEBUG_STRESS_ACTION_INTERVAL_INC,
    UI_DEBUG_STRESS_ACTION_EXECUTE,
} ui_debug_stress_action_t;

typedef struct {
    const char *title;
    const char *action;
    uint16_t timeout_ms;
} ui_debug_stress_item_def_t;

typedef struct {
    bool active;
    lv_timer_t *timer;
    ui_data_t *data;
    bool runner_active[UI_DEBUG_STRESS_ITEM_COUNT];
    bool waiting_result;
    size_t in_flight_item_index;
    uint32_t next_dispatch_ms[UI_DEBUG_STRESS_ITEM_COUNT];
    uint32_t last_sts_seq;
    int target_count[UI_DEBUG_STRESS_ITEM_COUNT];
    int interval_s[UI_DEBUG_STRESS_ITEM_COUNT];
    int executed_count[UI_DEBUG_STRESS_ITEM_COUNT];
    lv_obj_t *target_count_label[UI_DEBUG_STRESS_ITEM_COUNT];
    lv_obj_t *interval_label[UI_DEBUG_STRESS_ITEM_COUNT];
    lv_obj_t *executed_count_label[UI_DEBUG_STRESS_ITEM_COUNT];
    lv_obj_t *action_button[UI_DEBUG_STRESS_ITEM_COUNT];
    lv_obj_t *action_button_label[UI_DEBUG_STRESS_ITEM_COUNT];
} ui_debug_stress_test_view_t;

static ui_debug_stress_test_view_t s_debug_stress_test;

static const ui_debug_stress_item_def_t k_stress_items[UI_DEBUG_STRESS_ITEM_COUNT] = {
    {"蛋托", "GIVEEGG", UI_DEBUG_STRESS_ACK_TIMEOUT_MS},
    {"打蛋", "BEAT", UI_DEBUG_STRESS_ACK_TIMEOUT_MS},
    {"转运", "TRANS", UI_DEBUG_STRESS_ACK_TIMEOUT_MS},
    {"翻B", "TURN_POTB", UI_DEBUG_STRESS_ACK_TIMEOUT_MS},
    {"翻A", "TURN_POTA", UI_DEBUG_STRESS_ACK_TIMEOUT_MS},
    {"给油", "GIVE_OIL", UI_DEBUG_STRESS_ACK_TIMEOUT_MS},
};

static void ui_debug_stress_action_event_cb(lv_event_t *e);
static void ui_debug_stress_timer_cb(lv_timer_t *timer);
static bool ui_debug_stress_has_active_runner(void);
static bool ui_debug_stress_has_pending_work(void);
static bool ui_debug_stress_find_dispatch_item(size_t *item_index_out);

static uintptr_t ui_debug_stress_encode_action(size_t item_index, ui_debug_stress_action_t action)
{
    return (uintptr_t)(item_index * UI_DEBUG_STRESS_ACTION_STRIDE + (size_t)action);
}

static size_t ui_debug_stress_decode_item(uintptr_t encoded)
{
    return (size_t)(encoded / UI_DEBUG_STRESS_ACTION_STRIDE);
}

static ui_debug_stress_action_t ui_debug_stress_decode_action(uintptr_t encoded)
{
    return (ui_debug_stress_action_t)(encoded % UI_DEBUG_STRESS_ACTION_STRIDE);
}

static int ui_debug_stress_clamp_count(int value)
{
    if(value < UI_DEBUG_STRESS_COUNT_MIN) {
        return UI_DEBUG_STRESS_COUNT_MIN;
    }
    if(value > UI_DEBUG_STRESS_COUNT_MAX) {
        return UI_DEBUG_STRESS_COUNT_MAX;
    }
    return value;
}

static int ui_debug_stress_clamp_interval(int value)
{
    if(value < UI_DEBUG_STRESS_INTERVAL_MIN) {
        return UI_DEBUG_STRESS_INTERVAL_MIN;
    }
    if(value > UI_DEBUG_STRESS_INTERVAL_MAX) {
        return UI_DEBUG_STRESS_INTERVAL_MAX;
    }
    return value;
}

static void ui_debug_stress_clear_view_refs(void)
{
    size_t item_index;

    for(item_index = 0; item_index < UI_DEBUG_STRESS_ITEM_COUNT; item_index++) {
        s_debug_stress_test.target_count_label[item_index] = NULL;
        s_debug_stress_test.interval_label[item_index] = NULL;
        s_debug_stress_test.executed_count_label[item_index] = NULL;
        s_debug_stress_test.action_button[item_index] = NULL;
        s_debug_stress_test.action_button_label[item_index] = NULL;
    }
}

static void ui_debug_stress_update_action_buttons(void)
{
    size_t item_index;

    if(!s_debug_stress_test.active) {
        return;
    }

    for(item_index = 0; item_index < UI_DEBUG_STRESS_ITEM_COUNT; item_index++) {
        bool is_running_item = s_debug_stress_test.runner_active[item_index];

        if(s_debug_stress_test.action_button_label[item_index] != NULL) {
            lv_label_set_text(s_debug_stress_test.action_button_label[item_index],
                              is_running_item ? "停止" : "执行");
        }

        if(s_debug_stress_test.action_button[item_index] != NULL) {
            lv_obj_clear_state(s_debug_stress_test.action_button[item_index], LV_STATE_DISABLED);
        }
    }
}

static void ui_debug_stress_sync_from_data_manager(void)
{
    size_t item_index;

    for(item_index = 0; item_index < UI_DEBUG_STRESS_ITEM_COUNT; item_index++) {
        data_manager_stress_record_t record;

        data_manager_get_stress_record(item_index, &record);
        s_debug_stress_test.target_count[item_index] = record.test_count;
        s_debug_stress_test.interval_s[item_index] = record.interval_s;
        s_debug_stress_test.executed_count[item_index] = record.executed_count;
    }
}

static void ui_debug_stress_persist_config(size_t item_index)
{
    esp_err_t err;

    err = data_manager_set_stress_test_count(item_index,
                                             s_debug_stress_test.target_count[item_index]);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "persist test_count failed item=%u: %s",
                 (unsigned)item_index,
                 esp_err_to_name(err));
    }

    err = data_manager_set_stress_interval(item_index,
                                           s_debug_stress_test.interval_s[item_index]);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "persist interval failed item=%u: %s",
                 (unsigned)item_index,
                 esp_err_to_name(err));
    }
}

static void ui_debug_stress_persist_executed(size_t item_index, bool force_flush)
{
    esp_err_t err = data_manager_set_stress_executed_count(item_index,
                                                           s_debug_stress_test.executed_count[item_index],
                                                           force_flush);

    if(err != ESP_OK) {
        ESP_LOGW(TAG, "persist executed failed item=%u: %s",
                 (unsigned)item_index,
                 esp_err_to_name(err));
    }
}

static void ui_debug_stress_init_defaults(void)
{
    size_t item_index;

    for(item_index = 0; item_index < UI_DEBUG_STRESS_ITEM_COUNT; item_index++) {
        if(s_debug_stress_test.target_count[item_index] < UI_DEBUG_STRESS_COUNT_MIN) {
            s_debug_stress_test.target_count[item_index] = UI_DEBUG_STRESS_COUNT_DEFAULT;
        } else {
            s_debug_stress_test.target_count[item_index] =
                ui_debug_stress_clamp_count(s_debug_stress_test.target_count[item_index]);
        }

        if(s_debug_stress_test.interval_s[item_index] < UI_DEBUG_STRESS_INTERVAL_MIN) {
            s_debug_stress_test.interval_s[item_index] = UI_DEBUG_STRESS_INTERVAL_DEFAULT;
        } else {
            s_debug_stress_test.interval_s[item_index] =
                ui_debug_stress_clamp_interval(s_debug_stress_test.interval_s[item_index]);
        }

        if(s_debug_stress_test.executed_count[item_index] < 0) {
            s_debug_stress_test.executed_count[item_index] = 0;
        }
        if(s_debug_stress_test.executed_count[item_index] > s_debug_stress_test.target_count[item_index]) {
            s_debug_stress_test.executed_count[item_index] = s_debug_stress_test.target_count[item_index];
        }
    }
}

static void ui_debug_stress_update_labels(void)
{
    char text[16];
    size_t item_index;

    if(!s_debug_stress_test.active) {
        return;
    }

    for(item_index = 0; item_index < UI_DEBUG_STRESS_ITEM_COUNT; item_index++) {
        if(s_debug_stress_test.target_count_label[item_index] != NULL) {
            (void)snprintf(text, sizeof(text), "%d", s_debug_stress_test.target_count[item_index]);
            lv_label_set_text(s_debug_stress_test.target_count_label[item_index], text);
        }

        if(s_debug_stress_test.interval_label[item_index] != NULL) {
            (void)snprintf(text, sizeof(text), "%d", s_debug_stress_test.interval_s[item_index]);
            lv_label_set_text(s_debug_stress_test.interval_label[item_index], text);
        }

        if(s_debug_stress_test.executed_count_label[item_index] != NULL) {
            (void)snprintf(text, sizeof(text), "%d", s_debug_stress_test.executed_count[item_index]);
            lv_label_set_text(s_debug_stress_test.executed_count_label[item_index], text);
        }
    }

    ui_debug_stress_update_action_buttons();
}

static bool ui_debug_stress_has_pending_work(void)
{
    return s_debug_stress_test.waiting_result || ui_debug_stress_has_active_runner();
}

static bool ui_debug_stress_find_dispatch_item(size_t *item_index_out)
{
    size_t item_index;

    if(item_index_out == NULL) {
        return false;
    }

    for(item_index = 0; item_index < UI_DEBUG_STRESS_ITEM_COUNT; item_index++) {
        if(!s_debug_stress_test.runner_active[item_index]) {
            continue;
        }
        if(s_debug_stress_test.executed_count[item_index] >= s_debug_stress_test.target_count[item_index]) {
            continue;
        }
        if((int32_t)(lv_tick_get() - s_debug_stress_test.next_dispatch_ms[item_index]) < 0) {
            continue;
        }

        *item_index_out = item_index;
        return true;
    }

    return false;
}

static bool ui_debug_stress_has_active_runner(void)
{
    size_t item_index;

    for(item_index = 0; item_index < UI_DEBUG_STRESS_ITEM_COUNT; item_index++) {
        if(s_debug_stress_test.runner_active[item_index]) {
            return true;
        }
    }

    return false;
}

static void ui_debug_stress_stop_runner(size_t item_index, bool force_flush)
{
    if(item_index >= UI_DEBUG_STRESS_ITEM_COUNT) {
        return;
    }

    s_debug_stress_test.runner_active[item_index] = false;

    if(force_flush) {
        (void)data_manager_flush();
    }

    ui_debug_stress_update_action_buttons();
}

static void ui_debug_stress_start_runner(size_t item_index)
{
    if(item_index >= UI_DEBUG_STRESS_ITEM_COUNT) {
        return;
    }

    if(s_debug_stress_test.runner_active[item_index]) {
        ui_debug_stress_stop_runner(item_index, true);
        return;
    }

    if(s_debug_stress_test.executed_count[item_index] >= s_debug_stress_test.target_count[item_index]) {
        s_debug_stress_test.executed_count[item_index] = 0;
        ui_debug_stress_persist_executed(item_index, true);
    }

    s_debug_stress_test.runner_active[item_index] = true;
    s_debug_stress_test.next_dispatch_ms[item_index] = lv_tick_get();
    ui_debug_stress_update_action_buttons();
}

static void ui_debug_stress_create_header(lv_obj_t *parent,
                                          const char *text,
                                          lv_coord_t x,
                                          lv_coord_t width)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, &ui_setting_font_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_MED), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, x, UI_DEBUG_STRESS_HEADER_Y + 2);
}

static lv_obj_t *ui_debug_stress_create_value_box(lv_obj_t *parent,
                                                  lv_coord_t x,
                                                  lv_coord_t width)
{
    lv_obj_t *box = lv_obj_create(parent);

    ui_debug_style_box(box, width, UI_DEBUG_STRESS_CTRL_H, 12);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(box, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(UI_C_PANEL), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(UI_C_LINE), 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_pos(box, x, (UI_DEBUG_STRESS_ROW_H - UI_DEBUG_STRESS_CTRL_H) / 2);
    return box;
}

static lv_obj_t *ui_debug_stress_create_value_label(lv_obj_t *parent,
                                                    lv_coord_t x,
                                                    lv_coord_t width)
{
    lv_obj_t *box = ui_debug_stress_create_value_box(parent, x, width);
    lv_obj_t *label = lv_label_create(box);

    lv_obj_set_style_text_font(label, &ui_setting_font_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_center(label);
    return label;
}

static void ui_debug_stress_create_adjust_button(lv_obj_t *parent,
                                                 lv_coord_t x,
                                                 const char *text,
                                                 size_t item_index,
                                                 ui_debug_stress_action_t action)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;

    ui_debug_style_button(button, UI_DEBUG_STRESS_SMALL_BTN_W, UI_DEBUG_STRESS_CTRL_H, 12);
    lv_obj_set_pos(button, x, (UI_DEBUG_STRESS_ROW_H - UI_DEBUG_STRESS_CTRL_H) / 2);
    lv_obj_add_event_cb(button,
                        ui_debug_stress_action_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)ui_debug_stress_encode_action(item_index, action));

    label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &egg_ui_font_16, 0);
    lv_obj_center(label);
}

static void ui_debug_stress_create_row(lv_obj_t *parent, size_t item_index, lv_coord_t y)
{
    lv_obj_t *row;
    lv_obj_t *label;
    lv_obj_t *button;

    row = lv_obj_create(parent);
    ui_debug_style_box(row, UI_DEBUG_STRESS_ROW_W, UI_DEBUG_STRESS_ROW_H, 14);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_10, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_C_PANEL), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(UI_C_LINE), 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_pos(row, UI_DEBUG_STRESS_ROW_X, y);

    label = lv_label_create(row);
    lv_label_set_text(label, k_stress_items[item_index].title);
    lv_obj_set_width(label, UI_DEBUG_STRESS_PROJECT_W);
    lv_obj_set_style_text_font(label, &ui_setting_font_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, 0, 6);

    ui_debug_stress_create_adjust_button(row,
                                         UI_DEBUG_STRESS_COUNT_X,
                                         "-",
                                         item_index,
                                         UI_DEBUG_STRESS_ACTION_COUNT_DEC);
    s_debug_stress_test.target_count_label[item_index] =
        ui_debug_stress_create_value_label(row,
                                           UI_DEBUG_STRESS_COUNT_X + UI_DEBUG_STRESS_SMALL_BTN_W + 2,
                                           UI_DEBUG_STRESS_COUNT_BOX_W);
    ui_debug_stress_create_adjust_button(row,
                                         UI_DEBUG_STRESS_COUNT_X + UI_DEBUG_STRESS_SMALL_BTN_W + 2 +
                                             UI_DEBUG_STRESS_COUNT_BOX_W + 2,
                                         "+",
                                         item_index,
                                         UI_DEBUG_STRESS_ACTION_COUNT_INC);

    ui_debug_stress_create_adjust_button(row,
                                         UI_DEBUG_STRESS_INTERVAL_X,
                                         "-",
                                         item_index,
                                         UI_DEBUG_STRESS_ACTION_INTERVAL_DEC);
    s_debug_stress_test.interval_label[item_index] =
        ui_debug_stress_create_value_label(row,
                                           UI_DEBUG_STRESS_INTERVAL_X + UI_DEBUG_STRESS_SMALL_BTN_W + 2,
                                           UI_DEBUG_STRESS_INTERVAL_BOX_W);
    ui_debug_stress_create_adjust_button(row,
                                         UI_DEBUG_STRESS_INTERVAL_X + UI_DEBUG_STRESS_SMALL_BTN_W + 2 +
                                             UI_DEBUG_STRESS_INTERVAL_BOX_W + 2,
                                         "+",
                                         item_index,
                                         UI_DEBUG_STRESS_ACTION_INTERVAL_INC);

    s_debug_stress_test.executed_count_label[item_index] =
        ui_debug_stress_create_value_label(row,
                                           UI_DEBUG_STRESS_EXECUTED_X,
                                           UI_DEBUG_STRESS_EXECUTED_W);

    button = lv_button_create(row);
    s_debug_stress_test.action_button[item_index] = button;
    ui_debug_style_button(button, UI_DEBUG_STRESS_ACTION_W, UI_DEBUG_STRESS_CTRL_H, 10);
    lv_obj_set_pos(button, UI_DEBUG_STRESS_ACTION_X, (UI_DEBUG_STRESS_ROW_H - UI_DEBUG_STRESS_CTRL_H) / 2);
    lv_obj_add_event_cb(button,
                        ui_debug_stress_action_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)ui_debug_stress_encode_action(item_index, UI_DEBUG_STRESS_ACTION_EXECUTE));
    label = lv_label_create(button);
    s_debug_stress_test.action_button_label[item_index] = label;
    lv_label_set_text(label, "执行");
    lv_obj_set_style_text_font(label, &ui_setting_font_12, 0);
    lv_obj_center(label);
}

static void ui_debug_stress_timer_cb(lv_timer_t *timer)
{
    ui_uart_sts_result_t sts_result;
    const ui_data_state_t *state;
    size_t item_index;
    esp_err_t err;

    LV_UNUSED(timer);

    if(!ui_debug_stress_has_pending_work()) {
        return;
    }

    state = ui_data_get_state(s_debug_stress_test.data);
    if(ui_data_state_has_alert(state)) {
        for(item_index = 0; item_index < UI_DEBUG_STRESS_ITEM_COUNT; item_index++) {
            ui_debug_stress_stop_runner(item_index, true);
        }
        return;
    }

    for(item_index = 0; item_index < UI_DEBUG_STRESS_ITEM_COUNT; item_index++) {
        if(s_debug_stress_test.runner_active[item_index] &&
           s_debug_stress_test.executed_count[item_index] >= s_debug_stress_test.target_count[item_index]) {
            ui_debug_stress_stop_runner(item_index, true);
        }
    }

    if(s_debug_stress_test.waiting_result) {
        ui_uart_bridge_get_sts_result(&sts_result);
        if(sts_result.seq == s_debug_stress_test.last_sts_seq) {
            return;
        }

        item_index = s_debug_stress_test.in_flight_item_index;
        s_debug_stress_test.last_sts_seq = sts_result.seq;
        s_debug_stress_test.waiting_result = false;

        if(item_index >= UI_DEBUG_STRESS_ITEM_COUNT) {
            ui_debug_stress_update_labels();
            return;
        }

        if(strcmp(sts_result.action, k_stress_items[item_index].action) != 0) {
            ESP_LOGW(TAG,
                     "stress ack mismatch expected=%s actual=%s",
                     k_stress_items[item_index].action,
                     sts_result.action);
            ui_debug_stress_update_labels();
            return;
        }

        if(!s_debug_stress_test.runner_active[item_index]) {
            ui_debug_stress_update_labels();
            return;
        }

        if(sts_result.state == UI_UART_STS_RESULT_SUCCESS) {
            if(s_debug_stress_test.executed_count[item_index] < s_debug_stress_test.target_count[item_index]) {
                s_debug_stress_test.executed_count[item_index] += 1;
                ui_debug_stress_persist_executed(item_index, false);
            }

            if(s_debug_stress_test.executed_count[item_index] >= s_debug_stress_test.target_count[item_index]) {
                ui_debug_stress_stop_runner(item_index, true);
            } else {
                s_debug_stress_test.next_dispatch_ms[item_index] =
                    lv_tick_get() + (uint32_t)(s_debug_stress_test.interval_s[item_index] * 1000);
            }
        } else if(sts_result.state == UI_UART_STS_RESULT_FAILED) {
            ESP_LOGW(TAG,
                     "stress item rejected item=%u, schedule next interval",
                     (unsigned)item_index);
            s_debug_stress_test.next_dispatch_ms[item_index] =
                lv_tick_get() + (uint32_t)(s_debug_stress_test.interval_s[item_index] * 1000);
        } else {
            ESP_LOGW(TAG,
                     "stress item stopped item=%u state=%d",
                     (unsigned)item_index,
                     (int)sts_result.state);
            ui_debug_stress_stop_runner(item_index, true);
        }

        ui_debug_stress_update_labels();
        return;
    }

    if(!ui_debug_stress_find_dispatch_item(&item_index)) {
        return;
    }

    err = ui_uart_bridge_send_sts_command(k_stress_items[item_index].action,
                                          k_stress_items[item_index].timeout_ms);
    if(err != ESP_OK) {
        ESP_LOGW(TAG,
                 "stress command send failed item=%u: %s",
                 (unsigned)item_index,
                 esp_err_to_name(err));
        ui_debug_stress_stop_runner(item_index, true);
        ui_debug_stress_update_labels();
        return;
    }

    s_debug_stress_test.waiting_result = true;
    s_debug_stress_test.in_flight_item_index = item_index;
}

static void ui_debug_stress_action_event_cb(lv_event_t *e)
{
    uintptr_t encoded = (uintptr_t)lv_event_get_user_data(e);
    size_t item_index = ui_debug_stress_decode_item(encoded);
    ui_debug_stress_action_t action = ui_debug_stress_decode_action(encoded);
    bool config_changed = false;
    bool executed_changed = false;

    if(item_index >= UI_DEBUG_STRESS_ITEM_COUNT) {
        return;
    }

    buzzer_beep_async(50);

    if(action != UI_DEBUG_STRESS_ACTION_EXECUTE && s_debug_stress_test.runner_active[item_index]) {
        return;
    }

    switch(action) {
    case UI_DEBUG_STRESS_ACTION_COUNT_DEC:
        s_debug_stress_test.target_count[item_index] = ui_debug_stress_clamp_count(
            s_debug_stress_test.target_count[item_index] - UI_DEBUG_STRESS_COUNT_STEP);
        if(s_debug_stress_test.executed_count[item_index] > s_debug_stress_test.target_count[item_index]) {
            s_debug_stress_test.executed_count[item_index] = s_debug_stress_test.target_count[item_index];
            executed_changed = true;
        }
        config_changed = true;
        break;
    case UI_DEBUG_STRESS_ACTION_COUNT_INC:
        s_debug_stress_test.target_count[item_index] = ui_debug_stress_clamp_count(
            s_debug_stress_test.target_count[item_index] + UI_DEBUG_STRESS_COUNT_STEP);
        config_changed = true;
        break;
    case UI_DEBUG_STRESS_ACTION_INTERVAL_DEC:
        s_debug_stress_test.interval_s[item_index] = ui_debug_stress_clamp_interval(
            s_debug_stress_test.interval_s[item_index] - UI_DEBUG_STRESS_INTERVAL_STEP);
        config_changed = true;
        break;
    case UI_DEBUG_STRESS_ACTION_INTERVAL_INC:
        s_debug_stress_test.interval_s[item_index] = ui_debug_stress_clamp_interval(
            s_debug_stress_test.interval_s[item_index] + UI_DEBUG_STRESS_INTERVAL_STEP);
        config_changed = true;
        break;
    case UI_DEBUG_STRESS_ACTION_EXECUTE:
        ui_debug_stress_start_runner(item_index);
        break;
    }

    if(config_changed) {
        ui_debug_stress_persist_config(item_index);
    }
    if(executed_changed) {
        ui_debug_stress_persist_executed(item_index, true);
    }

    ui_debug_stress_update_labels();
}

static void ui_debug_stress_test_back_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    s_debug_stress_test.active = false;
    ui_debug_stress_clear_view_refs();
    (void)data_manager_flush();
    ui_debug_create(s_debug_stress_test.data);
}

void ui_debug_stress_test_create(ui_data_t *data)
{
    const ui_data_state_t *state;
    bool saved_runner_active[UI_DEBUG_STRESS_ITEM_COUNT];
    uint32_t saved_next_dispatch_ms[UI_DEBUG_STRESS_ITEM_COUNT];
    bool saved_waiting_result;
    size_t saved_in_flight_item_index;
    uint32_t saved_last_sts_seq;
    lv_timer_t *saved_timer;
    lv_obj_t *page_root;
    lv_obj_t *screen;
    lv_obj_t *button;
    lv_obj_t *label;
    lv_obj_t *title;
    lv_coord_t row_y;
    size_t item_index;

    if(data == NULL) {
        return;
    }

    state = ui_data_get_state(data);
    if(ui_alert_should_show(state)) {
        ui_alert_create(data);
        return;
    }

    memcpy(saved_runner_active,
           s_debug_stress_test.runner_active,
           sizeof(saved_runner_active));
    saved_waiting_result = s_debug_stress_test.waiting_result;
    saved_in_flight_item_index = s_debug_stress_test.in_flight_item_index;
    memcpy(saved_next_dispatch_ms,
           s_debug_stress_test.next_dispatch_ms,
           sizeof(saved_next_dispatch_ms));
    saved_last_sts_seq = s_debug_stress_test.last_sts_seq;
    saved_timer = s_debug_stress_test.timer;

    lv_memzero(&s_debug_stress_test, sizeof(s_debug_stress_test));
    s_debug_stress_test.active = true;
    memcpy(s_debug_stress_test.runner_active,
           saved_runner_active,
           sizeof(s_debug_stress_test.runner_active));
    s_debug_stress_test.waiting_result = saved_waiting_result;
    s_debug_stress_test.in_flight_item_index = saved_in_flight_item_index;
    memcpy(s_debug_stress_test.next_dispatch_ms,
           saved_next_dispatch_ms,
           sizeof(s_debug_stress_test.next_dispatch_ms));
    s_debug_stress_test.last_sts_seq = saved_last_sts_seq;
    s_debug_stress_test.timer = saved_timer;
    s_debug_stress_test.data = data;

    ui_debug_stress_sync_from_data_manager();
    ui_debug_stress_init_defaults();

    if(s_debug_stress_test.timer == NULL) {
        s_debug_stress_test.timer = lv_timer_create(ui_debug_stress_timer_cb,
                                                    UI_DEBUG_STRESS_TIMER_MS,
                                                    NULL);
    }

    s_debug.active = false;
    s_debug.heater_debug_active = false;
    ui_debug_stop_serial_timer();

    screen = lv_screen_active();
    ui_screen_reset(screen);

    page_root = lv_obj_create(screen);
    ui_prepare_transparent_obj(page_root);
    lv_obj_set_size(page_root, lv_obj_get_width(screen), lv_obj_get_height(screen));
    lv_obj_set_pos(page_root, 0, 0);
    lv_obj_add_flag(page_root, LV_OBJ_FLAG_HIDDEN);

    button = lv_button_create(page_root);
    ui_debug_style_button(button, UI_DEBUG_BACK_BTN_W, UI_DEBUG_BACK_BTN_H, 13);
    lv_obj_align(button, LV_ALIGN_TOP_LEFT, UI_DEBUG_SIDE_MARGIN, UI_DEBUG_TOP_ROW_Y);
    lv_obj_add_event_cb(button, ui_debug_stress_test_back_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(button);
    lv_label_set_text(label, "BACK");
    lv_obj_set_style_text_font(label, &egg_ui_font_12, 0);
    lv_obj_set_style_text_letter_space(label, 1, 0);
    lv_obj_center(label);

    title = lv_label_create(page_root);
    lv_label_set_text(title, "压力测试");
    lv_obj_set_style_text_font(title, &ui_alert_debug_font_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, UI_DEBUG_TOP_ROW_Y + 5);

    ui_debug_stress_create_header(page_root, "项目", UI_DEBUG_STRESS_ROW_X, UI_DEBUG_STRESS_PROJECT_W);
    ui_debug_stress_create_header(page_root, "次数", UI_DEBUG_STRESS_ROW_X + UI_DEBUG_STRESS_COUNT_X, UI_DEBUG_STRESS_COUNT_W);
    ui_debug_stress_create_header(page_root, "间隔", UI_DEBUG_STRESS_ROW_X + UI_DEBUG_STRESS_INTERVAL_X, UI_DEBUG_STRESS_INTERVAL_W);
    ui_debug_stress_create_header(page_root, "已执行", UI_DEBUG_STRESS_ROW_X + UI_DEBUG_STRESS_EXECUTED_X, UI_DEBUG_STRESS_EXECUTED_W);
    ui_debug_stress_create_header(page_root, "按钮", UI_DEBUG_STRESS_ROW_X + UI_DEBUG_STRESS_ACTION_X, UI_DEBUG_STRESS_ACTION_W);

    row_y = UI_DEBUG_STRESS_LIST_Y;
    for(item_index = 0; item_index < UI_DEBUG_STRESS_ITEM_COUNT; item_index++) {
        ui_debug_stress_create_row(page_root, item_index, row_y);
        row_y += UI_DEBUG_STRESS_ROW_H + UI_DEBUG_STRESS_ROW_GAP;
    }

    ui_debug_stress_update_labels();
    lv_obj_clear_flag(page_root, LV_OBJ_FLAG_HIDDEN);
}

void ui_debug_stress_test_on_data_changed(void)
{
    const ui_data_state_t *state = ui_data_get_state(s_debug_stress_test.data);

    if(state == NULL) {
        return;
    }

    if(ui_alert_should_show(state)) {
        size_t item_index;

        for(item_index = 0; item_index < UI_DEBUG_STRESS_ITEM_COUNT; item_index++) {
            ui_debug_stress_stop_runner(item_index, true);
        }
        if(s_debug_stress_test.active) {
            s_debug_stress_test.active = false;
            ui_debug_stress_clear_view_refs();
            ui_alert_create(s_debug_stress_test.data);
        }
    }
}