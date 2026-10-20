#include "ui_move_debug.h"

#include "buzzer.h"
#include "ui_alert.h"
#include "ui_common.h"
#include "ui_debug.h"
#include "ui_debug_internal.h"
#include "ui_uart_bridge.h"

#include <stdbool.h>
#include <stddef.h>

#define UI_MOVE_BTN_W   90
#define UI_MOVE_BTN_H   22
#define UI_MOVE_COL_GAP 10
#define UI_MOVE_ROW_GAP 5
#define UI_MOVE_TOP_Y   54
#define UI_MOVE_LEFT_X  15
#define UI_MOVE_COL_H   (UI_MOVE_BTN_H * 4 + UI_MOVE_ROW_GAP * 3)

typedef struct {
    bool active;
    ui_data_t *data;
} ui_move_debug_view_t;

static ui_move_debug_view_t s_move_debug;

static void ui_move_debug_action_event_cb(lv_event_t *e)
{
    const char *command = (const char *)lv_event_get_user_data(e);

    if(command == NULL) {
        return;
    }

    buzzer_beep_async(50);
    (void)ui_uart_bridge_send_raw_command(command, false);
}

static void ui_move_debug_back_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    s_move_debug.active = false;
    ui_debug_create(s_move_debug.data);
}

static void ui_move_debug_fan_event_cb(lv_event_t *e)
{
    lv_obj_t *button = lv_event_get_target(e);
    const bool on = lv_obj_has_state(button, LV_STATE_CHECKED);

    buzzer_beep_async(30);
    /* 开关态直接决定发送显式开/关，避免盲翻转导致 UI 与实际不一致。 */
    (void)ui_uart_bridge_send_raw_command(on ? "TST FAN 255" : "TST FAN 0", false);
}

static lv_obj_t *ui_move_debug_create_column(lv_obj_t *parent, lv_coord_t x)
{
    lv_obj_t *list = lv_obj_create(parent);

    ui_prepare_transparent_obj(list);
    lv_obj_set_size(list, UI_MOVE_BTN_W, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_gap(list, UI_MOVE_ROW_GAP, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(list, LV_ALIGN_TOP_LEFT, x, UI_MOVE_TOP_Y);
    return list;
}

static void ui_move_debug_create_button(lv_obj_t *parent,
                                        const char *text,
                                        const char *command,
                                        const lv_font_t *font)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;

    ui_debug_style_button(button, UI_MOVE_BTN_W, UI_MOVE_BTN_H, 12);
    lv_obj_add_event_cb(button, ui_move_debug_action_event_cb, LV_EVENT_CLICKED, (void *)command);

    label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_letter_space(label, 1, 0);
    lv_obj_center(label);
}

/* 带状态的风扇开关：可勾选按钮，ON=黄底高亮，OFF=常态，占位与普通按钮一致。 */
static void ui_move_debug_create_fan_button(lv_obj_t *parent,
                                           const char *text,
                                           const lv_font_t *font)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;

    ui_debug_style_button(button, UI_MOVE_BTN_W, UI_MOVE_BTN_H, 12);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_C_YELLOW), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(button, lv_color_hex(UI_C_BG), LV_STATE_CHECKED);
    lv_obj_add_event_cb(button, ui_move_debug_fan_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_letter_space(label, 1, 0);
    lv_obj_center(label);
}

void ui_move_debug_create(ui_data_t *data)
{
    const ui_data_state_t *state;
    lv_obj_t *screen;
    lv_obj_t *page_root;
    lv_obj_t *button;
    lv_obj_t *label;
    lv_obj_t *title;
    lv_obj_t *col;

    if(data == NULL) {
        return;
    }

    state = ui_data_get_state(data);
    if(ui_alert_should_show(state)) {
        ui_alert_create(data);
        return;
    }

    s_move_debug.active = true;
    s_move_debug.data = data;

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
    lv_obj_add_event_cb(button, ui_move_debug_back_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(button);
    lv_label_set_text(label, "BACK");
    lv_obj_set_style_text_font(label, &egg_ui_font_12, 0);
    lv_obj_set_style_text_letter_space(label, 1, 0);
    lv_obj_center(label);

    title = lv_label_create(page_root);
    lv_label_set_text(title, "运动调试");
    lv_obj_set_style_text_font(title, &ui_move_debug_font_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, UI_DEBUG_TOP_ROW_Y + 5);

    col = ui_move_debug_create_column(page_root, UI_MOVE_LEFT_X);
    ui_move_debug_create_button(col, "HOME", "TST HOME", &egg_ui_font_16);
    ui_move_debug_create_button(col, "给蛋", "TST GIVEEGG", &egg_ui_font_16);
    ui_move_debug_create_button(col, "打蛋", "TST BEAT", &egg_ui_font_16);
    ui_move_debug_create_button(col, "打开推蛋器", "TST PUSHGO", &ui_setting_font_12);
    ui_move_debug_create_button(col, "打蛋推", "TST PUSHSTEP", &egg_ui_font_16);
    ui_move_debug_create_button(col, "打蛋回", "TST PUSHBACK", &egg_ui_font_16);
    ui_move_debug_create_button(col, "抖壳", "TST PUSHSHAKE", &ui_shake_shell_font_16);

    col = ui_move_debug_create_column(page_root, UI_MOVE_LEFT_X + (UI_MOVE_BTN_W + UI_MOVE_COL_GAP));
    ui_move_debug_create_button(col, "给油", "TST GIVEOIL", &egg_ui_font_16);
    ui_move_debug_create_button(col, "回油", "TST BACKOIL", &egg_ui_font_16);
    ui_move_debug_create_button(col, "舵机到A", "TST SERVO_A", &ui_setting_font_12);
    ui_move_debug_create_button(col, "舵机到B", "TST SERVO_B", &ui_setting_font_12);
    ui_move_debug_create_button(col, "舵机回零", "TST SERVO_ZERO", &ui_setting_font_12);    ui_move_debug_create_fan_button(col, "散热风扇", &ui_debug_font_16);
    col = ui_move_debug_create_column(page_root, UI_MOVE_LEFT_X + 2 * (UI_MOVE_BTN_W + UI_MOVE_COL_GAP));
    ui_move_debug_create_button(col, "翻A锅", "TST TURNA", &egg_ui_font_16);
    ui_move_debug_create_button(col, "翻B锅", "TST TURNB", &egg_ui_font_16);
    ui_move_debug_create_button(col, "转运到锅A", "TST GO_POTA", &ui_setting_font_12);
    ui_move_debug_create_button(col, "转运到锅B", "TST GO_POTB", &ui_setting_font_12);
    ui_move_debug_create_button(col, "抛壳模拟", "TST TRANS_DUMP", &ui_dump_mock_font_16);
    ui_move_debug_create_button(col, "二次抛壳", "TST TRANS_DUMP_RETRY", &ui_dump_mock_font_16);
    ui_move_debug_create_button(col, "抖蛋液", "TST SHAKE_EGG", &ui_shake_egg_font_16);


    lv_obj_clear_flag(page_root, LV_OBJ_FLAG_HIDDEN);
}
