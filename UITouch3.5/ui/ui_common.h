#pragma once

#include <stdbool.h>

#include "lvgl.h"
#include "gpio.h"

/* ---- Native 480x320 layout adapter --------------------------------------
 * The UI below is authored against a 320x240 canvas (the 2.8" panel).
 * On this 3.5" board LVGL renders natively at 480x320, so geometry passed
 * to the common LVGL setters is transparently scaled up. Font glyph sizes
 * are bitmap and stay as authored. Keep this block ahead of any UI code. */
static inline lv_coord_t ui_scale_coord(lv_coord_t value, lv_coord_t from, lv_coord_t to)
{
    int32_t v = (int32_t)value;

    if(LV_COORD_IS_SPEC(value)) {
        return value;
    }

    if(v > from || v < -from) {
        return value;
    }

    if(v >= 0) {
        return (lv_coord_t)((v * to + from / 2) / from);
    }

    return (lv_coord_t)(-(((-v) * to + from / 2) / from));
}

static inline lv_coord_t ui_scale_x(lv_coord_t value)
{
    return ui_scale_coord(value, UI_BASE_H_RES, UI_H_RES);
}

static inline lv_coord_t ui_scale_y(lv_coord_t value)
{
    return ui_scale_coord(value, UI_BASE_V_RES, UI_V_RES);
}

#define lv_obj_set_size(obj, w, h) \
    lv_obj_set_size((obj), ui_scale_x(w), ui_scale_y(h))
#define lv_obj_set_width(obj, w) \
    lv_obj_set_width((obj), ui_scale_x(w))
#define lv_obj_set_height(obj, h) \
    lv_obj_set_height((obj), ui_scale_y(h))
#define lv_obj_set_pos(obj, x, y) \
    lv_obj_set_pos((obj), ui_scale_x(x), ui_scale_y(y))
#define lv_obj_align(obj, align, x_ofs, y_ofs) \
    lv_obj_align((obj), (align), ui_scale_x(x_ofs), ui_scale_y(y_ofs))
#define lv_obj_align_to(obj, base, align, x_ofs, y_ofs) \
    lv_obj_align_to((obj), (base), (align), ui_scale_x(x_ofs), ui_scale_y(y_ofs))

/* ---- Shared color palette ------------------------------------------------ */
#define UI_C_BG          0x000000
#define UI_C_TEXT_STRONG 0xE8E8E8
#define UI_C_TEXT_MED    0xA8A8A8
#define UI_C_TEXT_DIM    0x5E5E5E
#define UI_C_LINE        0x2C2C2C
#define UI_C_PANEL       0x1A1A1A
#define UI_C_RED         0xFF4040
#define UI_C_ACCENT         0xF7C948
#define UI_C_PRIMARY_TOP    0xFBBF24
#define UI_C_PRIMARY_BOTTOM 0xF59E0B
#define UI_C_WARNING        0xF0B44C
/* Compatibility alias for existing status/text styles. */
#define UI_C_YELLOW UI_C_ACCENT

#define UI_OVERLAY_PANEL_H 200
#define UI_OVERLAY_BG_OPA  LV_OPA_50
#define UI_OVERLAY_TITLE_TOP 14
#define UI_OVERLAY_ACTION_BOTTOM 10
#define UI_OVERLAY_ACTION_H 42

/* ---- Shared centered notice-dialog geometry ----------------------------- */
#define UI_DIALOG_CARD_W          240
#define UI_DIALOG_CARD_H          180
#define UI_DIALOG_CONTENT_W       208
#define UI_DIALOG_TITLE_TOP       10
#define UI_DIALOG_MESSAGE_TOP     42
#define UI_DIALOG_MESSAGE_H       68
#define UI_DIALOG_BUTTON_W        100
#define UI_DIALOG_DOUBLE_BUTTON_W 84
#define UI_DIALOG_BUTTON_H        34
#define UI_DIALOG_BUTTON_BOTTOM   8
#define UI_DIALOG_BUTTON_RADIUS   17
#define UI_DIALOG_CARD_RADIUS     18
#define UI_DIALOG_TRANSLATE_Y_PX  10

/* ---- Shared fonts -------------------------------------------------------- */
LV_FONT_DECLARE(egg_ui_font_12);
LV_FONT_DECLARE(egg_ui_font_16);
LV_FONT_DECLARE(ui_alert_font_16);
LV_FONT_DECLARE(ui_debug_font_16);
LV_FONT_DECLARE(ui_alert_debug_font_16);
LV_FONT_DECLARE(ui_status_font_12);
LV_FONT_DECLARE(ui_setting_font_12);
LV_FONT_DECLARE(ui_brightness_font_12);
LV_FONT_DECLARE(ui_wifi_ssid_font_12);
LV_FONT_DECLARE(ui_motor_font_12);
LV_FONT_DECLARE(ui_motor_font_16);
LV_FONT_DECLARE(ui_move_debug_font_16);
LV_FONT_DECLARE(ui_sensor_debug_font_16);
LV_FONT_DECLARE(ui_shake_shell_font_16);
LV_FONT_DECLARE(ui_shake_egg_font_16);
LV_FONT_DECLARE(ui_func_font_14);
LV_FONT_DECLARE(ui_func_font_16);
LV_FONT_DECLARE(ui_func_font_28);
LV_FONT_DECLARE(ui_dump_mock_font_16);
LV_FONT_DECLARE(ui_notice_font_16);
LV_FONT_DECLARE(ui_notice_font_18);
LV_FONT_DECLARE(ui_clean_font_14);
LV_FONT_DECLARE(ui_init_dw_font_12);
LV_FONT_DECLARE(ui_laser_check_font_12);
LV_FONT_DECLARE(yahei);
LV_FONT_DECLARE(yahei_sm);
LV_FONT_DECLARE(ui_don_font_40);
LV_FONT_DECLARE(lv_font_montserrat_48);
LV_FONT_DECLARE(ui_egg_name_28);
LV_FONT_DECLARE(ui_egg_desc_13);
LV_FONT_DECLARE(ui_egg_name_38);
LV_FONT_DECLARE(ui_egg_desc_17);
LV_FONT_DECLARE(ui_egg_desc_19);
LV_FONT_DECLARE(ui_egg_btn_16);
LV_FONT_DECLARE(ui_egg_btn_20);
LV_FONT_DECLARE(ui_panel_20);
LV_FONT_DECLARE(ui_cook_font_20);

/* ---- Main-interface egg images (RGB565, 480x320) ------------------------- */
LV_IMAGE_DECLARE(egg_img_well);
LV_IMAGE_DECLARE(egg_img_easy);
LV_IMAGE_DECLARE(egg_img_sunny);
LV_IMAGE_DECLARE(egg_img_sunny_side);

/* ---- Shared LVGL helpers ------------------------------------------------- */

/* Strip all LVGL default styles and disable scrolling */
void ui_reset_base_obj(lv_obj_t *obj);

/* Reset base + transparent background + no border */
void ui_prepare_transparent_obj(lv_obj_t *obj);

/* Clean and reset the active screen to a plain black background */
void ui_screen_reset(lv_obj_t *screen);

/* Shared centered notice-dialog styling. These helpers keep custom dialogs
 * (for example Wi-Fi confirmation and screen lock) visually identical to the
 * global ui_notice component while allowing different interaction logic. */
void ui_dialog_style_overlay(lv_obj_t *overlay);
void ui_dialog_style_card(lv_obj_t *card);
void ui_dialog_style_title(lv_obj_t *label);
void ui_dialog_style_message_panel(lv_obj_t *panel);
void ui_dialog_style_message(lv_obj_t *label);
void ui_dialog_style_button(lv_obj_t *button, lv_coord_t width, bool primary);
void ui_dialog_style_button_label(lv_obj_t *label, bool primary);
