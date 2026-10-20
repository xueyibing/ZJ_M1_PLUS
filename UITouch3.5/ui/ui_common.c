#include "ui_common.h"

void ui_reset_base_obj(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void ui_prepare_transparent_obj(lv_obj_t *obj)
{
    ui_reset_base_obj(obj);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
}

void ui_screen_reset(lv_obj_t *screen)
{
    lv_obj_clean(screen);
    lv_obj_remove_style_all(screen);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(UI_C_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    lv_obj_set_style_bg_image_src(screen, NULL, LV_PART_MAIN);
    lv_obj_set_style_bg_image_opa(screen, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
}

void ui_dialog_style_overlay(lv_obj_t *overlay)
{
    ui_reset_base_obj(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(UI_C_BG), 0);
    lv_obj_set_style_bg_opa(overlay, UI_OVERLAY_BG_OPA, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_center(overlay);
}

void ui_dialog_style_card(lv_obj_t *card)
{
    ui_reset_base_obj(card);
    lv_obj_set_size(card, UI_DIALOG_CARD_W, UI_DIALOG_CARD_H);
    lv_obj_set_style_radius(card, UI_DIALOG_CARD_RADIUS, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_C_PANEL), 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_center(card);
    /* Native pixels: keep every centered notice exactly 10 px lower. */
    lv_obj_set_style_translate_y(card, UI_DIALOG_TRANSLATE_Y_PX, 0);
}

void ui_dialog_style_title(lv_obj_t *label)
{
    lv_obj_set_width(label, UI_DIALOG_CONTENT_W);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, &ui_notice_font_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_YELLOW), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(label, 2, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, UI_DIALOG_TITLE_TOP);
}

void ui_dialog_style_message_panel(lv_obj_t *panel)
{
    ui_reset_base_obj(panel);
    lv_obj_set_size(panel, UI_DIALOG_CONTENT_W, UI_DIALOG_MESSAGE_H);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, UI_DIALOG_MESSAGE_TOP);
}

void ui_dialog_style_message(lv_obj_t *label)
{
    lv_obj_set_width(label, UI_DIALOG_CONTENT_W);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, &ui_notice_font_18, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_MED), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(label, 2, 0);
    lv_obj_center(label);
}

void ui_dialog_style_button(lv_obj_t *button, lv_coord_t width, bool primary)
{
    ui_reset_base_obj(button);
    lv_obj_set_size(button, width, UI_DIALOG_BUTTON_H);
    lv_obj_set_style_radius(button, UI_DIALOG_BUTTON_RADIUS, 0);
    lv_obj_set_style_border_width(button, primary ? 0 : 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(UI_C_YELLOW), 0);
    lv_obj_set_style_border_opa(button, primary ? LV_OPA_TRANSP : LV_OPA_50, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button,
                              lv_color_hex(primary ? UI_C_PRIMARY_TOP : 0x202020), 0);
    lv_obj_set_style_bg_grad_color(button,
                                   lv_color_hex(primary ? UI_C_PRIMARY_BOTTOM : 0x202020), 0);
    lv_obj_set_style_bg_grad_dir(button,
                                 primary ? LV_GRAD_DIR_VER : LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, primary ? 10 : 0, 0);
    lv_obj_set_style_shadow_color(button, lv_color_hex(UI_C_PRIMARY_BOTTOM), 0);
    lv_obj_set_style_shadow_opa(button, primary ? LV_OPA_30 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_spread(button, 0, 0);
}

void ui_dialog_style_button_label(lv_obj_t *label, bool primary)
{
    lv_obj_set_style_text_font(label, &ui_notice_font_18, 0);
    lv_obj_set_style_text_color(label,
                                lv_color_hex(primary ? UI_C_BG : UI_C_TEXT_STRONG), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}
