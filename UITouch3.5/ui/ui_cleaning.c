#include "ui_cleaning.h"

#include "ui_alert.h"
#include "ui_common.h"
#include "ui.h"
#include "ui_settings.h"
#include "ui_uart_bridge.h"
#include "ui_i18n.h"
#include "buzzer.h"
#include "lvgl_port.h"

#include <stdbool.h>
#include <stdint.h>

#define UI_CLEANING_HEADER_BTN_SIZE 32
#define UI_CLEANING_HEADER_BTN_Y    10
#define UI_CLEANING_PANEL_W         288
#define UI_CLEANING_PANEL_H         124
#define UI_CLEANING_PANEL_TOP_Y     64
#define UI_CLEANING_ACTION_BTN_W    110
#define UI_CLEANING_ACTION_BTN_H    34
#define UI_CLEANING_ACTION_LEFT_X   26
#define UI_CLEANING_ACTION_RIGHT_X  152
#define UI_CLEANING_ACTION_ROW_GAP  42
#define UI_CLEANING_UNLOCK_HOLD_MS  1000U
#define UI_CLEANING_DEFAULT_LONG_PRESS_MS 400U

typedef struct {
    bool active;
    bool locked;
    bool return_to_main;
    lv_obj_t *screen;
    lv_obj_t *lock_hint_label;
    lv_obj_t *lock_overlay;
    ui_data_t *data;
} ui_cleaning_view_t;

static ui_cleaning_view_t s_cleaning;

static void ui_cleaning_refresh(void);
static void ui_cleaning_set_locked(bool locked);

static void ui_cleaning_style_label(lv_obj_t *label, uint32_t color, lv_text_align_t align)
{
    lv_obj_set_style_text_font(label, &ui_setting_font_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
}

static void ui_cleaning_style_icon_button(lv_obj_t *button)
{
    ui_reset_base_obj(button);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x334155), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_60, LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(button, 6);
}

static void ui_cleaning_style_reserve_button(lv_obj_t *button)
{
    ui_reset_base_obj(button);
    lv_obj_set_style_radius(button, UI_CLEANING_ACTION_BTN_H / 2, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_C_PRIMARY_TOP), 0);
    lv_obj_set_style_bg_grad_color(button, lv_color_hex(UI_C_PRIMARY_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(button, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_width(button, 12, 0);
    lv_obj_set_style_shadow_color(button, lv_color_hex(UI_C_PRIMARY_BOTTOM), 0);
    lv_obj_set_style_shadow_opa(button, LV_OPA_70, 0);
    lv_obj_set_style_opa(button, LV_OPA_90, LV_STATE_PRESSED);
}

static const ui_data_state_t *ui_cleaning_get_state(void)
{
    if(s_cleaning.data == NULL) {
        return NULL;
    }

    return ui_data_get_state(s_cleaning.data);
}

static void ui_cleaning_set_locked(bool locked)
{
    if(s_cleaning.lock_overlay == NULL) {
        return;
    }

    s_cleaning.locked = locked;
    lvgl_port_set_touch_long_press_time(locked ? UI_CLEANING_UNLOCK_HOLD_MS
                                               : UI_CLEANING_DEFAULT_LONG_PRESS_MS);
    if(locked) {
        lv_obj_clear_flag(s_cleaning.lock_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_cleaning.lock_overlay);
    } else {
        lv_obj_add_flag(s_cleaning.lock_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ui_cleaning_back_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    lvgl_port_set_touch_long_press_time(UI_CLEANING_DEFAULT_LONG_PRESS_MS);
    s_cleaning.active = false;
    if(s_cleaning.return_to_main) {
        ui_create(s_cleaning.data);
    } else {
        ui_settings_create(s_cleaning.data);
    }
}

static void ui_cleaning_lock_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    ui_cleaning_set_locked(true);
}

static void ui_cleaning_unlock_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code != LV_EVENT_LONG_PRESSED) {
        return;
    }

    buzzer_beep_async(50);
    ui_cleaning_set_locked(false);
}

static void ui_cleaning_overlay_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
}

static void ui_cleaning_push_open_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    (void)ui_uart_bridge_send_raw_command("TST PUSHGO", false);
}

static void ui_cleaning_push_close_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    (void)ui_uart_bridge_send_raw_command("TST PUSHBACK", false);
}

static void ui_cleaning_give_oil_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    (void)ui_uart_bridge_send_raw_command("TST GIVEOIL", false);
}

static void ui_cleaning_give_egg_open_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    (void)ui_uart_bridge_send_raw_command("TST GIVEEGGOPEN", false);
}

static void ui_cleaning_give_egg_close_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    (void)ui_uart_bridge_send_raw_command("TST GIVEEGGCLOSE", false);
}

static void ui_cleaning_back_oil_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    (void)ui_uart_bridge_send_raw_command("TST BACKOIL", false);
}

static void ui_cleaning_create_action_button(lv_obj_t *parent,
                                                  const char *text,
                                                  lv_coord_t x,
                                                  lv_coord_t y,
                                                  lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_t *label;

    ui_cleaning_style_reserve_button(btn);
    lv_obj_set_size(btn, UI_CLEANING_ACTION_BTN_W, UI_CLEANING_ACTION_BTN_H);
    lv_obj_set_pos(btn, x, y);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    label = lv_label_create(btn);
    lv_label_set_text(label, text);
    ui_cleaning_style_label(label, 0x422006, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(label);
}

static void ui_cleaning_create_lock_overlay(lv_obj_t *screen)
{
    lv_obj_t *overlay;
    lv_obj_t *card;
    lv_obj_t *message_panel;
    lv_obj_t *label;
    lv_obj_t *button;

    overlay = lv_obj_create(screen);
    ui_dialog_style_overlay(overlay);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(overlay, ui_cleaning_overlay_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(overlay, ui_cleaning_overlay_event_cb, LV_EVENT_PRESSED, NULL);
    s_cleaning.lock_overlay = overlay;

    card = lv_obj_create(overlay);
    ui_dialog_style_card(card);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(card, ui_cleaning_unlock_cb, LV_EVENT_LONG_PRESSED, NULL);

    label = lv_label_create(card);
    lv_label_set_text(label, ui_t(UI_STR_SCREEN_LOCKED));
    ui_dialog_style_title(label);

    message_panel = lv_obj_create(card);
    ui_dialog_style_message_panel(message_panel);

    s_cleaning.lock_hint_label = lv_label_create(message_panel);
    lv_label_set_text(s_cleaning.lock_hint_label, ui_t(UI_STR_UNLOCK_HINT));
    ui_dialog_style_message(s_cleaning.lock_hint_label);

    button = lv_button_create(card);
    ui_dialog_style_button(button, UI_DIALOG_BUTTON_W, true);
    lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -UI_DIALOG_BUTTON_BOTTOM);
    lv_obj_add_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_ext_click_area(button, 18);
    lv_obj_add_event_cb(button, ui_cleaning_unlock_cb, LV_EVENT_LONG_PRESSED, NULL);

    label = lv_label_create(button);
    lv_label_set_text(label, ui_t(UI_STR_UNLOCK_SCREEN));
    ui_dialog_style_button_label(label, true);
}

static void ui_cleaning_refresh(void)
{
    const ui_data_state_t *state = ui_cleaning_get_state();

    if(state == NULL) {
        return;
    }

    if(ui_alert_should_show(state)) {
        lvgl_port_set_touch_long_press_time(UI_CLEANING_DEFAULT_LONG_PRESS_MS);
        s_cleaning.active = false;
        ui_alert_create(s_cleaning.data);
    }
}

static void ui_cleaning_create_internal(ui_data_t *data, bool return_to_main)
{
    const ui_data_state_t *state;
    lv_obj_t *screen;
    lv_obj_t *mask;
    lv_obj_t *sheet;
    lv_obj_t *panel;
    lv_obj_t *btn;
    lv_obj_t *label;

    if(data == NULL) {
        return;
    }

    state = ui_data_get_state(data);
    if(ui_alert_should_show(state)) {
        ui_alert_create(data);
        return;
    }

    lv_memzero(&s_cleaning, sizeof(s_cleaning));
    s_cleaning.active = true;
    s_cleaning.data = data;
    s_cleaning.return_to_main = return_to_main;

    screen = lv_screen_active();
    s_cleaning.screen = screen;

    /* 保留主页面作为背景，只在其上叠加暗色遮罩与底部清洁面板。 */
    mask = lv_obj_create(screen);
    ui_reset_base_obj(mask);
    lv_obj_set_size(mask, 480, 320);
    lv_obj_set_pos(mask, 0, 0);
    lv_obj_set_style_bg_color(mask, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(mask, UI_OVERLAY_BG_OPA, 0);
    lv_obj_set_style_border_width(mask, 0, 0);
    lv_obj_add_flag(mask, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(mask, ui_cleaning_back_cb, LV_EVENT_CLICKED, NULL);

    sheet = lv_obj_create(screen);
    ui_reset_base_obj(sheet);
    lv_obj_clear_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sheet, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(sheet, 480, UI_OVERLAY_PANEL_H);
    lv_obj_align(sheet, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(sheet, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sheet, 24, 0);
    lv_obj_set_style_border_width(sheet, 1, 0);
    lv_obj_set_style_border_color(sheet, lv_color_hex(UI_C_YELLOW), 0);
    lv_obj_set_style_border_opa(sheet, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(sheet, 0, 0);

    btn = lv_button_create(sheet);
    ui_cleaning_style_icon_button(btn);
    lv_obj_set_size(btn, UI_CLEANING_HEADER_BTN_SIZE, UI_CLEANING_HEADER_BTN_SIZE);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 12, UI_CLEANING_HEADER_BTN_Y);
    lv_obj_add_event_cb(btn, ui_cleaning_back_cb, LV_EVENT_CLICKED, NULL);

    label = lv_label_create(btn);
    lv_label_set_text(label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_center(label);

    label = lv_label_create(sheet);
    lv_label_set_text(label, ui_t(UI_STR_CLEAN_TITLE));
    ui_cleaning_style_label(label, UI_C_YELLOW, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_font(label, &ui_panel_20, 0);
    lv_obj_set_style_text_letter_space(label, 3, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, UI_OVERLAY_TITLE_TOP);

    label = lv_label_create(sheet);
    lv_label_set_text(label, ui_t(UI_STR_LOCK_SCREEN));
    ui_cleaning_style_label(label, UI_C_TEXT_STRONG, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_width(label, 84);
    lv_obj_set_style_text_decor(label, LV_TEXT_DECOR_UNDERLINE, 0);
    lv_obj_align(label, LV_ALIGN_TOP_RIGHT, -16, UI_OVERLAY_TITLE_TOP + 3);
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(label, 8);
    lv_obj_add_event_cb(label, ui_cleaning_lock_cb, LV_EVENT_CLICKED, NULL);

    panel = lv_obj_create(sheet);
    ui_prepare_transparent_obj(panel);
    lv_obj_set_size(panel, UI_CLEANING_PANEL_W, UI_CLEANING_PANEL_H);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, UI_CLEANING_PANEL_TOP_Y);

    ui_cleaning_create_action_button(panel,
                                          ui_t(UI_STR_PUSH_OPEN),
                                          UI_CLEANING_ACTION_LEFT_X,
                                          0,
                                          ui_cleaning_push_open_cb);
    ui_cleaning_create_action_button(panel,
                                          ui_t(UI_STR_PUSH_CLOSE),
                                          UI_CLEANING_ACTION_RIGHT_X,
                                          0,
                                          ui_cleaning_push_close_cb);
    ui_cleaning_create_action_button(panel,
                                          ui_t(UI_STR_ACT_GIVE_OIL),
                                          UI_CLEANING_ACTION_LEFT_X,
                                          UI_CLEANING_ACTION_ROW_GAP,
                                          ui_cleaning_give_oil_cb);
    ui_cleaning_create_action_button(panel,
                                          ui_t(UI_STR_ACT_BACK_OIL),
                                          UI_CLEANING_ACTION_RIGHT_X,
                                          UI_CLEANING_ACTION_ROW_GAP,
                                          ui_cleaning_back_oil_cb);
    ui_cleaning_create_action_button(panel,
                                          ui_t(UI_STR_EGG_GATE_OPEN),
                                          UI_CLEANING_ACTION_LEFT_X,
                                          UI_CLEANING_ACTION_ROW_GAP * 2,
                                          ui_cleaning_give_egg_open_cb);
    ui_cleaning_create_action_button(panel,
                                          ui_t(UI_STR_EGG_GATE_CLOSE),
                                          UI_CLEANING_ACTION_RIGHT_X,
                                          UI_CLEANING_ACTION_ROW_GAP * 2,
                                          ui_cleaning_give_egg_close_cb);

    lvgl_port_set_touch_long_press_time(UI_CLEANING_DEFAULT_LONG_PRESS_MS);
    ui_cleaning_create_lock_overlay(screen);
    ui_cleaning_set_locked(false);
}

void ui_cleaning_create(ui_data_t *data)
{
    ui_cleaning_create_internal(data, true);
}

void ui_cleaning_on_data_changed(void)
{
    if(!s_cleaning.active) {
        return;
    }

    ui_cleaning_refresh();
}
