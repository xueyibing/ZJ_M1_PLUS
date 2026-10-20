#include "ui_motor_debug.h"

#include "buzzer.h"
#include "ui_alert.h"
#include "ui_common.h"
#include "ui_debug.h"
#include "ui_debug_internal.h"
#include "ui_uart_bridge.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define UI_MOTOR_COUNT            4U
#define UI_MOTOR_CURRENT_MIN      1000
#define UI_MOTOR_CURRENT_MAX      2000
#define UI_MOTOR_CURRENT_STEP     200
#define UI_MOTOR_CURRENT_DEFAULT  1500

#define UI_MOTOR_MICRO_MIN        8
#define UI_MOTOR_MICRO_MAX        256
#define UI_MOTOR_MICRO_DEFAULT    8

#define UI_MOTOR_ROW_X            4
#define UI_MOTOR_LABEL_W          36
#define UI_MOTOR_COL_W            68
#define UI_MOTOR_ROW_W            (UI_MOTOR_LABEL_W + UI_MOTOR_COL_W * (int)UI_MOTOR_COUNT)
#define UI_MOTOR_ROW_H            30
#define UI_MOTOR_ROW_GAP          4
#define UI_MOTOR_CTRL_H           20
#define UI_MOTOR_SMALL_BTN_W      17
#define UI_MOTOR_VALUE_W          30
#define UI_MOTOR_HEADER_Y         46
#define UI_MOTOR_LIST_Y           64

typedef enum {
    UI_MOTOR_ACTION_CURRENT_DEC = 0,
    UI_MOTOR_ACTION_CURRENT_INC,
    UI_MOTOR_ACTION_MICRO_DEC,
    UI_MOTOR_ACTION_MICRO_INC,
} ui_motor_action_t;

#define UI_MOTOR_ACTION_STRIDE    4U

typedef struct {
    bool active;
    ui_data_t *data;
    int current_ma[UI_MOTOR_COUNT];
    lv_obj_t *current_label[UI_MOTOR_COUNT];
    int microsteps[UI_MOTOR_COUNT];
    lv_obj_t *micro_label[UI_MOTOR_COUNT];
} ui_motor_debug_view_t;

static ui_motor_debug_view_t s_motor_debug;

static const char *const k_motor_names[UI_MOTOR_COUNT] = {
    "PUSH",
    "TRANS",
    "POTB",
    "POTA",
};

static uintptr_t ui_motor_encode_action(size_t motor_index, ui_motor_action_t action)
{
    return (uintptr_t)(motor_index * UI_MOTOR_ACTION_STRIDE + (size_t)action);
}

static size_t ui_motor_decode_index(uintptr_t encoded)
{
    return (size_t)(encoded / UI_MOTOR_ACTION_STRIDE);
}

static ui_motor_action_t ui_motor_decode_action(uintptr_t encoded)
{
    return (ui_motor_action_t)(encoded % UI_MOTOR_ACTION_STRIDE);
}

static int ui_motor_clamp_current(int value)
{
    if(value < UI_MOTOR_CURRENT_MIN) {
        return UI_MOTOR_CURRENT_MIN;
    }
    if(value > UI_MOTOR_CURRENT_MAX) {
        return UI_MOTOR_CURRENT_MAX;
    }
    return value;
}

static int ui_motor_clamp_microsteps(int value)
{
    if(value < UI_MOTOR_MICRO_MIN) {
        return UI_MOTOR_MICRO_MIN;
    }
    if(value > UI_MOTOR_MICRO_MAX) {
        return UI_MOTOR_MICRO_MAX;
    }
    return value;
}

static void ui_motor_update_labels(void)
{
    char text[16];
    size_t motor_index;

    if(!s_motor_debug.active) {
        return;
    }

    for(motor_index = 0; motor_index < UI_MOTOR_COUNT; motor_index++) {
        if(s_motor_debug.current_label[motor_index] != NULL) {
            (void)snprintf(text, sizeof(text), "%d", s_motor_debug.current_ma[motor_index]);
            lv_label_set_text(s_motor_debug.current_label[motor_index], text);
        }
        if(s_motor_debug.micro_label[motor_index] != NULL) {
            (void)snprintf(text, sizeof(text), "%d", s_motor_debug.microsteps[motor_index]);
            lv_label_set_text(s_motor_debug.micro_label[motor_index], text);
        }
    }
}

static void ui_motor_send_current(size_t motor_index)
{
    char command[32];

    if(motor_index >= UI_MOTOR_COUNT) {
        return;
    }

    (void)snprintf(command, sizeof(command), "TST CUR %s %d",
                   k_motor_names[motor_index], s_motor_debug.current_ma[motor_index]);
    (void)ui_uart_bridge_send_raw_command(command, false);
}

static void ui_motor_send_microsteps(size_t motor_index)
{
    char command[32];

    if(motor_index >= UI_MOTOR_COUNT) {
        return;
    }

    (void)snprintf(command, sizeof(command), "TST MST %s %d",
                   k_motor_names[motor_index], s_motor_debug.microsteps[motor_index]);
    (void)ui_uart_bridge_send_raw_command(command, false);
}

static void ui_motor_action_event_cb(lv_event_t *e)
{
    uintptr_t encoded = (uintptr_t)lv_event_get_user_data(e);
    size_t motor_index = ui_motor_decode_index(encoded);
    ui_motor_action_t action = ui_motor_decode_action(encoded);

    if(motor_index >= UI_MOTOR_COUNT) {
        return;
    }

    buzzer_beep_async(30);

    switch(action) {
    case UI_MOTOR_ACTION_CURRENT_DEC:
        s_motor_debug.current_ma[motor_index] = ui_motor_clamp_current(
            s_motor_debug.current_ma[motor_index] - UI_MOTOR_CURRENT_STEP);
        ui_motor_update_labels();
        ui_motor_send_current(motor_index);
        break;
    case UI_MOTOR_ACTION_CURRENT_INC:
        s_motor_debug.current_ma[motor_index] = ui_motor_clamp_current(
            s_motor_debug.current_ma[motor_index] + UI_MOTOR_CURRENT_STEP);
        ui_motor_update_labels();
        ui_motor_send_current(motor_index);
        break;
    case UI_MOTOR_ACTION_MICRO_DEC:
        s_motor_debug.microsteps[motor_index] = ui_motor_clamp_microsteps(
            s_motor_debug.microsteps[motor_index] / 2);
        ui_motor_update_labels();
        ui_motor_send_microsteps(motor_index);
        break;
    case UI_MOTOR_ACTION_MICRO_INC:
        s_motor_debug.microsteps[motor_index] = ui_motor_clamp_microsteps(
            s_motor_debug.microsteps[motor_index] * 2);
        ui_motor_update_labels();
        ui_motor_send_microsteps(motor_index);
        break;
    }
}

static void ui_motor_back_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    s_motor_debug.active = false;
    ui_debug_create(s_motor_debug.data);
}

static void ui_motor_create_header(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t width)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, &ui_motor_font_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_MED), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, x, UI_MOTOR_HEADER_Y + 2);
}

static lv_obj_t *ui_motor_create_row(lv_obj_t *parent, const char *title, lv_coord_t y)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_t *label;

    ui_debug_style_box(row, UI_MOTOR_ROW_W, UI_MOTOR_ROW_H, 14);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_10, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_C_PANEL), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(UI_C_LINE), 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_pos(row, UI_MOTOR_ROW_X, y);

    label = lv_label_create(row);
    lv_label_set_text(label, title);
    lv_obj_set_width(label, UI_MOTOR_LABEL_W);
    lv_obj_set_style_text_font(label, &ui_motor_font_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, 0, (UI_MOTOR_ROW_H - 12) / 2);

    return row;
}

static void ui_motor_create_adjust_button(lv_obj_t *parent,
                                          lv_coord_t x,
                                          const char *text,
                                          size_t motor_index,
                                          ui_motor_action_t action)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;

    ui_debug_style_button(button, UI_MOTOR_SMALL_BTN_W, UI_MOTOR_CTRL_H, 10);
    lv_obj_set_pos(button, x, (UI_MOTOR_ROW_H - UI_MOTOR_CTRL_H) / 2);
    lv_obj_add_event_cb(button,
                        ui_motor_action_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)ui_motor_encode_action(motor_index, action));

    label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &egg_ui_font_16, 0);
    lv_obj_center(label);
}

static lv_obj_t *ui_motor_create_value_label(lv_obj_t *parent, lv_coord_t x)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_t *label;

    ui_debug_style_box(box, UI_MOTOR_VALUE_W, UI_MOTOR_CTRL_H, 10);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(box, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(UI_C_PANEL), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(UI_C_LINE), 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_pos(box, x, (UI_MOTOR_ROW_H - UI_MOTOR_CTRL_H) / 2);

    label = lv_label_create(box);
    lv_obj_set_style_text_font(label, &ui_motor_font_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_center(label);
    return label;
}

static void ui_motor_fill_row(lv_obj_t *row,
                              lv_obj_t **value_labels,
                              ui_motor_action_t dec_action,
                              ui_motor_action_t inc_action)
{
    size_t motor_index;

    for(motor_index = 0; motor_index < UI_MOTOR_COUNT; motor_index++) {
        lv_coord_t base = UI_MOTOR_LABEL_W + (lv_coord_t)motor_index * UI_MOTOR_COL_W;

        ui_motor_create_adjust_button(row,
                                      base + 1,
                                      "-",
                                      motor_index,
                                      dec_action);
        value_labels[motor_index] =
            ui_motor_create_value_label(row, base + 1 + UI_MOTOR_SMALL_BTN_W + 1);
        ui_motor_create_adjust_button(row,
                                      base + 1 + UI_MOTOR_SMALL_BTN_W + 1 + UI_MOTOR_VALUE_W + 1,
                                      "+",
                                      motor_index,
                                      inc_action);
    }
}

void ui_motor_debug_create(ui_data_t *data)
{
    const ui_data_state_t *state;
    lv_obj_t *page_root;
    lv_obj_t *screen;
    lv_obj_t *button;
    lv_obj_t *label;
    lv_obj_t *title;
    lv_obj_t *current_row;
    lv_obj_t *micro_row;
    size_t motor_index;

    if(data == NULL) {
        return;
    }

    state = ui_data_get_state(data);
    if(ui_alert_should_show(state)) {
        ui_alert_create(data);
        return;
    }

    for(motor_index = 0; motor_index < UI_MOTOR_COUNT; motor_index++) {
        if(s_motor_debug.current_ma[motor_index] < UI_MOTOR_CURRENT_MIN) {
            s_motor_debug.current_ma[motor_index] = UI_MOTOR_CURRENT_DEFAULT;
        } else {
            s_motor_debug.current_ma[motor_index] =
                ui_motor_clamp_current(s_motor_debug.current_ma[motor_index]);
        }
        if(s_motor_debug.microsteps[motor_index] < UI_MOTOR_MICRO_MIN) {
            s_motor_debug.microsteps[motor_index] = UI_MOTOR_MICRO_DEFAULT;
        } else {
            s_motor_debug.microsteps[motor_index] =
                ui_motor_clamp_microsteps(s_motor_debug.microsteps[motor_index]);
        }
        s_motor_debug.current_label[motor_index] = NULL;
        s_motor_debug.micro_label[motor_index] = NULL;
    }

    s_motor_debug.active = true;
    s_motor_debug.data = data;

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
    lv_obj_add_event_cb(button, ui_motor_back_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(button);
    lv_label_set_text(label, "BACK");
    lv_obj_set_style_text_font(label, &egg_ui_font_12, 0);
    lv_obj_set_style_text_letter_space(label, 1, 0);
    lv_obj_center(label);

    title = lv_label_create(page_root);
    lv_label_set_text(title, "电机调试");
    lv_obj_set_style_text_font(title, &ui_motor_font_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, UI_DEBUG_TOP_ROW_Y + 5);

    ui_motor_create_header(page_root, "项目", UI_MOTOR_ROW_X, UI_MOTOR_LABEL_W);
    for(motor_index = 0; motor_index < UI_MOTOR_COUNT; motor_index++) {
        ui_motor_create_header(page_root,
                               k_motor_names[motor_index],
                               UI_MOTOR_ROW_X + UI_MOTOR_LABEL_W + (lv_coord_t)motor_index * UI_MOTOR_COL_W,
                               UI_MOTOR_COL_W);
    }

    current_row = ui_motor_create_row(page_root, "电流", UI_MOTOR_LIST_Y);
    ui_motor_fill_row(current_row,
                      s_motor_debug.current_label,
                      UI_MOTOR_ACTION_CURRENT_DEC,
                      UI_MOTOR_ACTION_CURRENT_INC);

    micro_row = ui_motor_create_row(page_root,
                                    "细分",
                                    UI_MOTOR_LIST_Y + UI_MOTOR_ROW_H + UI_MOTOR_ROW_GAP);
    ui_motor_fill_row(micro_row,
                      s_motor_debug.micro_label,
                      UI_MOTOR_ACTION_MICRO_DEC,
                      UI_MOTOR_ACTION_MICRO_INC);

    ui_motor_update_labels();
    lv_obj_clear_flag(page_root, LV_OBJ_FLAG_HIDDEN);
}
