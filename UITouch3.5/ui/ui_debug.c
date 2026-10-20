#include "ui_debug.h"
#include "ui_alert.h"
#include "ui_bypass_switch.h"
#include "ui_common.h"
#include "ui_debug_internal.h"
#include "ui_alert_debug.h"
#include "ui_heater_debug.h"
#include "ui_debug_stress_test.h"
#include "ui_error_log.h"
#include "ui_motor_debug.h"
#include "ui_move_debug.h"
#include "ui_sensor_debug.h"
#include "ui_notice.h"
#include "ui.h"
#include "buzzer.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "ui_uart_bridge.h"

#define UI_DEBUG_ACTION_BTN_W    88
#define UI_DEBUG_MEASURE_BTN_W   106
#define UI_DEBUG_ACTION_BTN_H    20
#define UI_DEBUG_ACTION_GAP      4
#define UI_DEBUG_ACTION_RIGHT_BTN_H UI_DEBUG_ACTION_BTN_H
#define UI_DEBUG_ACTION_RIGHT_GAP   UI_DEBUG_ACTION_GAP
#define UI_DEBUG_ACTION_COL_GAP  8
#define UI_DEBUG_ACTION_LEFT_COUNT  2
#define UI_DEBUG_ACTION_RIGHT_COUNT 7
#define UI_DEBUG_ACTION_LIST_H   (UI_DEBUG_ACTION_BTN_H * UI_DEBUG_ACTION_LEFT_COUNT + UI_DEBUG_ACTION_GAP * (UI_DEBUG_ACTION_LEFT_COUNT - 1))
#define UI_DEBUG_ACTION_RIGHT_H  (UI_DEBUG_ACTION_RIGHT_BTN_H * UI_DEBUG_ACTION_RIGHT_COUNT + UI_DEBUG_ACTION_RIGHT_GAP * (UI_DEBUG_ACTION_RIGHT_COUNT - 1))
#define UI_DEBUG_MEASURE_LIST_H  (UI_DEBUG_ACTION_BTN_H * 7 + UI_DEBUG_ACTION_GAP * 6)
/* 88 + 88 + 106 + 8 + 8 = 298, centered on a 320px-wide screen. */
#define UI_DEBUG_ACTION_LEFT_X   11
#define UI_DEBUG_ACTION_TOP_Y    42
#define UI_DEBUG_NEXT_BTN_W      88
#define UI_DEBUG_NEXT_BTN_H      22
#define UI_DEBUG_NAV_BTN_W       98
#define UI_DEBUG_NAV_BTN_H       24
#define UI_DEBUG_NAV_BTN_Y       (-6)
#define UI_DEBUG_NAV_SIDE_OFFSET 104
#define UI_DEBUG_NAV_ACCENT      UI_C_ACCENT
#define UI_DEBUG_HUB_BTN_W       120
#define UI_DEBUG_HUB_BTN_H       34
#define UI_DEBUG_HUB_GAP         16
#define UI_DEBUG_HUB_ROW_Y       88
#define UI_DEBUG_TITLE_Y         36
#define UI_DEBUG_GRID_BTN_W      132
#define UI_DEBUG_GRID_BTN_H      26
#define UI_DEBUG_GRID_COL_GAP    14
#define UI_DEBUG_GRID_ROW_GAP    5
#define UI_DEBUG_GRID_TOP_Y      60
#define UI_DEBUG_GRID_COL_DX     (UI_DEBUG_GRID_BTN_W / 2 + UI_DEBUG_GRID_COL_GAP / 2)
#define UI_SENSOR_BTN_W          128
#define UI_SENSOR_BTN_H          30
#define UI_SENSOR_COL_GAP        16
#define UI_SENSOR_ROW_GAP        16
#define UI_SENSOR_TITLE_Y        36
#define UI_SENSOR_LIST_TOP_Y     84
#define UI_SENSOR_LIST_H         (UI_SENSOR_BTN_H * 3 + UI_SENSOR_ROW_GAP * 2)
#define UI_SENSOR_LEFT_X         ((320 - (UI_SENSOR_BTN_W * 2 + UI_SENSOR_COL_GAP)) / 2)
#define UI_DEBUG_DISTANCE_TIMEOUT_MS 2500U
#define UI_DEBUG_STATUS_TIMEOUT_MS 1500U
#define UI_DEBUG_DISTANCE_COMMAND "TST DIST"
#define UI_DEBUG_PAN_A_DISTANCE_COMMAND "TST DISTA"
#define UI_DEBUG_ENV_TEMP_COMMAND "TST ENV_TMP"
#define UI_DEBUG_PUSH_STATE_COMMAND "TST PUBS"
#define UI_DEBUG_POSITION_STATE_COMMAND "TST TRANSS"
#define UI_DEBUG_OIL_STATE_COMMAND "TST OILS"
#define UI_DEBUG_ENV_TEMP_TITLE "环境温度"
#define UI_DEBUG_PAN_A_DISTANCE_TITLE "锅A测距"
#define UI_DEBUG_TRAY_DISTANCE_TITLE "蛋托测距"
#define UI_DEBUG_PUSH_STATE_TITLE "推杆状态"
#define UI_DEBUG_POSITION_STATE_TITLE "位置状态"
#define UI_DEBUG_OIL_STATE_TITLE "油位状态"

static const char *TAG = "ui_debug";

typedef enum {
    UI_DEBUG_ACTION_RESET = 0,
    UI_DEBUG_ACTION_HOME,
    UI_DEBUG_ACTION_GIVE_EGG,
    UI_DEBUG_ACTION_GIVE_OIL,
    UI_DEBUG_ACTION_BACK_OIL,
    UI_DEBUG_ACTION_CANCEL_NOTICE_PREVIEW,
    UI_DEBUG_ACTION_BEAT_EGG,
    UI_DEBUG_ACTION_FAN_TOGGLE,
    UI_DEBUG_ACTION_FLIP_A,
    UI_DEBUG_ACTION_FLIP_B,
    UI_DEBUG_ACTION_BEAT_PUSH,
    UI_DEBUG_ACTION_BEAT_BACK,
    UI_DEBUG_ACTION_HEAT_A_10S,
    UI_DEBUG_ACTION_HEAT_B_10S,
    UI_DEBUG_ACTION_TRANS_DUMP,
    UI_DEBUG_ACTION_TRANS_DUMP_SECOND,
    UI_DEBUG_ACTION_ENV_TEMP,
    UI_DEBUG_ACTION_PUSH_STATE,
    UI_DEBUG_ACTION_POSITION_STATE,
    UI_DEBUG_ACTION_TRAY_DISTANCE,
    UI_DEBUG_ACTION_PAN_A_DISTANCE,
    UI_DEBUG_ACTION_OIL_STATE,
} ui_debug_action_t;

ui_debug_view_t s_debug;

static const ui_data_state_t *ui_debug_get_state(void)
{
    if(s_debug.data == NULL) {
        return NULL;
    }

    return ui_data_get_state(s_debug.data);
}


void ui_debug_style_button(lv_obj_t *button, lv_coord_t width, lv_coord_t height, lv_coord_t radius)
{
    ui_reset_base_obj(button);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, radius, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_set_style_border_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(button, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_C_TEXT_STRONG), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(button, lv_color_hex(UI_C_BG), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 10, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_color(button, lv_color_hex(UI_C_TEXT_STRONG), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_opa(button, LV_OPA_20, LV_STATE_PRESSED);
}

void ui_debug_style_box(lv_obj_t *box, lv_coord_t width, lv_coord_t height, lv_coord_t radius)
{
    ui_reset_base_obj(box);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(box, width, height);
    lv_obj_set_style_radius(box, radius, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_set_style_border_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
}

void ui_debug_stop_serial_timer(void)
{
    if(s_debug.serial_timer != NULL) {
        lv_timer_delete(s_debug.serial_timer);
        s_debug.serial_timer = NULL;
    }
}

lv_obj_t *ui_debug_create_uart_box(lv_obj_t *parent, const char *tag_text, lv_obj_t **out_value_label)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_t *tag_label;
    lv_obj_t *value_label;

    ui_debug_style_box(box, UI_DEBUG_UART_BOX_W, UI_DEBUG_UART_BOX_H, 4);
    lv_obj_set_style_pad_left(box, 8, 0);
    lv_obj_set_style_pad_right(box, 8, 0);
    lv_obj_set_style_pad_top(box, 0, 0);
    lv_obj_set_style_pad_bottom(box, 0, 0);

    tag_label = lv_label_create(box);
    lv_label_set_text(tag_label, tag_text);
    lv_obj_set_style_text_font(tag_label, &egg_ui_font_12, 0);
    lv_obj_set_style_text_color(tag_label, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_set_style_text_letter_space(tag_label, 1, 0);
    lv_obj_align(tag_label, LV_ALIGN_LEFT_MID, 0, 0);

    value_label = lv_label_create(box);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(value_label, "--");
    lv_obj_set_width(value_label, UI_DEBUG_UART_BOX_W - 42);
    lv_obj_set_style_text_font(value_label, &egg_ui_font_12, 0);
    lv_obj_set_style_text_color(value_label, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_set_style_text_letter_space(value_label, 1, 0);
    lv_obj_align(value_label, LV_ALIGN_RIGHT_MID, 0, 0);

    if(out_value_label != NULL) {
        *out_value_label = value_label;
    }

    return box;
}

static void ui_debug_set_value_text(lv_obj_t *label, char *last_text, size_t last_text_size, const char *text);
static void ui_debug_action_event_cb(lv_event_t *e);
static void ui_debug_style_nav_button(lv_obj_t *button, lv_coord_t width, lv_coord_t height);
static lv_obj_t *ui_debug_create_grid_button(lv_obj_t *parent,
                                             const char *text,
                                             const lv_font_t *font,
                                             lv_event_cb_t event_cb,
                                             void *user_data,
                                             int col,
                                             int row);

static void ui_debug_set_status_button_text(lv_obj_t *label,
                                            char *last_text,
                                            size_t last_text_size,
                                            const char *title,
                                            const char *value_text)
{
    char text[UI_DEBUG_UART_TEXT_MAX];
    const char *safe_value_text = (value_text != NULL && value_text[0] != '\0') ? value_text : "--";

    if(title == NULL) {
        title = "";
    }

    (void)snprintf(text, sizeof(text), "%s:[%s]", title, safe_value_text);
    ui_debug_set_value_text(label, last_text, last_text_size, text);
}

static lv_obj_t *ui_debug_create_status_button(lv_obj_t *parent,
                                               const char *title,
                                               ui_debug_action_t action,
                                               lv_obj_t **out_label,
                                               char *last_text,
                                               size_t last_text_size,
                                               lv_coord_t width,
                                               const lv_font_t *font)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;

    if(font == NULL) {
        font = &egg_ui_font_12;
    }

    ui_debug_style_button(button, width, UI_SENSOR_BTN_H, 12);
    lv_obj_add_event_cb(button, ui_debug_action_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)action);

    label = lv_label_create(button);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(label, width - 8);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);

    ui_debug_set_status_button_text(label, last_text, last_text_size, title, "--");

    if(out_label != NULL) {
        *out_label = label;
    }

    return button;
}

static void ui_debug_set_value_text(lv_obj_t *label, char *last_text, size_t last_text_size, const char *text)
{
    const char *safe_text = (text != NULL && text[0] != '\0') ? text : "--";

    if(label == NULL || last_text == NULL || last_text_size == 0U) {
        return;
    }

    if(strcmp(safe_text, last_text) != 0) {
        strlcpy(last_text, safe_text, last_text_size);
        lv_label_set_text(label, safe_text);
    }
}

static void ui_debug_set_distance_text(ui_debug_distance_target_t target, const char *text)
{
    switch(target) {
    case UI_DEBUG_DISTANCE_TARGET_ENV_TEMP:
        ui_debug_set_status_button_text(s_debug.env_temp_value_label,
                                        s_debug.last_env_temp,
                                        sizeof(s_debug.last_env_temp),
                                        UI_DEBUG_ENV_TEMP_TITLE,
                                        (text != NULL && text[0] != '\0') ? text : "-");
        break;
    case UI_DEBUG_DISTANCE_TARGET_TRAY:
        ui_debug_set_status_button_text(s_debug.tray_distance_value_label,
                                        s_debug.last_tray_distance,
                                        sizeof(s_debug.last_tray_distance),
                                        UI_DEBUG_TRAY_DISTANCE_TITLE,
                                        (text != NULL && text[0] != '\0') ? text : "-");
        break;
    case UI_DEBUG_DISTANCE_TARGET_PAN_A:
        ui_debug_set_status_button_text(s_debug.pan_a_distance_value_label,
                                        s_debug.last_pan_a_distance,
                                        sizeof(s_debug.last_pan_a_distance),
                                        UI_DEBUG_PAN_A_DISTANCE_TITLE,
                                        (text != NULL && text[0] != '\0') ? text : "-");
        break;
    case UI_DEBUG_DISTANCE_TARGET_NONE:
        break;
    }
}

static uint32_t *ui_debug_get_status_pending_ms(ui_debug_status_target_t target)
{
    switch(target) {
    case UI_DEBUG_STATUS_TARGET_PUSH:
        return &s_debug.pending_push_status_started_ms;
    case UI_DEBUG_STATUS_TARGET_POSITION:
        return &s_debug.pending_position_status_started_ms;
    case UI_DEBUG_STATUS_TARGET_OIL:
        return &s_debug.pending_oil_status_started_ms;
    case UI_DEBUG_STATUS_TARGET_NONE:
        break;
    }

    return NULL;
}

static void ui_debug_set_status_text(ui_debug_status_target_t target, const char *text)
{
    switch(target) {
    case UI_DEBUG_STATUS_TARGET_PUSH:
        ui_debug_set_status_button_text(s_debug.push_state_label,
                                        s_debug.last_push_state,
                                        sizeof(s_debug.last_push_state),
                                        UI_DEBUG_PUSH_STATE_TITLE,
                                        text);
        break;
    case UI_DEBUG_STATUS_TARGET_POSITION:
        ui_debug_set_status_button_text(s_debug.position_state_label,
                                        s_debug.last_position_state,
                                        sizeof(s_debug.last_position_state),
                                        UI_DEBUG_POSITION_STATE_TITLE,
                                        text);
        break;
    case UI_DEBUG_STATUS_TARGET_OIL:
        ui_debug_set_status_button_text(s_debug.oil_state_label,
                                        s_debug.last_oil_state,
                                        sizeof(s_debug.last_oil_state),
                                        UI_DEBUG_OIL_STATE_TITLE,
                                        text);
        break;
    case UI_DEBUG_STATUS_TARGET_NONE:
        break;
    }
}

static void ui_debug_set_status_value(ui_debug_status_target_t target, int32_t value)
{
    char text[12];
    uint32_t *pending_ms = ui_debug_get_status_pending_ms(target);

    (void)snprintf(text, sizeof(text), "%ld", (long)value);
    if(pending_ms != NULL) {
        *pending_ms = 0U;
    }

    ui_debug_set_status_text(target, text);
}

static void ui_debug_prepare_status_request(ui_debug_status_target_t target)
{
    uint32_t *pending_ms = ui_debug_get_status_pending_ms(target);

    if(pending_ms == NULL) {
        return;
    }

    *pending_ms = lv_tick_get();
    ui_debug_set_status_text(target, "...");
}

static void ui_debug_fail_status_request(ui_debug_status_target_t target)
{
    uint32_t *pending_ms = ui_debug_get_status_pending_ms(target);

    if(pending_ms != NULL) {
        *pending_ms = 0U;
    }

    ui_debug_set_status_text(target, "--");
}

static void ui_debug_check_status_timeout(ui_debug_status_target_t target)
{
    uint32_t *pending_ms = ui_debug_get_status_pending_ms(target);

    if(pending_ms == NULL || *pending_ms == 0U) {
        return;
    }

    if(lv_tick_elaps(*pending_ms) >= UI_DEBUG_STATUS_TIMEOUT_MS) {
        ui_debug_fail_status_request(target);
    }
}

static ui_debug_status_target_t ui_debug_action_to_status_target(ui_debug_action_t action)
{
    switch(action) {
    case UI_DEBUG_ACTION_PUSH_STATE:
        return UI_DEBUG_STATUS_TARGET_PUSH;
    case UI_DEBUG_ACTION_POSITION_STATE:
        return UI_DEBUG_STATUS_TARGET_POSITION;
    case UI_DEBUG_ACTION_OIL_STATE:
        return UI_DEBUG_STATUS_TARGET_OIL;
    case UI_DEBUG_ACTION_RESET:
    case UI_DEBUG_ACTION_HOME:
    case UI_DEBUG_ACTION_GIVE_EGG:
    case UI_DEBUG_ACTION_GIVE_OIL:
    case UI_DEBUG_ACTION_BACK_OIL:
    case UI_DEBUG_ACTION_CANCEL_NOTICE_PREVIEW:
    case UI_DEBUG_ACTION_BEAT_EGG:
    case UI_DEBUG_ACTION_FAN_TOGGLE:
    case UI_DEBUG_ACTION_FLIP_A:
    case UI_DEBUG_ACTION_FLIP_B:
    case UI_DEBUG_ACTION_BEAT_PUSH:
    case UI_DEBUG_ACTION_BEAT_BACK:
    case UI_DEBUG_ACTION_HEAT_A_10S:
    case UI_DEBUG_ACTION_HEAT_B_10S:
    case UI_DEBUG_ACTION_TRANS_DUMP:
    case UI_DEBUG_ACTION_TRANS_DUMP_SECOND:
    case UI_DEBUG_ACTION_ENV_TEMP:
    case UI_DEBUG_ACTION_TRAY_DISTANCE:
    case UI_DEBUG_ACTION_PAN_A_DISTANCE:
        return UI_DEBUG_STATUS_TARGET_NONE;
    }

    return UI_DEBUG_STATUS_TARGET_NONE;
}

static bool ui_debug_extract_plain_integer_line(const char *rx, char *out, size_t out_size)
{
    long value;
    char tail;
    int parsed_count;

    if(rx == NULL || out == NULL || out_size == 0U) {
        return false;
    }

    parsed_count = sscanf(rx, " %ld %c", &value, &tail);
    if(parsed_count != 1) {
        return false;
    }

    (void)snprintf(out, out_size, "%ld", value);
    return true;
}

static bool ui_debug_parse_distance_response(const char *rx,
                                             ui_debug_distance_target_t *out_target,
                                             char *out_text,
                                             size_t out_text_size)
{
    char prefix[8];
    char sensor[16];
    long value;
    char tail;
    int parsed_count;

    if(rx == NULL || out_target == NULL || out_text == NULL || out_text_size == 0U) {
        return false;
    }

    parsed_count = sscanf(rx, " %7s %15s %ld %c", prefix, sensor, &value, &tail);
    if(parsed_count != 3) {
        return false;
    }

    if(strcmp(prefix, "MEAS") != 0 && strcmp(prefix, "DIST") != 0) {
        return false;
    }

    if(strcmp(sensor, "ENVTMP") == 0 ||
       strcmp(sensor, "ENV_TMP") == 0 ||
       strcmp(sensor, "ENV") == 0) {
        *out_target = UI_DEBUG_DISTANCE_TARGET_ENV_TEMP;
    } else if(strcmp(sensor, "TRAY") == 0 || strcmp(sensor, "HAVE") == 0) {
        *out_target = UI_DEBUG_DISTANCE_TARGET_TRAY;
    } else if(strcmp(sensor, "PANA") == 0 ||
              strcmp(sensor, "PAN_A") == 0 ||
              strcmp(sensor, "POTA") == 0) {
        *out_target = UI_DEBUG_DISTANCE_TARGET_PAN_A;
    } else {
        return false;
    }

    (void)snprintf(out_text, out_text_size, "%ld", value);
    return true;
}

static bool ui_debug_parse_status_response(const char *rx,
                                           ui_debug_status_target_t *out_target,
                                           int32_t *out_value)
{
    char prefix[8];
    char name[16];
    long value;
    char tail;
    int parsed_count;

    if(rx == NULL || out_target == NULL || out_value == NULL) {
        return false;
    }

    parsed_count = sscanf(rx, " %7s %15s %ld %c", prefix, name, &value, &tail);
    if(parsed_count != 3 || strcmp(prefix, "STAT") != 0) {
        return false;
    }

    if(strcmp(name, "PUBS") == 0 || strcmp(name, "PUSH") == 0) {
        *out_target = UI_DEBUG_STATUS_TARGET_PUSH;
    } else if(strcmp(name, "TRANSS") == 0 ||
              strcmp(name, "TRANS") == 0 ||
              strcmp(name, "POSITION") == 0) {
        *out_target = UI_DEBUG_STATUS_TARGET_POSITION;
    } else if(strcmp(name, "OILS") == 0 || strcmp(name, "OIL") == 0) {
        *out_target = UI_DEBUG_STATUS_TARGET_OIL;
    } else {
        return false;
    }

    *out_value = (int32_t)value;
    return true;
}

static void ui_debug_handle_rx_line(const char *rx)
{
    ui_debug_distance_target_t target;
    ui_debug_status_target_t status_target;
    char distance_text[UI_DEBUG_UART_TEXT_MAX];
    int32_t status_value;

    if(rx == NULL || rx[0] == '\0') {
        return;
    }

    if(ui_debug_parse_distance_response(rx, &target, distance_text, sizeof(distance_text))) {
        ui_debug_set_distance_text(target, distance_text);
        if(s_debug.pending_distance_target == target) {
            s_debug.pending_distance_target = UI_DEBUG_DISTANCE_TARGET_NONE;
            s_debug.pending_distance_started_ms = 0U;
        }
        return;
    }

    if(ui_debug_parse_status_response(rx, &status_target, &status_value)) {
        ui_debug_set_status_value(status_target, status_value);
        return;
    }

    if(s_debug.pending_distance_target != UI_DEBUG_DISTANCE_TARGET_NONE &&
       ui_debug_extract_plain_integer_line(rx, distance_text, sizeof(distance_text))) {
        ui_debug_set_distance_text(s_debug.pending_distance_target, distance_text);
        s_debug.pending_distance_target = UI_DEBUG_DISTANCE_TARGET_NONE;
        s_debug.pending_distance_started_ms = 0U;
    }
}

static void ui_debug_refresh_serial_boxes(void)
{
    char tx[UI_DEBUG_UART_TEXT_MAX];
    char rx[UI_DEBUG_RX_LINE_MAX];

    if(s_debug.tx_value_label == NULL || s_debug.rx_value_label == NULL) {
        return;
    }

    ui_uart_bridge_get_last_messages(tx, sizeof(tx), NULL, 0U);
    ui_debug_set_value_text(s_debug.tx_value_label, s_debug.last_tx, sizeof(s_debug.last_tx), tx);

    while(ui_uart_bridge_get_next_rx_line(&s_debug.rx_history_cursor, rx, sizeof(rx))) {
        ui_debug_set_value_text(s_debug.rx_value_label, s_debug.last_rx, sizeof(s_debug.last_rx), rx);
        if(ui_heater_debug_handle_rx_line(rx)) {
            continue;
        }
        ui_debug_handle_rx_line(rx);
    }

    if(s_debug.pending_distance_target != UI_DEBUG_DISTANCE_TARGET_NONE &&
       s_debug.pending_distance_started_ms != 0U &&
       lv_tick_elaps(s_debug.pending_distance_started_ms) >= UI_DEBUG_DISTANCE_TIMEOUT_MS) {
        ui_debug_set_distance_text(s_debug.pending_distance_target, NULL);
        s_debug.pending_distance_target = UI_DEBUG_DISTANCE_TARGET_NONE;
        s_debug.pending_distance_started_ms = 0U;
    }

    ui_debug_check_status_timeout(UI_DEBUG_STATUS_TARGET_PUSH);
    ui_debug_check_status_timeout(UI_DEBUG_STATUS_TARGET_POSITION);
    ui_debug_check_status_timeout(UI_DEBUG_STATUS_TARGET_OIL);
    ui_heater_debug_poll_if_needed();
}

static void ui_debug_serial_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    ui_debug_refresh_serial_boxes();
}

static const char *ui_debug_action_name(ui_debug_action_t action)
{
    switch(action) {
    case UI_DEBUG_ACTION_RESET:
        return "reset";
    case UI_DEBUG_ACTION_HOME:
        return "home";
    case UI_DEBUG_ACTION_GIVE_EGG:
        return "give_egg";
    case UI_DEBUG_ACTION_GIVE_OIL:
        return "give_oil";
    case UI_DEBUG_ACTION_BACK_OIL:
        return "back_oil";
    case UI_DEBUG_ACTION_CANCEL_NOTICE_PREVIEW:
        return "cancel_notice_preview";
    case UI_DEBUG_ACTION_BEAT_EGG:
        return "beat_egg";
    case UI_DEBUG_ACTION_FAN_TOGGLE:
        return "fan_toggle";
    case UI_DEBUG_ACTION_FLIP_A:
        return "flip_a";
    case UI_DEBUG_ACTION_FLIP_B:
        return "flip_b";
    case UI_DEBUG_ACTION_BEAT_PUSH:
        return "beat_push";
    case UI_DEBUG_ACTION_BEAT_BACK:
        return "beat_back";
    case UI_DEBUG_ACTION_HEAT_A_10S:
        return "heat_a_10s";
    case UI_DEBUG_ACTION_HEAT_B_10S:
        return "heat_b_10s";
    case UI_DEBUG_ACTION_TRANS_DUMP:
        return "trans_dump";
    case UI_DEBUG_ACTION_TRANS_DUMP_SECOND:
        return "trans_dump_second";
    case UI_DEBUG_ACTION_ENV_TEMP:
        return "env_temp";
    case UI_DEBUG_ACTION_PUSH_STATE:
        return "push_state";
    case UI_DEBUG_ACTION_POSITION_STATE:
        return "position_state";
    case UI_DEBUG_ACTION_TRAY_DISTANCE:
        return "tray_distance";
    case UI_DEBUG_ACTION_PAN_A_DISTANCE:
        return "pan_a_distance";
    case UI_DEBUG_ACTION_OIL_STATE:
        return "oil_state";
    }

    return "unknown";
}

static void ui_debug_back_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    s_debug.active = false;
    ui_debug_stop_serial_timer();
    ui_create(s_debug.data);
}

static void ui_debug_next_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    ui_heater_debug_create(s_debug.data);
}

static void ui_alert_debug_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    ui_alert_debug_create(s_debug.data);
}

static void ui_debug_stress_test_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    ui_debug_stress_test_create(s_debug.data);
}

static void ui_debug_motor_debug_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    ui_motor_debug_create(s_debug.data);
}

static void ui_debug_move_debug_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    ui_move_debug_create(s_debug.data);
}

static void ui_debug_sensor_debug_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    s_debug.active = false;
    ui_debug_stop_serial_timer();
    ui_sensor_debug_create(s_debug.data);
}

static void ui_debug_bypass_switch_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    s_debug.active = false;
    ui_debug_stop_serial_timer();
    ui_bypass_switch_create(s_debug.data);
}

static void ui_debug_error_log_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    ui_error_log_create(s_debug.data);
}

static ui_debug_distance_target_t ui_debug_action_to_distance_target(ui_debug_action_t action)
{
    switch(action) {
    case UI_DEBUG_ACTION_ENV_TEMP:
        return UI_DEBUG_DISTANCE_TARGET_ENV_TEMP;
    case UI_DEBUG_ACTION_TRAY_DISTANCE:
        return UI_DEBUG_DISTANCE_TARGET_TRAY;
    case UI_DEBUG_ACTION_PAN_A_DISTANCE:
        return UI_DEBUG_DISTANCE_TARGET_PAN_A;
    case UI_DEBUG_ACTION_RESET:
    case UI_DEBUG_ACTION_HOME:
    case UI_DEBUG_ACTION_GIVE_EGG:
    case UI_DEBUG_ACTION_GIVE_OIL:
    case UI_DEBUG_ACTION_BACK_OIL:
    case UI_DEBUG_ACTION_CANCEL_NOTICE_PREVIEW:
    case UI_DEBUG_ACTION_BEAT_EGG:
    case UI_DEBUG_ACTION_FAN_TOGGLE:
    case UI_DEBUG_ACTION_FLIP_A:
    case UI_DEBUG_ACTION_FLIP_B:
    case UI_DEBUG_ACTION_BEAT_PUSH:
    case UI_DEBUG_ACTION_BEAT_BACK:
    case UI_DEBUG_ACTION_HEAT_A_10S:
    case UI_DEBUG_ACTION_HEAT_B_10S:
    case UI_DEBUG_ACTION_TRANS_DUMP:
    case UI_DEBUG_ACTION_TRANS_DUMP_SECOND:
    case UI_DEBUG_ACTION_PUSH_STATE:
    case UI_DEBUG_ACTION_POSITION_STATE:
    case UI_DEBUG_ACTION_OIL_STATE:
        return UI_DEBUG_DISTANCE_TARGET_NONE;
    }

    return UI_DEBUG_DISTANCE_TARGET_NONE;
}

static void ui_debug_prepare_distance_request(ui_debug_distance_target_t target)
{
    s_debug.pending_distance_target = target;
    s_debug.pending_distance_started_ms = lv_tick_get();
    s_debug.rx_history_cursor = ui_uart_bridge_get_rx_history_cursor();
    s_debug.last_rx[0] = '\0';
    ui_uart_bridge_clear_last_rx();

    if(s_debug.rx_value_label != NULL) {
        lv_label_set_text(s_debug.rx_value_label, "--");
    }

    ui_debug_set_distance_text(target, "...");
}

static void ui_debug_request_status_updates(void)
{
    ui_debug_prepare_status_request(UI_DEBUG_STATUS_TARGET_PUSH);
    ui_debug_prepare_status_request(UI_DEBUG_STATUS_TARGET_POSITION);
    ui_debug_prepare_status_request(UI_DEBUG_STATUS_TARGET_OIL);
    (void)ui_uart_bridge_send_raw_command(UI_DEBUG_PUSH_STATE_COMMAND, false);
    (void)ui_uart_bridge_send_raw_command(UI_DEBUG_POSITION_STATE_COMMAND, false);
    (void)ui_uart_bridge_send_raw_command(UI_DEBUG_OIL_STATE_COMMAND, false);
}

static void ui_debug_action_event_cb(lv_event_t *e)
{
    ui_debug_action_t action = (ui_debug_action_t)(uintptr_t)lv_event_get_user_data(e);
    esp_err_t err = ESP_OK;
    ui_debug_status_target_t status_target = ui_debug_action_to_status_target(action);

    buzzer_beep_async(50);
    ESP_LOGI(TAG, "debug action clicked: %s", ui_debug_action_name(action));

    switch(action) {
    case UI_DEBUG_ACTION_RESET:
        err = ui_uart_bridge_send_raw_command("RST", false);
        break;
    case UI_DEBUG_ACTION_HOME:
        err = ui_uart_bridge_send_raw_command("TST HOME", false);
        break;
    case UI_DEBUG_ACTION_GIVE_EGG:
        err = ui_uart_bridge_send_raw_command("TST GIVEEGG", false);
        break;
    case UI_DEBUG_ACTION_GIVE_OIL:
        err = ui_uart_bridge_send_raw_command("TST GIVEOIL", false);
        break;
    case UI_DEBUG_ACTION_BACK_OIL:
        err = ui_uart_bridge_send_raw_command("TST BACKOIL", false);
        break;
    case UI_DEBUG_ACTION_CANCEL_NOTICE_PREVIEW:
        s_debug.active = false;
        ui_debug_stop_serial_timer();
        ui_notice_create(s_debug.data,
                         "提示",
                         "正在立即停止。动作完全停止后，请清理锅内鸡蛋并确认机构已回到安全位置，再点击确认复位。",
                         "确认");
        break;
    case UI_DEBUG_ACTION_BEAT_EGG:
        err = ui_uart_bridge_send_raw_command("TST BEAT", false);
        break;
    case UI_DEBUG_ACTION_FAN_TOGGLE:
        err = ui_uart_bridge_send_raw_command("TST FAN", false);
        break;
    case UI_DEBUG_ACTION_FLIP_A:
        err = ui_uart_bridge_send_raw_command("TST TURNA", false);
        break;
    case UI_DEBUG_ACTION_FLIP_B:
        err = ui_uart_bridge_send_raw_command("TST TURNB", false);
        break;
    case UI_DEBUG_ACTION_BEAT_PUSH:
        err = ui_uart_bridge_send_raw_command("TST PUSHSTEP", false);
        break;
    case UI_DEBUG_ACTION_BEAT_BACK:
        err = ui_uart_bridge_send_raw_command("TST PUSHBACK", false);
        break;
    case UI_DEBUG_ACTION_HEAT_A_10S:
        err = ui_uart_bridge_send_raw_command("TST DWA 6 10", false);
        break;
    case UI_DEBUG_ACTION_HEAT_B_10S:
        err = ui_uart_bridge_send_raw_command("TST DWB 6 10", false);
        break;
    case UI_DEBUG_ACTION_TRANS_DUMP:
        err = ui_uart_bridge_send_raw_command("TST TRANS_DUMP", false);
        break;
    case UI_DEBUG_ACTION_TRANS_DUMP_SECOND:
        err = ui_uart_bridge_send_raw_command("TST TRANS_DUMP_RETRY", false);
        break;
    case UI_DEBUG_ACTION_ENV_TEMP:
        ui_debug_prepare_distance_request(UI_DEBUG_DISTANCE_TARGET_ENV_TEMP);
        err = ui_uart_bridge_send_raw_command(UI_DEBUG_ENV_TEMP_COMMAND, false);
        break;
    case UI_DEBUG_ACTION_PUSH_STATE:
        ui_debug_prepare_status_request(UI_DEBUG_STATUS_TARGET_PUSH);
        err = ui_uart_bridge_send_raw_command(UI_DEBUG_PUSH_STATE_COMMAND, false);
        break;
    case UI_DEBUG_ACTION_POSITION_STATE:
        ui_debug_prepare_status_request(UI_DEBUG_STATUS_TARGET_POSITION);
        err = ui_uart_bridge_send_raw_command(UI_DEBUG_POSITION_STATE_COMMAND, false);
        break;
    case UI_DEBUG_ACTION_TRAY_DISTANCE:
        ui_debug_prepare_distance_request(UI_DEBUG_DISTANCE_TARGET_TRAY);
        err = ui_uart_bridge_send_raw_command(UI_DEBUG_DISTANCE_COMMAND, false);
        break;
    case UI_DEBUG_ACTION_PAN_A_DISTANCE:
        ui_debug_prepare_distance_request(UI_DEBUG_DISTANCE_TARGET_PAN_A);
        err = ui_uart_bridge_send_raw_command(UI_DEBUG_PAN_A_DISTANCE_COMMAND, false);
        break;
    case UI_DEBUG_ACTION_OIL_STATE:
        ui_debug_prepare_status_request(UI_DEBUG_STATUS_TARGET_OIL);
        err = ui_uart_bridge_send_raw_command(UI_DEBUG_OIL_STATE_COMMAND, false);
        break;
    }

    if(err != ESP_OK) {
        ui_debug_distance_target_t distance_target = ui_debug_action_to_distance_target(action);

        if(distance_target != UI_DEBUG_DISTANCE_TARGET_NONE) {
            s_debug.pending_distance_target = UI_DEBUG_DISTANCE_TARGET_NONE;
            s_debug.pending_distance_started_ms = 0U;
            ui_debug_set_distance_text(distance_target, NULL);
        }
        if(status_target != UI_DEBUG_STATUS_TARGET_NONE) {
            ui_debug_fail_status_request(status_target);
        }
        ESP_LOGW(TAG, "debug command queue failed: %s", esp_err_to_name(err));
    }
}

static void ui_debug_style_nav_button(lv_obj_t *button, lv_coord_t width, lv_coord_t height)
{
    ui_debug_style_button(button, width, height, 12);
    lv_obj_set_style_bg_opa(button, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_C_PANEL), 0);
    lv_obj_set_style_border_color(button, lv_color_hex(UI_DEBUG_NAV_ACCENT), 0);
    lv_obj_set_style_border_opa(button, LV_OPA_70, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_C_PANEL), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 10, 0);
    lv_obj_set_style_shadow_color(button, lv_color_hex(UI_DEBUG_NAV_ACCENT), 0);
    lv_obj_set_style_shadow_opa(button, LV_OPA_10, 0);
}

static lv_obj_t *ui_debug_create_grid_button(lv_obj_t *parent,
                                             const char *text,
                                             const lv_font_t *font,
                                             lv_event_cb_t event_cb,
                                             void *user_data,
                                             int col,
                                             int row)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;
    lv_coord_t dx = (col == 0) ? -UI_DEBUG_GRID_COL_DX : UI_DEBUG_GRID_COL_DX;
    lv_coord_t y = UI_DEBUG_GRID_TOP_Y + row * (UI_DEBUG_GRID_BTN_H + UI_DEBUG_GRID_ROW_GAP);

    ui_debug_style_nav_button(button, UI_DEBUG_GRID_BTN_W, UI_DEBUG_GRID_BTN_H);
    lv_obj_add_event_cb(button, event_cb, LV_EVENT_CLICKED, user_data);

    label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(label, UI_DEBUG_GRID_BTN_W - 8);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    lv_obj_align(button, LV_ALIGN_TOP_MID, dx, y);

    return button;
}

void ui_debug_start_serial_timer(void)
{
    char tx[UI_DEBUG_UART_TEXT_MAX];
    char rx[UI_DEBUG_UART_TEXT_MAX];

    s_debug.rx_history_cursor = ui_uart_bridge_get_rx_history_cursor();
    ui_uart_bridge_get_last_messages(tx, sizeof(tx), rx, sizeof(rx));
    ui_debug_set_value_text(s_debug.tx_value_label, s_debug.last_tx, sizeof(s_debug.last_tx), tx);
    ui_debug_set_value_text(s_debug.rx_value_label, s_debug.last_rx, sizeof(s_debug.last_rx), rx);
    s_debug.serial_timer = lv_timer_create(ui_debug_serial_timer_cb, 120, NULL);
}

void ui_debug_create(ui_data_t *data)
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
    lv_memzero(&s_debug, sizeof(s_debug));
    s_debug.active = true;
    s_debug.data = data;
    s_debug.rx_history_cursor = ui_uart_bridge_get_rx_history_cursor();

    screen = lv_screen_active();

    ui_screen_reset(screen);

    button = lv_button_create(screen);
    ui_debug_style_button(button, UI_DEBUG_BACK_BTN_W, UI_DEBUG_BACK_BTN_H, 13);
    lv_obj_align(button, LV_ALIGN_TOP_LEFT, UI_DEBUG_SIDE_MARGIN, UI_DEBUG_TOP_ROW_Y);
    lv_obj_add_event_cb(button, ui_debug_back_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(button);
    lv_label_set_text(label, "BACK");
    lv_obj_set_style_text_font(label, &egg_ui_font_12, 0);
    lv_obj_set_style_text_letter_space(label, 1, 0);
    lv_obj_center(label);

    tx_box = ui_debug_create_uart_box(screen, "TX", &s_debug.tx_value_label);
    lv_obj_align_to(tx_box, button, LV_ALIGN_OUT_RIGHT_MID, UI_DEBUG_TOP_ROW_GAP, 0);

    rx_box = ui_debug_create_uart_box(screen, "RX", &s_debug.rx_value_label);
    lv_obj_align_to(rx_box, tx_box, LV_ALIGN_OUT_RIGHT_MID, UI_DEBUG_TOP_ROW_GAP, 0);

    /* 页面标题 */
    label = lv_label_create(screen);
    lv_label_set_text(label, "调试");
    lv_obj_set_style_text_font(label, &ui_alert_debug_font_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, UI_DEBUG_TITLE_Y);

    /* 统一风格按钮网格：2 列 × 5 行 */
    ui_debug_create_grid_button(screen, "重启", &egg_ui_font_16,
                                ui_debug_action_event_cb,
                                (void *)(uintptr_t)UI_DEBUG_ACTION_RESET, 0, 0);
    ui_debug_create_grid_button(screen, "错误日志", &ui_alert_font_16,
                                ui_debug_error_log_event_cb, NULL, 1, 0);
    ui_debug_create_grid_button(screen, "电机调试", &ui_motor_font_16,
                                ui_debug_motor_debug_event_cb, NULL, 0, 1);
    ui_debug_create_grid_button(screen, "运动调试", &ui_move_debug_font_16,
                                ui_debug_move_debug_event_cb, NULL, 1, 1);
    ui_debug_create_grid_button(screen, "传感器调试", &ui_sensor_debug_font_16,
                                ui_debug_sensor_debug_event_cb, NULL, 0, 2);
    ui_debug_create_grid_button(screen, "压力测试", &ui_alert_debug_font_16,
                                ui_debug_stress_test_event_cb, NULL, 1, 2);
    ui_debug_create_grid_button(screen, "告警调试页", &ui_alert_debug_font_16,
                                ui_alert_debug_event_cb, NULL, 0, 3);
    ui_debug_create_grid_button(screen, "电磁炉调试页", &ui_alert_debug_font_16,
                                ui_debug_next_event_cb, NULL, 1, 3);
    ui_debug_create_grid_button(screen, "调试开关", &ui_sensor_debug_font_16,
                                ui_debug_bypass_switch_event_cb, NULL, 0, 4);

    ui_debug_start_serial_timer();
    ui_debug_request_status_updates();
}

static void ui_sensor_debug_back_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    s_debug.active = false;
    ui_debug_stop_serial_timer();
    ui_debug_create(s_debug.data);
}

static lv_obj_t *ui_sensor_debug_create_column(lv_obj_t *parent, lv_coord_t x)
{
    lv_obj_t *list = lv_obj_create(parent);

    ui_prepare_transparent_obj(list);
    lv_obj_set_size(list, UI_SENSOR_BTN_W, UI_SENSOR_LIST_H);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_gap(list, UI_SENSOR_ROW_GAP, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(list, LV_ALIGN_TOP_LEFT, x, UI_SENSOR_LIST_TOP_Y);
    return list;
}

void ui_sensor_debug_create(ui_data_t *data)
{
    lv_obj_t *screen;
    lv_obj_t *button;
    lv_obj_t *label;
    lv_obj_t *tx_box;
    lv_obj_t *rx_box;
    lv_obj_t *left_list;
    lv_obj_t *right_list;
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
    lv_memzero(&s_debug, sizeof(s_debug));
    s_debug.active = true;
    s_debug.data = data;
    s_debug.rx_history_cursor = ui_uart_bridge_get_rx_history_cursor();

    screen = lv_screen_active();
    ui_screen_reset(screen);

    button = lv_button_create(screen);
    ui_debug_style_button(button, UI_DEBUG_BACK_BTN_W, UI_DEBUG_BACK_BTN_H, 13);
    lv_obj_align(button, LV_ALIGN_TOP_LEFT, UI_DEBUG_SIDE_MARGIN, UI_DEBUG_TOP_ROW_Y);
    lv_obj_add_event_cb(button, ui_sensor_debug_back_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(button);
    lv_label_set_text(label, "BACK");
    lv_obj_set_style_text_font(label, &egg_ui_font_12, 0);
    lv_obj_set_style_text_letter_space(label, 1, 0);
    lv_obj_center(label);

    tx_box = ui_debug_create_uart_box(screen, "TX", &s_debug.tx_value_label);
    lv_obj_align_to(tx_box, button, LV_ALIGN_OUT_RIGHT_MID, UI_DEBUG_TOP_ROW_GAP, 0);

    rx_box = ui_debug_create_uart_box(screen, "RX", &s_debug.rx_value_label);
    lv_obj_align_to(rx_box, tx_box, LV_ALIGN_OUT_RIGHT_MID, UI_DEBUG_TOP_ROW_GAP, 0);

    /* 页面标题 */
    label = lv_label_create(screen);
    lv_label_set_text(label, "传感器调试");
    lv_obj_set_style_text_font(label, &ui_sensor_debug_font_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, UI_SENSOR_TITLE_Y);

    left_list = ui_sensor_debug_create_column(screen, UI_SENSOR_LEFT_X);
    right_list = ui_sensor_debug_create_column(screen,
                                               UI_SENSOR_LEFT_X + UI_SENSOR_BTN_W + UI_SENSOR_COL_GAP);

    (void)ui_debug_create_status_button(left_list,
                                        UI_DEBUG_ENV_TEMP_TITLE,
                                        UI_DEBUG_ACTION_ENV_TEMP,
                                        &s_debug.env_temp_value_label,
                                        s_debug.last_env_temp,
                                        sizeof(s_debug.last_env_temp),
                                        UI_SENSOR_BTN_W,
                                        NULL);
    (void)ui_debug_create_status_button(left_list,
                                        UI_DEBUG_TRAY_DISTANCE_TITLE,
                                        UI_DEBUG_ACTION_TRAY_DISTANCE,
                                        &s_debug.tray_distance_value_label,
                                        s_debug.last_tray_distance,
                                        sizeof(s_debug.last_tray_distance),
                                        UI_SENSOR_BTN_W,
                                        NULL);
    (void)ui_debug_create_status_button(left_list,
                                        UI_DEBUG_PUSH_STATE_TITLE,
                                        UI_DEBUG_ACTION_PUSH_STATE,
                                        &s_debug.push_state_label,
                                        s_debug.last_push_state,
                                        sizeof(s_debug.last_push_state),
                                        UI_SENSOR_BTN_W,
                                        NULL);

    (void)ui_debug_create_status_button(right_list,
                                        UI_DEBUG_POSITION_STATE_TITLE,
                                        UI_DEBUG_ACTION_POSITION_STATE,
                                        &s_debug.position_state_label,
                                        s_debug.last_position_state,
                                        sizeof(s_debug.last_position_state),
                                        UI_SENSOR_BTN_W,
                                        NULL);
    (void)ui_debug_create_status_button(right_list,
                                        UI_DEBUG_OIL_STATE_TITLE,
                                        UI_DEBUG_ACTION_OIL_STATE,
                                        &s_debug.oil_state_label,
                                        s_debug.last_oil_state,
                                        sizeof(s_debug.last_oil_state),
                                        UI_SENSOR_BTN_W,
                                        NULL);
    ui_debug_set_distance_text(UI_DEBUG_DISTANCE_TARGET_ENV_TEMP, NULL);
    ui_debug_set_distance_text(UI_DEBUG_DISTANCE_TARGET_TRAY, NULL);

    ui_debug_start_serial_timer();
    ui_debug_request_status_updates();
}

void ui_debug_on_data_changed(void)
{
    const ui_data_state_t *state;

    if(!s_debug.active) {
        return;
    }

    state = ui_debug_get_state();
    if(ui_alert_should_show(state)) {
        s_debug.active = false;
        ui_debug_stop_serial_timer();
        ui_alert_create(s_debug.data);
    }
}
