#include "ui_alert_debug.h"

#include "buzzer.h"
#include "ui.h"
#include "ui_alert.h"
#include "ui_common.h"
#include "ui_debug.h"
#include "ui_debug_internal.h"
#include "ui_notice.h"
#include "ui_uart_bridge.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define UI_ALERT_DEBUG_STATUS_W      220
#define UI_ALERT_DEBUG_STATUS_H      30
#define UI_ALERT_DEBUG_STATUS_BOTTOM_Y (-10)
#define UI_ALERT_DEBUG_STATUS_TIMER_MS 500
#define UI_ALERT_DEBUG_COLS         3
#define UI_ALERT_DEBUG_GRID_W       296
#define UI_ALERT_DEBUG_GRID_TOP_Y   40
#define UI_ALERT_DEBUG_GRID_GAP_X   4
#define UI_ALERT_DEBUG_GRID_GAP_Y   4
#define UI_ALERT_DEBUG_BTN_W        96
#define UI_ALERT_DEBUG_BTN_H        24

typedef enum {
    UI_ALERT_DEBUG_SHORTCUT_NOTICE = 0,
    UI_ALERT_DEBUG_SHORTCUT_ALERT,
} ui_alert_debug_shortcut_kind_t;

typedef struct {
    ui_alert_debug_shortcut_kind_t kind;
    ui_error_code_t code;
    const char *label;
} ui_alert_debug_shortcut_t;

typedef struct {
    bool active;
    ui_data_t *data;
    lv_obj_t *status_box;
    lv_obj_t *status_label;
    lv_timer_t *status_timer;
} ui_alert_debug_view_t;

static ui_alert_debug_view_t s_alert_debug;

static const ui_alert_debug_shortcut_t k_shortcuts[] = {
    {UI_ALERT_DEBUG_SHORTCUT_NOTICE, UI_ERROR_UNKNOWN_ERROR, "通知页"},
    {UI_ALERT_DEBUG_SHORTCUT_ALERT, UI_ERROR_OIL_EMPTY, "油不足"},
    {UI_ALERT_DEBUG_SHORTCUT_ALERT, UI_ERROR_EGG_EMPTY, "未检鸡蛋"},
    {UI_ALERT_DEBUG_SHORTCUT_ALERT, UI_ERROR_BEAT_NOT_EMPTY, "打蛋未空"},
    {UI_ALERT_DEBUG_SHORTCUT_ALERT, UI_ERROR_POTA_TMP_SENSOR_ERROR, "A温感错"},
    {UI_ALERT_DEBUG_SHORTCUT_ALERT, UI_ERROR_POTB_TMP_SENSOR_ERROR, "B温感错"},
    {UI_ALERT_DEBUG_SHORTCUT_ALERT, UI_ERROR_POTA_NO, "A锅未检"},
    {UI_ALERT_DEBUG_SHORTCUT_ALERT, UI_ERROR_POTB_NO, "B锅未检"},
    {UI_ALERT_DEBUG_SHORTCUT_ALERT, UI_ERROR_POTA_NO_COIL, "A加热错"},
    {UI_ALERT_DEBUG_SHORTCUT_ALERT, UI_ERROR_POTB_NO_COIL, "B加热错"},
    {UI_ALERT_DEBUG_SHORTCUT_ALERT, UI_ERROR_BEAT_SHELL_NOT_CLEAN, "壳未清"},
    {UI_ALERT_DEBUG_SHORTCUT_ALERT, UI_ERROR_POTA_OUT_EGG_FAIL, "粘锅"},
    {UI_ALERT_DEBUG_SHORTCUT_ALERT, UI_ERROR_POTA_IIC_ERROR, "A通信错"},
    {UI_ALERT_DEBUG_SHORTCUT_ALERT, UI_ERROR_POTB_IIC_ERROR, "B通信错"},
    {UI_ALERT_DEBUG_SHORTCUT_ALERT, UI_ERROR_VL53_HAVE_ERROR, "打位测距"},
    {UI_ALERT_DEBUG_SHORTCUT_ALERT, UI_ERROR_VL53_HAVE_BLOCK, "打位遮挡"},
    {UI_ALERT_DEBUG_SHORTCUT_ALERT, UI_ERROR_VL53_POTA_ERROR, "A锅测距"},
    {UI_ALERT_DEBUG_SHORTCUT_ALERT, UI_ERROR_VL53_POTA_BLOCK, "A锅遮挡"},
    {UI_ALERT_DEBUG_SHORTCUT_ALERT, UI_ERROR_POTB_OVERHEAT, "B-OVHT"},
    {UI_ALERT_DEBUG_SHORTCUT_ALERT, UI_ERROR_POTA_OVERHEAT, "A-OVHT"},
    {UI_ALERT_DEBUG_SHORTCUT_ALERT, UI_ERROR_UNKNOWN_ERROR, "未知告警"},
};

static void ui_alert_debug_stop_status_timer(void)
{
    if(s_alert_debug.status_timer != NULL) {
        lv_timer_delete(s_alert_debug.status_timer);
        s_alert_debug.status_timer = NULL;
    }
}

static void ui_alert_debug_set_status_visible(bool visible)
{
    if(s_alert_debug.status_box == NULL) {
        return;
    }

    if(visible) {
        lv_obj_clear_flag(s_alert_debug.status_box, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_alert_debug.status_box, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ui_alert_debug_refresh_status(void)
{
    const ui_data_state_t *state;
    ui_uart_heartbeat_state_t hb;
    char text[128];

    if(!s_alert_debug.active || s_alert_debug.data == NULL || s_alert_debug.status_label == NULL) {
        return;
    }

    state = ui_data_get_state(s_alert_debug.data);
    ui_uart_bridge_get_heartbeat_state(&hb);

    if(!hb.online) {
        ui_alert_debug_set_status_visible(true);
        lv_label_set_text(s_alert_debug.status_label, "WAIT\nNO HEARTBEAT");
        return;
    }

    if(state != NULL && state->error_code != UI_ERROR_ALL_OK) {
        ui_alert_debug_set_status_visible(true);
        (void)snprintf(text,
                       sizeof(text),
                       "ERR E%02d / CHECK ALERT",
                       (int)state->error_code);
        lv_label_set_text(s_alert_debug.status_label, text);
        return;
    }

    ui_alert_debug_set_status_visible(false);
}

static void ui_alert_debug_status_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    ui_alert_debug_refresh_status();
}

static void ui_alert_debug_back_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    ui_alert_debug_stop_status_timer();
    s_alert_debug.active = false;
    ui_debug_create(s_alert_debug.data);
}

static void ui_alert_debug_shortcut_event_cb(lv_event_t *e)
{
    const ui_alert_debug_shortcut_t *shortcut =
        (const ui_alert_debug_shortcut_t *)lv_event_get_user_data(e);

    buzzer_beep_async(50);

    if(shortcut == NULL) {
        return;
    }

    ui_alert_debug_stop_status_timer();
    s_alert_debug.active = false;

    if(shortcut->kind == UI_ALERT_DEBUG_SHORTCUT_NOTICE) {
        ui_notice_create(s_alert_debug.data,
                         "提示",
                         "正在立即停止。动作完全停止后，请清理锅内鸡蛋并确认机构已回到安全位置，再点击确认复位。",
                         "确认");
        return;
    }

    ui_alert_create_preview(s_alert_debug.data, shortcut->code);
}

static void ui_alert_debug_create_grid(lv_obj_t *parent)
{
    lv_obj_t *grid = lv_obj_create(parent);
    const uint32_t shortcut_count = sizeof(k_shortcuts) / sizeof(k_shortcuts[0]);
    const uint32_t row_count = (shortcut_count + UI_ALERT_DEBUG_COLS - 1U) / UI_ALERT_DEBUG_COLS;
    const lv_coord_t grid_height = (lv_coord_t)(row_count * UI_ALERT_DEBUG_BTN_H +
                                  (row_count - 1U) * UI_ALERT_DEBUG_GRID_GAP_Y);

    ui_reset_base_obj(grid);
    lv_obj_set_size(grid, UI_ALERT_DEBUG_GRID_W, grid_height);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, UI_ALERT_DEBUG_GRID_GAP_Y, 0);
    lv_obj_set_style_pad_column(grid, UI_ALERT_DEBUG_GRID_GAP_X, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, UI_ALERT_DEBUG_GRID_TOP_Y);

    for(uint32_t i = 0; i < shortcut_count; i++) {
        lv_obj_t *button = lv_button_create(grid);
        lv_obj_t *label;
        const lv_font_t *font = k_shortcuts[i].kind == UI_ALERT_DEBUG_SHORTCUT_NOTICE ?
            &ui_alert_debug_font_16 : &ui_alert_font_16;

        ui_debug_style_button(button, UI_ALERT_DEBUG_BTN_W, UI_ALERT_DEBUG_BTN_H, 12);
        lv_obj_set_style_bg_opa(button, LV_OPA_10, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(UI_C_PANEL), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_20, LV_STATE_PRESSED);
        lv_obj_add_event_cb(button, ui_alert_debug_shortcut_event_cb, LV_EVENT_CLICKED, (void *)&k_shortcuts[i]);

        label = lv_label_create(button);
        lv_label_set_text(label, k_shortcuts[i].label);
        lv_obj_set_style_text_font(label, font, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_STRONG), 0);
        lv_obj_center(label);
    }
}

void ui_alert_debug_create(ui_data_t *data)
{
    const ui_data_state_t *state;
    lv_obj_t *screen;
    lv_obj_t *button;
    lv_obj_t *label;
    lv_obj_t *title;

    if(data == NULL) {
        return;
    }

    state = ui_data_get_state(data);
    if(ui_alert_should_show(state)) {
        ui_alert_create(data);
        return;
    }

    ui_debug_stop_serial_timer();
    ui_alert_debug_stop_status_timer();
    lv_memzero(&s_alert_debug, sizeof(s_alert_debug));
    s_alert_debug.active = true;
    s_alert_debug.data = data;
    s_debug.active = false;
    s_debug.heater_debug_active = false;

    screen = lv_screen_active();
    ui_screen_reset(screen);

    button = lv_button_create(screen);
    ui_debug_style_button(button, UI_DEBUG_BACK_BTN_W, UI_DEBUG_BACK_BTN_H, 13);
    lv_obj_align(button, LV_ALIGN_TOP_LEFT, UI_DEBUG_SIDE_MARGIN, UI_DEBUG_TOP_ROW_Y);
    lv_obj_add_event_cb(button, ui_alert_debug_back_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(button);
    lv_label_set_text(label, "BACK");
    lv_obj_set_style_text_font(label, &egg_ui_font_12, 0);
    lv_obj_set_style_text_letter_space(label, 1, 0);
    lv_obj_center(label);

    title = lv_label_create(screen);
    lv_label_set_text(title, "通知与告警调试");
    lv_obj_set_style_text_font(title, &ui_alert_debug_font_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, UI_DEBUG_TOP_ROW_Y + 5);

    ui_alert_debug_create_grid(screen);

    s_alert_debug.status_box = lv_obj_create(screen);
    ui_reset_base_obj(s_alert_debug.status_box);
    lv_obj_set_size(s_alert_debug.status_box, UI_ALERT_DEBUG_STATUS_W, UI_ALERT_DEBUG_STATUS_H);
    lv_obj_set_style_radius(s_alert_debug.status_box, 15, 0);
    lv_obj_set_style_bg_opa(s_alert_debug.status_box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_alert_debug.status_box, lv_color_hex(UI_C_PANEL), 0);
    lv_obj_set_style_border_width(s_alert_debug.status_box, 1, 0);
    lv_obj_set_style_border_color(s_alert_debug.status_box, lv_color_hex(UI_C_LINE), 0);
    lv_obj_set_style_pad_all(s_alert_debug.status_box, 0, 0);
    lv_obj_align(s_alert_debug.status_box, LV_ALIGN_BOTTOM_MID, 0, UI_ALERT_DEBUG_STATUS_BOTTOM_Y);
    lv_obj_add_flag(s_alert_debug.status_box, LV_OBJ_FLAG_HIDDEN);

    s_alert_debug.status_label = lv_label_create(s_alert_debug.status_box);
    lv_obj_set_width(s_alert_debug.status_label, UI_ALERT_DEBUG_STATUS_W - 20);
    lv_label_set_long_mode(s_alert_debug.status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_alert_debug.status_label, &ui_alert_font_16, 0);
    lv_obj_set_style_text_color(s_alert_debug.status_label, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_set_style_text_align(s_alert_debug.status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_alert_debug.status_label);
    ui_alert_debug_refresh_status();
    s_alert_debug.status_timer = lv_timer_create(ui_alert_debug_status_timer_cb,
                                                UI_ALERT_DEBUG_STATUS_TIMER_MS,
                                                NULL);
}

void ui_alert_debug_on_data_changed(void)
{
    const ui_data_state_t *state;

    if(!s_alert_debug.active || s_alert_debug.data == NULL) {
        return;
    }

    state = ui_data_get_state(s_alert_debug.data);
    if(ui_alert_should_show(state)) {
        ui_alert_debug_stop_status_timer();
        s_alert_debug.active = false;
        ui_alert_create(s_alert_debug.data);
        return;
    }

    ui_alert_debug_refresh_status();
}
