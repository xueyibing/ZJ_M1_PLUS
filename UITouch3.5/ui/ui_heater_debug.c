#include "ui_heater_debug.h"

#include "buzzer.h"
#include "ui.h"
#include "ui_alert.h"
#include "ui_debug.h"
#include "ui_debug_internal.h"
#include "ui_uart_bridge.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_log.h"

#define UI_HEATER_DEBUG_ROW_X       6
#define UI_HEATER_DEBUG_ROW_W       308
#define UI_HEATER_DEBUG_ROW_H       24
#define UI_HEATER_DEBUG_ROW_GAP     4
#define UI_HEATER_DEBUG_HEAT_BTN_W  70
#define UI_HEATER_DEBUG_SMALL_BTN_W 26
#define UI_HEATER_DEBUG_DW_BOX_W    46
#define UI_HEATER_DEBUG_TIME_BOX_W  40
#define UI_HEATER_DEBUG_INFO_ROW_X  8
#define UI_HEATER_DEBUG_INFO_ROW_W  304
#define UI_HEATER_DEBUG_INFO_ROW_H  17
#define UI_HEATER_DEBUG_INFO_GAP    2
#define UI_HEATER_DEBUG_INFO_ADDR_W 50
#define UI_HEATER_DEBUG_INFO_AD_W   60
#define UI_HEATER_DEBUG_INFO_VALUE_W 68
#define UI_HEATER_DEBUG_INFO_IGBT_VALUE_W 110
#define UI_HEATER_DEBUG_INFO_PAIR_W 151
#define UI_HEATER_DEBUG_INFO_VOLTAGE_W 58
#define UI_HEATER_DEBUG_INFO_CURRENT_W 58
#define UI_HEATER_DEBUG_INFO_PPG_W 58
#define UI_HEATER_DEBUG_INFO_STATUS_W 72
#define UI_HEATER_DEBUG_HEAT_ROW_GAP 3
#define UI_HEATER_DEBUG_INFO_ROW_GAP 2
#define UI_HEATER_DEBUG_HEAT_DW_MIN      1
#define UI_HEATER_DEBUG_HEAT_DW_MAX      8
#define UI_HEATER_DEBUG_HEAT_DW_STEP     1
#define UI_HEATER_DEBUG_HEAT_DW_DEFAULT  6
#define UI_HEATER_DEBUG_HEAT_TIME_MIN    5
#define UI_HEATER_DEBUG_HEAT_TIME_MAX    120
#define UI_HEATER_DEBUG_HEAT_TIME_STEP   5
#define UI_HEATER_DEBUG_HEAT_TIME_DEFAULT 10
#define UI_HEATER_DEBUG_POLL_INTERVAL_MS 1000U
#define UI_HEATER_DEBUG_POLL_COMMAND "HET 2"

static const char *TAG = "ui_heater_debug";

typedef enum {
    UI_HEATER_DEBUG_ACTION_HEAT_A = 0,
    UI_HEATER_DEBUG_ACTION_HEAT_B,
    UI_HEATER_DEBUG_ACTION_A_DW_DEC,
    UI_HEATER_DEBUG_ACTION_A_DW_INC,
    UI_HEATER_DEBUG_ACTION_A_TIME_DEC,
    UI_HEATER_DEBUG_ACTION_A_TIME_INC,
    UI_HEATER_DEBUG_ACTION_B_DW_DEC,
    UI_HEATER_DEBUG_ACTION_B_DW_INC,
    UI_HEATER_DEBUG_ACTION_B_TIME_DEC,
    UI_HEATER_DEBUG_ACTION_B_TIME_INC,
} ui_heater_debug_action_t;

typedef enum {
    UI_HEATER_DEBUG_INFO_ADDR = 0,
    UI_HEATER_DEBUG_INFO_TEMP_AD,
    UI_HEATER_DEBUG_INFO_TEMP_VALUE,
    UI_HEATER_DEBUG_INFO_IGBT_AD,
    UI_HEATER_DEBUG_INFO_IGBT_TEMP_VALUE,
    UI_HEATER_DEBUG_INFO_VOLTAGE_AD,
    UI_HEATER_DEBUG_INFO_CURRENT_AD,
    UI_HEATER_DEBUG_INFO_PPG,
    UI_HEATER_DEBUG_INFO_STATUS,
} ui_heater_debug_info_t;

static int ui_heater_debug_clamp_int(int value, int min_value, int max_value)
{
    if(value < min_value) {
        return min_value;
    }
    if(value > max_value) {
        return max_value;
    }
    return value;
}

static void ui_heater_debug_prev_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    ui_debug_create(s_debug.data);
}

static void ui_heater_debug_init_heat_defaults(void)
{
    if(s_debug.heat_a_dw < UI_HEATER_DEBUG_HEAT_DW_MIN ||
       s_debug.heat_a_dw > UI_HEATER_DEBUG_HEAT_DW_MAX) {
        s_debug.heat_a_dw = UI_HEATER_DEBUG_HEAT_DW_DEFAULT;
    }
    if(s_debug.heat_b_dw < UI_HEATER_DEBUG_HEAT_DW_MIN ||
       s_debug.heat_b_dw > UI_HEATER_DEBUG_HEAT_DW_MAX) {
        s_debug.heat_b_dw = UI_HEATER_DEBUG_HEAT_DW_DEFAULT;
    }
    if(s_debug.heat_a_time < UI_HEATER_DEBUG_HEAT_TIME_MIN ||
       s_debug.heat_a_time > UI_HEATER_DEBUG_HEAT_TIME_MAX) {
        s_debug.heat_a_time = UI_HEATER_DEBUG_HEAT_TIME_DEFAULT;
    }
    if(s_debug.heat_b_time < UI_HEATER_DEBUG_HEAT_TIME_MIN ||
       s_debug.heat_b_time > UI_HEATER_DEBUG_HEAT_TIME_MAX) {
        s_debug.heat_b_time = UI_HEATER_DEBUG_HEAT_TIME_DEFAULT;
    }
}

static void ui_heater_debug_update_heat_value_labels(void)
{
    char text[12];

    if(s_debug.heat_a_dw_label != NULL) {
        (void)snprintf(text, sizeof(text), "%d档", s_debug.heat_a_dw);
        lv_label_set_text(s_debug.heat_a_dw_label, text);
    }
    if(s_debug.heat_a_time_label != NULL) {
        (void)snprintf(text, sizeof(text), "%ds", s_debug.heat_a_time);
        lv_label_set_text(s_debug.heat_a_time_label, text);
    }
    if(s_debug.heat_b_dw_label != NULL) {
        (void)snprintf(text, sizeof(text), "%d档", s_debug.heat_b_dw);
        lv_label_set_text(s_debug.heat_b_dw_label, text);
    }
    if(s_debug.heat_b_time_label != NULL) {
        (void)snprintf(text, sizeof(text), "%ds", s_debug.heat_b_time);
        lv_label_set_text(s_debug.heat_b_time_label, text);
    }
}

static esp_err_t ui_heater_debug_send_heat_command(bool pot_a)
{
    char command[32];
    int dw = pot_a ? s_debug.heat_a_dw : s_debug.heat_b_dw;
    int seconds = pot_a ? s_debug.heat_a_time : s_debug.heat_b_time;

    (void)snprintf(command, sizeof(command), "TST DW%c %d %d", pot_a ? 'A' : 'B', dw, seconds);
    return ui_uart_bridge_send_raw_command(command, false);
}

static void ui_heater_debug_action_event_cb(lv_event_t *e)
{
    ui_heater_debug_action_t action = (ui_heater_debug_action_t)(uintptr_t)lv_event_get_user_data(e);
    esp_err_t err = ESP_OK;

    if(lv_event_get_code(e) != LV_EVENT_LONG_PRESSED_REPEAT) {
        buzzer_beep_async(50);
    }

    ui_heater_debug_init_heat_defaults();

    switch(action) {
    case UI_HEATER_DEBUG_ACTION_HEAT_A:
        err = ui_heater_debug_send_heat_command(true);
        break;
    case UI_HEATER_DEBUG_ACTION_HEAT_B:
        err = ui_heater_debug_send_heat_command(false);
        break;
    case UI_HEATER_DEBUG_ACTION_A_DW_DEC:
        s_debug.heat_a_dw = ui_heater_debug_clamp_int(s_debug.heat_a_dw - UI_HEATER_DEBUG_HEAT_DW_STEP,
                                                    UI_HEATER_DEBUG_HEAT_DW_MIN,
                                                    UI_HEATER_DEBUG_HEAT_DW_MAX);
        break;
    case UI_HEATER_DEBUG_ACTION_A_DW_INC:
        s_debug.heat_a_dw = ui_heater_debug_clamp_int(s_debug.heat_a_dw + UI_HEATER_DEBUG_HEAT_DW_STEP,
                                                    UI_HEATER_DEBUG_HEAT_DW_MIN,
                                                    UI_HEATER_DEBUG_HEAT_DW_MAX);
        break;
    case UI_HEATER_DEBUG_ACTION_A_TIME_DEC:
        s_debug.heat_a_time = ui_heater_debug_clamp_int(s_debug.heat_a_time - UI_HEATER_DEBUG_HEAT_TIME_STEP,
                                                      UI_HEATER_DEBUG_HEAT_TIME_MIN,
                                                      UI_HEATER_DEBUG_HEAT_TIME_MAX);
        break;
    case UI_HEATER_DEBUG_ACTION_A_TIME_INC:
        s_debug.heat_a_time = ui_heater_debug_clamp_int(s_debug.heat_a_time + UI_HEATER_DEBUG_HEAT_TIME_STEP,
                                                      UI_HEATER_DEBUG_HEAT_TIME_MIN,
                                                      UI_HEATER_DEBUG_HEAT_TIME_MAX);
        break;
    case UI_HEATER_DEBUG_ACTION_B_DW_DEC:
        s_debug.heat_b_dw = ui_heater_debug_clamp_int(s_debug.heat_b_dw - UI_HEATER_DEBUG_HEAT_DW_STEP,
                                                    UI_HEATER_DEBUG_HEAT_DW_MIN,
                                                    UI_HEATER_DEBUG_HEAT_DW_MAX);
        break;
    case UI_HEATER_DEBUG_ACTION_B_DW_INC:
        s_debug.heat_b_dw = ui_heater_debug_clamp_int(s_debug.heat_b_dw + UI_HEATER_DEBUG_HEAT_DW_STEP,
                                                    UI_HEATER_DEBUG_HEAT_DW_MIN,
                                                    UI_HEATER_DEBUG_HEAT_DW_MAX);
        break;
    case UI_HEATER_DEBUG_ACTION_B_TIME_DEC:
        s_debug.heat_b_time = ui_heater_debug_clamp_int(s_debug.heat_b_time - UI_HEATER_DEBUG_HEAT_TIME_STEP,
                                                      UI_HEATER_DEBUG_HEAT_TIME_MIN,
                                                      UI_HEATER_DEBUG_HEAT_TIME_MAX);
        break;
    case UI_HEATER_DEBUG_ACTION_B_TIME_INC:
        s_debug.heat_b_time = ui_heater_debug_clamp_int(s_debug.heat_b_time + UI_HEATER_DEBUG_HEAT_TIME_STEP,
                                                      UI_HEATER_DEBUG_HEAT_TIME_MIN,
                                                      UI_HEATER_DEBUG_HEAT_TIME_MAX);
        break;
    }

    ui_heater_debug_update_heat_value_labels();

    if(err != ESP_OK) {
        ESP_LOGW(TAG, "debug more command queue failed: %s", esp_err_to_name(err));
    }
}

static bool ui_heater_debug_action_is_adjust(ui_heater_debug_action_t action)
{
    switch(action) {
    case UI_HEATER_DEBUG_ACTION_A_DW_DEC:
    case UI_HEATER_DEBUG_ACTION_A_DW_INC:
    case UI_HEATER_DEBUG_ACTION_A_TIME_DEC:
    case UI_HEATER_DEBUG_ACTION_A_TIME_INC:
    case UI_HEATER_DEBUG_ACTION_B_DW_DEC:
    case UI_HEATER_DEBUG_ACTION_B_DW_INC:
    case UI_HEATER_DEBUG_ACTION_B_TIME_DEC:
    case UI_HEATER_DEBUG_ACTION_B_TIME_INC:
        return true;
    case UI_HEATER_DEBUG_ACTION_HEAT_A:
    case UI_HEATER_DEBUG_ACTION_HEAT_B:
        break;
    }

    return false;
}

static lv_obj_t **ui_heater_debug_info_label_slot(bool pot_a, ui_heater_debug_info_t info)
{
    switch(info) {
    case UI_HEATER_DEBUG_INFO_ADDR:
        return pot_a ? &s_debug.heat_a_addr_label : &s_debug.heat_b_addr_label;
    case UI_HEATER_DEBUG_INFO_TEMP_AD:
        return pot_a ? &s_debug.heat_a_temp_ad_label : &s_debug.heat_b_temp_ad_label;
    case UI_HEATER_DEBUG_INFO_TEMP_VALUE:
        return pot_a ? &s_debug.heat_a_temp_value_label : &s_debug.heat_b_temp_value_label;
    case UI_HEATER_DEBUG_INFO_IGBT_AD:
        return pot_a ? &s_debug.heat_a_igbt_ad_label : &s_debug.heat_b_igbt_ad_label;
    case UI_HEATER_DEBUG_INFO_IGBT_TEMP_VALUE:
        return pot_a ? &s_debug.heat_a_igbt_temp_value_label : &s_debug.heat_b_igbt_temp_value_label;
    case UI_HEATER_DEBUG_INFO_VOLTAGE_AD:
        return pot_a ? &s_debug.heat_a_voltage_ad_label : &s_debug.heat_b_voltage_ad_label;
    case UI_HEATER_DEBUG_INFO_CURRENT_AD:
        return pot_a ? &s_debug.heat_a_current_ad_label : &s_debug.heat_b_current_ad_label;
    case UI_HEATER_DEBUG_INFO_PPG:
        return pot_a ? &s_debug.heat_a_ppg_label : &s_debug.heat_b_ppg_label;
    case UI_HEATER_DEBUG_INFO_STATUS:
        return pot_a ? &s_debug.heat_a_status_label : &s_debug.heat_b_status_label;
    }

    return NULL;
}

static void ui_heater_debug_set_info_text(lv_obj_t *label, const char *title, int value, bool hex_value)
{
    char text[32];

    if(label == NULL || title == NULL) {
        return;
    }

    if(hex_value) {
        (void)snprintf(text, sizeof(text), "%s:%02X", title, value);
    } else {
        (void)snprintf(text, sizeof(text), "%s:%d", title, value);
    }
    lv_label_set_text(label, text);
}

static void ui_heater_debug_update_pot_info(bool pot_a,
                                          int addr,
                                          int temp_ad,
                                          int temp_value,
                                          int igbt_ad,
                                          int igbt_temp_value,
                                          int vol,
                                          int cur,
                                          int ppg,
                                          int state)
{
    ui_heater_debug_set_info_text(*ui_heater_debug_info_label_slot(pot_a, UI_HEATER_DEBUG_INFO_ADDR),
                                "地址", addr, true);
    ui_heater_debug_set_info_text(*ui_heater_debug_info_label_slot(pot_a, UI_HEATER_DEBUG_INFO_VOLTAGE_AD),
                                "VAD", vol, false);
    ui_heater_debug_set_info_text(*ui_heater_debug_info_label_slot(pot_a, UI_HEATER_DEBUG_INFO_CURRENT_AD),
                                "IAD", cur, false);
    ui_heater_debug_set_info_text(*ui_heater_debug_info_label_slot(pot_a, UI_HEATER_DEBUG_INFO_PPG),
                                "PPG", ppg, false);
    ui_heater_debug_set_info_text(*ui_heater_debug_info_label_slot(pot_a, UI_HEATER_DEBUG_INFO_STATUS),
                                "状态", state, true);
    ui_heater_debug_set_info_text(*ui_heater_debug_info_label_slot(pot_a, UI_HEATER_DEBUG_INFO_TEMP_AD),
                                "温AD", temp_ad, false);
    ui_heater_debug_set_info_text(*ui_heater_debug_info_label_slot(pot_a, UI_HEATER_DEBUG_INFO_TEMP_VALUE),
                                "温值", temp_value, false);
    ui_heater_debug_set_info_text(*ui_heater_debug_info_label_slot(pot_a, UI_HEATER_DEBUG_INFO_IGBT_AD),
                                "IGBTAD", igbt_ad, false);
    ui_heater_debug_set_info_text(*ui_heater_debug_info_label_slot(pot_a, UI_HEATER_DEBUG_INFO_IGBT_TEMP_VALUE),
                                "IGBT温", igbt_temp_value, false);
}

bool ui_heater_debug_handle_rx_line(const char *rx)
{
    int a_addr, a_temp_ad, a_temp, a_igbt_ad, a_igbt_temp, a_vol, a_cur, a_ppg, a_state;
    int b_addr, b_temp_ad, b_temp, b_igbt_ad, b_igbt_temp, b_vol, b_cur, b_ppg, b_state;
    int parsed_count;

    if(!s_debug.heater_debug_active || rx == NULL) {
        return false;
    }

    parsed_count = sscanf(rx,
                          "PDBG %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
                          &a_addr, &a_temp_ad, &a_temp, &a_igbt_ad, &a_igbt_temp,
                          &a_vol, &a_cur, &a_ppg, &a_state,
                          &b_addr, &b_temp_ad, &b_temp, &b_igbt_ad, &b_igbt_temp,
                          &b_vol, &b_cur, &b_ppg, &b_state);
    if(parsed_count != 18) {
        return false;
    }

    ui_heater_debug_update_pot_info(true,
                                  a_addr, a_temp_ad, a_temp, a_igbt_ad, a_igbt_temp,
                                  a_vol, a_cur, a_ppg, a_state);
    ui_heater_debug_update_pot_info(false,
                                  b_addr, b_temp_ad, b_temp, b_igbt_ad, b_igbt_temp,
                                  b_vol, b_cur, b_ppg, b_state);
    return true;
}

void ui_heater_debug_poll_if_needed(void)
{
    uint32_t now;

    if(!s_debug.heater_debug_active) {
        return;
    }

    now = lv_tick_get();
    if(s_debug.heater_debug_last_poll_ms != 0U &&
       lv_tick_elaps(s_debug.heater_debug_last_poll_ms) < UI_HEATER_DEBUG_POLL_INTERVAL_MS) {
        return;
    }

    if(ui_uart_bridge_send_raw_command(UI_HEATER_DEBUG_POLL_COMMAND, false) == ESP_OK) {
        s_debug.heater_debug_last_poll_ms = now;
    }
}

static lv_obj_t *ui_heater_debug_create_action_button(lv_obj_t *parent,
                                                    const char *text,
                                                    ui_heater_debug_action_t action,
                                                    lv_coord_t width,
                                                    const lv_font_t *font)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;

    if(font == NULL) {
        font = &egg_ui_font_12;
    }

    ui_debug_style_button(button, width, UI_HEATER_DEBUG_ROW_H, 12);
    lv_obj_add_event_cb(button, ui_heater_debug_action_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)action);
    if(ui_heater_debug_action_is_adjust(action)) {
        lv_obj_add_event_cb(button, ui_heater_debug_action_event_cb, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)action);
        lv_obj_add_event_cb(button, ui_heater_debug_action_event_cb, LV_EVENT_LONG_PRESSED_REPEAT,
                            (void *)(uintptr_t)action);
    }

    label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);

    return button;
}

static lv_obj_t *ui_heater_debug_create_value_box(lv_obj_t *parent, lv_coord_t width, lv_obj_t **out_label)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_t *label;

    ui_debug_style_box(box, width, UI_HEATER_DEBUG_ROW_H, 12);

    label = lv_label_create(box);
    lv_label_set_text(label, "-");
    lv_obj_set_style_text_font(label, &egg_ui_font_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);

    if(out_label != NULL) {
        *out_label = label;
    }

    return box;
}

static lv_obj_t *ui_heater_debug_create_info_cell(lv_obj_t *parent,
                                                const char *title,
                                                lv_coord_t width,
                                                lv_obj_t **out_label)
{
    lv_obj_t *label;
    char text[32];

    label = lv_label_create(parent);
    (void)snprintf(text, sizeof(text), "%s:-", title);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, text);
    lv_obj_set_size(label, width, UI_HEATER_DEBUG_INFO_ROW_H);
    lv_obj_set_style_text_font(label, &egg_ui_font_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    if(out_label != NULL) {
        *out_label = label;
    }

    return label;
}

static lv_obj_t *ui_heater_debug_create_row(lv_obj_t *parent, lv_coord_t y, lv_flex_align_t main_align)
{
    lv_obj_t *row = lv_obj_create(parent);

    ui_prepare_transparent_obj(row);
    lv_obj_set_size(row, UI_HEATER_DEBUG_ROW_W, UI_HEATER_DEBUG_ROW_H);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_gap(row, UI_HEATER_DEBUG_ROW_GAP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, main_align, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(row, LV_ALIGN_TOP_LEFT, UI_HEATER_DEBUG_ROW_X, y);

    return row;
}

static lv_obj_t *ui_heater_debug_create_info_row(lv_obj_t *parent, lv_coord_t y)
{
    lv_obj_t *row = lv_obj_create(parent);

    ui_prepare_transparent_obj(row);
    lv_obj_set_size(row, UI_HEATER_DEBUG_INFO_ROW_W, UI_HEATER_DEBUG_INFO_ROW_H);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_gap(row, UI_HEATER_DEBUG_INFO_GAP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(row, LV_ALIGN_TOP_LEFT, UI_HEATER_DEBUG_INFO_ROW_X, y);

    return row;
}

static void ui_heater_debug_create_info_boxes(lv_obj_t *parent, lv_coord_t y, bool pot_a)
{
    lv_obj_t *state_row = ui_heater_debug_create_info_row(parent, y);
    lv_obj_t *temp_row = ui_heater_debug_create_info_row(parent, y + UI_HEATER_DEBUG_INFO_ROW_H + UI_HEATER_DEBUG_INFO_ROW_GAP);
    lv_obj_t *igbt_row = ui_heater_debug_create_info_row(parent, y + (UI_HEATER_DEBUG_INFO_ROW_H + UI_HEATER_DEBUG_INFO_ROW_GAP) * 2);

    ui_heater_debug_create_info_cell(state_row,
                                   "地址",
                                   UI_HEATER_DEBUG_INFO_ADDR_W,
                                   ui_heater_debug_info_label_slot(pot_a, UI_HEATER_DEBUG_INFO_ADDR));
    ui_heater_debug_create_info_cell(state_row,
                                   "VAD",
                                   UI_HEATER_DEBUG_INFO_VOLTAGE_W,
                                   ui_heater_debug_info_label_slot(pot_a, UI_HEATER_DEBUG_INFO_VOLTAGE_AD));
    ui_heater_debug_create_info_cell(state_row,
                                   "IAD",
                                   UI_HEATER_DEBUG_INFO_CURRENT_W,
                                   ui_heater_debug_info_label_slot(pot_a, UI_HEATER_DEBUG_INFO_CURRENT_AD));
    ui_heater_debug_create_info_cell(state_row,
                                   "PPG",
                                   UI_HEATER_DEBUG_INFO_PPG_W,
                                   ui_heater_debug_info_label_slot(pot_a, UI_HEATER_DEBUG_INFO_PPG));
    ui_heater_debug_create_info_cell(state_row,
                                   "状态",
                                   UI_HEATER_DEBUG_INFO_STATUS_W,
                                   ui_heater_debug_info_label_slot(pot_a, UI_HEATER_DEBUG_INFO_STATUS));

    ui_heater_debug_create_info_cell(temp_row,
                                   "温AD",
                                   UI_HEATER_DEBUG_INFO_PAIR_W,
                                   ui_heater_debug_info_label_slot(pot_a, UI_HEATER_DEBUG_INFO_TEMP_AD));
    ui_heater_debug_create_info_cell(temp_row,
                                   "温值",
                                   UI_HEATER_DEBUG_INFO_PAIR_W,
                                   ui_heater_debug_info_label_slot(pot_a, UI_HEATER_DEBUG_INFO_TEMP_VALUE));
    ui_heater_debug_create_info_cell(igbt_row,
                                   "IGBTAD",
                                   UI_HEATER_DEBUG_INFO_PAIR_W,
                                   ui_heater_debug_info_label_slot(pot_a, UI_HEATER_DEBUG_INFO_IGBT_AD));
    ui_heater_debug_create_info_cell(igbt_row,
                                   "IGBT温",
                                   UI_HEATER_DEBUG_INFO_PAIR_W,
                                   ui_heater_debug_info_label_slot(pot_a, UI_HEATER_DEBUG_INFO_IGBT_TEMP_VALUE));
}

static void ui_heater_debug_create_heat_group(lv_obj_t *parent, lv_coord_t y, bool pot_a)
{
    lv_obj_t *control_row = ui_heater_debug_create_row(parent, y, LV_FLEX_ALIGN_CENTER);
    lv_coord_t info_y = y + UI_HEATER_DEBUG_ROW_H + UI_HEATER_DEBUG_HEAT_ROW_GAP;
    lv_obj_t **dw_label = pot_a ? &s_debug.heat_a_dw_label : &s_debug.heat_b_dw_label;
    lv_obj_t **time_label = pot_a ? &s_debug.heat_a_time_label : &s_debug.heat_b_time_label;

    ui_heater_debug_create_action_button(control_row,
                                       pot_a ? "加热A锅" : "加热B锅",
                                       pot_a ? UI_HEATER_DEBUG_ACTION_HEAT_A : UI_HEATER_DEBUG_ACTION_HEAT_B,
                                       UI_HEATER_DEBUG_HEAT_BTN_W,
                                       &egg_ui_font_12);
    ui_heater_debug_create_action_button(control_row,
                                       "-",
                                       pot_a ? UI_HEATER_DEBUG_ACTION_A_DW_DEC : UI_HEATER_DEBUG_ACTION_B_DW_DEC,
                                       UI_HEATER_DEBUG_SMALL_BTN_W,
                                       &egg_ui_font_16);
    ui_heater_debug_create_value_box(control_row, UI_HEATER_DEBUG_DW_BOX_W, dw_label);
    ui_heater_debug_create_action_button(control_row,
                                       "+",
                                       pot_a ? UI_HEATER_DEBUG_ACTION_A_DW_INC : UI_HEATER_DEBUG_ACTION_B_DW_INC,
                                       UI_HEATER_DEBUG_SMALL_BTN_W,
                                       &egg_ui_font_16);

    ui_heater_debug_create_action_button(control_row,
                                       "-",
                                       pot_a ? UI_HEATER_DEBUG_ACTION_A_TIME_DEC : UI_HEATER_DEBUG_ACTION_B_TIME_DEC,
                                       UI_HEATER_DEBUG_SMALL_BTN_W,
                                       &egg_ui_font_16);
    ui_heater_debug_create_value_box(control_row, UI_HEATER_DEBUG_TIME_BOX_W, time_label);
    ui_heater_debug_create_action_button(control_row,
                                       "+",
                                       pot_a ? UI_HEATER_DEBUG_ACTION_A_TIME_INC : UI_HEATER_DEBUG_ACTION_B_TIME_INC,
                                       UI_HEATER_DEBUG_SMALL_BTN_W,
                                       &egg_ui_font_16);

    ui_heater_debug_create_info_boxes(parent, info_y, pot_a);
}

void ui_heater_debug_create(ui_data_t *data)
{
    lv_obj_t *screen;
    lv_obj_t *button;
    lv_obj_t *label;
    lv_obj_t *tx_box;
    lv_obj_t *rx_box;
    const ui_data_state_t *state;

    if(data == NULL) {
        return;
    }

    state = ui_data_get_state(data);
    if(ui_alert_should_show(state)) {
        ui_alert_create(data);
        return;
    }

    ui_debug_stop_serial_timer();
    s_debug.active = true;
    s_debug.data = data;
    s_debug.env_temp_value_label = NULL;
    s_debug.tray_distance_value_label = NULL;
    s_debug.pan_a_distance_value_label = NULL;
    s_debug.push_state_label = NULL;
    s_debug.position_state_label = NULL;
    s_debug.oil_state_label = NULL;
    s_debug.heat_a_dw_label = NULL;
    s_debug.heat_b_dw_label = NULL;
    s_debug.heat_a_addr_label = NULL;
    s_debug.heat_a_temp_ad_label = NULL;
    s_debug.heat_a_temp_value_label = NULL;
    s_debug.heat_a_igbt_ad_label = NULL;
    s_debug.heat_a_igbt_temp_value_label = NULL;
    s_debug.heat_a_voltage_ad_label = NULL;
    s_debug.heat_a_current_ad_label = NULL;
    s_debug.heat_a_ppg_label = NULL;
    s_debug.heat_a_status_label = NULL;
    s_debug.heat_b_addr_label = NULL;
    s_debug.heat_b_temp_ad_label = NULL;
    s_debug.heat_b_temp_value_label = NULL;
    s_debug.heat_b_igbt_ad_label = NULL;
    s_debug.heat_b_igbt_temp_value_label = NULL;
    s_debug.heat_b_voltage_ad_label = NULL;
    s_debug.heat_b_current_ad_label = NULL;
    s_debug.heat_b_ppg_label = NULL;
    s_debug.heat_b_status_label = NULL;
    s_debug.pending_distance_target = UI_DEBUG_DISTANCE_TARGET_NONE;
    s_debug.pending_distance_started_ms = 0U;
    s_debug.pending_push_status_started_ms = 0U;
    s_debug.pending_position_status_started_ms = 0U;
    s_debug.pending_oil_status_started_ms = 0U;
    s_debug.heater_debug_last_poll_ms = 0U;
    s_debug.heater_debug_active = true;
    ui_heater_debug_init_heat_defaults();

    screen = lv_screen_active();
    ui_screen_reset(screen);

    button = lv_button_create(screen);
    ui_debug_style_button(button, UI_DEBUG_BACK_BTN_W, UI_DEBUG_BACK_BTN_H, 13);
    lv_obj_align(button, LV_ALIGN_TOP_LEFT, UI_DEBUG_SIDE_MARGIN, UI_DEBUG_TOP_ROW_Y);
    lv_obj_add_event_cb(button, ui_heater_debug_prev_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(button);
    lv_label_set_text(label, "上一页");
    lv_obj_set_style_text_font(label, &egg_ui_font_12, 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    lv_obj_center(label);

    tx_box = ui_debug_create_uart_box(screen, "TX", &s_debug.tx_value_label);
    lv_obj_align_to(tx_box, button, LV_ALIGN_OUT_RIGHT_MID, UI_DEBUG_TOP_ROW_GAP, 0);

    rx_box = ui_debug_create_uart_box(screen, "RX", &s_debug.rx_value_label);
    lv_obj_align_to(rx_box, tx_box, LV_ALIGN_OUT_RIGHT_MID, UI_DEBUG_TOP_ROW_GAP, 0);

    ui_heater_debug_create_heat_group(screen, 50, true);
    ui_heater_debug_create_heat_group(screen, 136, false);
    ui_heater_debug_update_heat_value_labels();

    ui_debug_start_serial_timer();
}
