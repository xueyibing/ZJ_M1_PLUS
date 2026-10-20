#include "ui.h"
#include "ui_home.h"
#include "ui_alert.h"
#include "ui_notice.h"
#include "ui_settings.h"
#include "ui_cleaning.h"
#include "ui_debug.h"
#include "ui_alert_debug.h"
#include "ui_uart_bridge.h"

static lv_obj_t *s_bypass_indicator;
static lv_obj_t *s_no_heat_indicator;

static void ui_update_bypass_indicator(void)
{
    ui_uart_heartbeat_state_t heartbeat;
    lv_obj_t *screen = lv_screen_active();

    ui_uart_bridge_get_heartbeat_state(&heartbeat);

    if(s_bypass_indicator == NULL || !lv_obj_is_valid(s_bypass_indicator) ||
       lv_obj_get_parent(s_bypass_indicator) != screen) {
        s_bypass_indicator = lv_label_create(screen);
        lv_label_set_text(s_bypass_indicator, "DEBUG");
        lv_obj_set_style_text_color(s_bypass_indicator, lv_color_white(), 0);
        lv_obj_set_style_bg_color(s_bypass_indicator, lv_color_hex(0xD84040), 0);
        lv_obj_set_style_bg_opa(s_bypass_indicator, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_bypass_indicator, 4, 0);
        lv_obj_set_style_pad_hor(s_bypass_indicator, 5, 0);
        lv_obj_set_style_pad_ver(s_bypass_indicator, 2, 0);
        lv_obj_align(s_bypass_indicator, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    }

    if(s_no_heat_indicator == NULL || !lv_obj_is_valid(s_no_heat_indicator) ||
       lv_obj_get_parent(s_no_heat_indicator) != screen) {
        s_no_heat_indicator = lv_label_create(screen);
        lv_label_set_text(s_no_heat_indicator, "NO HEAT");
        lv_obj_set_style_text_color(s_no_heat_indicator, lv_color_white(), 0);
        lv_obj_set_style_bg_color(s_no_heat_indicator, lv_color_hex(0xD84040), 0);
        lv_obj_set_style_bg_opa(s_no_heat_indicator, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_no_heat_indicator, 4, 0);
        lv_obj_set_style_pad_hor(s_no_heat_indicator, 5, 0);
        lv_obj_set_style_pad_ver(s_no_heat_indicator, 2, 0);
    }

    const bool bypass_active = heartbeat.online &&
                               heartbeat.has_remote_debug_bypass &&
                               heartbeat.remote_debug_bypass;
    const bool no_heat_active = heartbeat.online &&
                                heartbeat.has_remote_no_heat &&
                                heartbeat.remote_no_heat;

    if(bypass_active) {
        lv_obj_clear_flag(s_bypass_indicator, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_bypass_indicator);
    } else {
        lv_obj_add_flag(s_bypass_indicator, LV_OBJ_FLAG_HIDDEN);
    }

    if(no_heat_active) {
        lv_obj_align(s_no_heat_indicator, LV_ALIGN_BOTTOM_RIGHT, -4,
                     bypass_active ? -29 : -4);
        lv_obj_clear_flag(s_no_heat_indicator, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_no_heat_indicator);
    } else {
        lv_obj_add_flag(s_no_heat_indicator, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_create(ui_data_t *data)
{
    if(data == NULL) {
        return;
    }

    ui_home_create(data);
}

void ui_on_data_changed(void)
{
    ui_alert_on_data_changed();
    ui_notice_on_data_changed();
    ui_settings_on_data_changed();
    ui_cleaning_on_data_changed();
    ui_debug_on_data_changed();
    ui_alert_debug_on_data_changed();

    ui_home_on_data_changed();
    ui_update_bypass_indicator();
}

void ui_on_debug_state_changed(void)
{
    ui_update_bypass_indicator();
}
