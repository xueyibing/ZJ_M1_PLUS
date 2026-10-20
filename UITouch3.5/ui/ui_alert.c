#include "ui_alert.h"

#include "ui.h"
#include "ui_common.h"
#include "ui_alert_debug.h"
#include "buzzer.h"
#include "ui_i18n.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define UI_ALERT_C_AMBER UI_C_WARNING
#define UI_ALERT_C_RED 0xE05C47
#define UI_ALERT_C_BUTTON_BG 0x181818

typedef struct
{
    ui_error_code_t code;
    uint32_t accent_color;
    const char *headline;
    const char *hint;
    const char *headline_en;
    const char *hint_en;
} ui_alert_spec_t;

typedef struct
{
    bool active;
    bool preview_mode;
    lv_obj_t *screen;
    lv_obj_t *headline_label;
    lv_obj_t *hint_label;
    lv_obj_t *icon_disc;
    lv_obj_t *icon_label;
    ui_data_t *data;
    ui_error_code_t preview_code;
} ui_alert_view_t;

static ui_alert_view_t s_alert;

static const ui_alert_spec_t k_alert_specs[] = {
    {UI_ERROR_OIL_EMPTY, UI_ALERT_C_AMBER, "食用油不足", "请补充食用油后重试", "Low Cooking Oil", "Refill oil and retry"},
    {UI_ERROR_EGG_EMPTY, UI_ALERT_C_AMBER, "未检测到鸡蛋", "请添加鸡蛋后重试", "No Egg Detected", "Add eggs and retry"},
    {UI_ERROR_BEAT_NOT_EMPTY, UI_ALERT_C_AMBER, "打蛋器未清空", "请清理残留蛋壳或异物", "Beater Not Empty", "Clear shells or debris"},
    {UI_ERROR_POTA_TMP_SENSOR_ERROR, UI_ALERT_C_RED, "A锅温度传感器异常", "请检查传感器和连线", "Pot A Temp Sensor Fault", "Check sensor and wiring"},
    {UI_ERROR_POTB_TMP_SENSOR_ERROR, UI_ALERT_C_RED, "B锅温度传感器异常", "请检查传感器和连线", "Pot B Temp Sensor Fault", "Check sensor and wiring"},
    {UI_ERROR_POTA_NO, UI_ALERT_C_RED, "A锅未检测到", "请重新放置A锅", "Pot A Missing", "Reseat Pot A"},
    {UI_ERROR_POTB_NO, UI_ALERT_C_RED, "B锅未检测到", "请重新放置B锅", "Pot B Missing", "Reseat Pot B"},
    {UI_ERROR_POTA_NO_COIL, UI_ALERT_C_RED, "A锅加热回路异常", "请检查A锅线圈和连接", "Pot A Heater Fault", "Check Pot A coil and link"},
    {UI_ERROR_POTB_NO_COIL, UI_ALERT_C_RED, "B锅加热回路异常", "请检查B锅线圈和连接", "Pot B Heater Fault", "Check Pot B coil and link"},
    {UI_ERROR_BEAT_SHELL_NOT_CLEAN, UI_ALERT_C_AMBER, "蛋壳未清理干净", "请清理打蛋区域后重试", "Shells Not Cleared", "Clean beater area and retry"},
    {UI_ERROR_POTA_OUT_EGG_FAIL, UI_ALERT_C_RED, "鸡蛋粘锅", "请清锅后调整油量和温度", "Egg Stuck", "Clean pot, adjust oil and heat"},
    {UI_ERROR_POTA_IIC_ERROR, UI_ALERT_C_RED, "A锅通信异常", "请检查A锅电磁炉通信线", "Pot A Comm Error", "Check Pot A cooker link"},
    {UI_ERROR_POTB_IIC_ERROR, UI_ALERT_C_RED, "B锅通信异常", "请检查B锅电磁炉通信线", "Pot B Comm Error", "Check Pot B cooker link"},
    {UI_ERROR_VL53_HAVE_ERROR, UI_ALERT_C_RED, "打蛋位测距异常", "请联系售后检查VL53传感器和供电", "Beater Range Fault", "Contact service: check VL53 and power"},
    {UI_ERROR_VL53_HAVE_BLOCK, UI_ALERT_C_AMBER, "打蛋位传感器被遮挡", "请清理镜头前异物或污渍", "Beater Sensor Blocked", "Clear lens obstruction"},
    {UI_ERROR_VL53_POTA_ERROR, UI_ALERT_C_RED, "A锅测距异常", "请联系售后检查A锅VL53传感器和供电", "Pot A Range Fault", "Contact service: check Pot A VL53 and power"},
    {UI_ERROR_VL53_POTA_BLOCK, UI_ALERT_C_AMBER, "A锅传感器被遮挡", "请清理镜头前异物或油污", "Pot A Sensor Blocked", "Clear lens obstruction or oil"},
    {UI_ERROR_POTB_OVERHEAT, UI_ALERT_C_RED, "B锅温度异常", "请检查锅温和加热", "Pot B Overheat", "Check pot temp and heating"},
    {UI_ERROR_POTA_OVERHEAT, UI_ALERT_C_RED, "A锅温度异常", "请检查锅温和加热", "Pot A Overheat", "Check pot temp and heating"},
    {UI_ERROR_MOTOR_DRIVER_ERROR, UI_ALERT_C_RED, "电机驱动异常", "请检查电机驱动器和连线", "Motor Driver Fault", "Check motor driver and wiring"},
    {UI_ERROR_UNKNOWN_ERROR, UI_ALERT_C_RED, "未知告警", "请重启设备或联系售后", "Unknown Alert", "Restart device or contact service"},
};

static const ui_alert_spec_t k_unknown_alert_spec = {
    UI_ERROR_UNKNOWN_ERROR,
    UI_ALERT_C_RED,
    "未知告警",
    "请重启设备或联系售后",
    "Unknown Alert",
    "Restart device or contact service"};

static const char *ui_alert_spec_headline(const ui_alert_spec_t *spec)
{
    return (ui_i18n_get_language() == UI_LANG_EN) ? spec->headline_en : spec->headline;
}

static const char *ui_alert_spec_hint(const ui_alert_spec_t *spec)
{
    return (ui_i18n_get_language() == UI_LANG_EN) ? spec->hint_en : spec->hint;
}


static const ui_data_state_t *ui_alert_get_state(void)
{
    if (s_alert.data == NULL)
    {
        return NULL;
    }

    return ui_data_get_state(s_alert.data);
}

static ui_error_code_t ui_alert_get_current_code(const ui_data_state_t *state)
{
    if(s_alert.preview_mode) {
        return s_alert.preview_code;
    }

    if(state == NULL) {
        return UI_ERROR_UNKNOWN_ERROR;
    }

    return state->error_code;
}

static const ui_alert_spec_t *ui_alert_get_spec(ui_error_code_t code)
{
    for (uint32_t i = 0; i < (sizeof(k_alert_specs) / sizeof(k_alert_specs[0])); i++)
    {
        if (k_alert_specs[i].code == code)
        {
            return &k_alert_specs[i];
        }
    }

    return &k_unknown_alert_spec;
}

const char *ui_alert_error_headline(ui_error_code_t code)
{
    return ui_alert_spec_headline(ui_alert_get_spec(code));
}

static void ui_alert_style_button(lv_obj_t *button, lv_coord_t width, lv_coord_t height,
                                  lv_coord_t radius, uint32_t accent_color)
{
    ui_reset_base_obj(button);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, radius, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(accent_color), 0);
    lv_obj_set_style_border_opa(button, LV_OPA_40, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_ALERT_C_BUTTON_BG), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_60, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, lv_color_hex(accent_color), LV_STATE_PRESSED);
}

static void ui_alert_apply_colors(const ui_alert_spec_t *spec)
{
    if (spec == NULL || s_alert.icon_disc == NULL || s_alert.icon_label == NULL)
    {
        return;
    }

    lv_obj_set_style_border_color(s_alert.icon_disc, lv_color_hex(spec->accent_color), 0);
    lv_obj_set_style_text_color(s_alert.icon_label, lv_color_hex(spec->accent_color), 0);
}

static void ui_alert_back_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_alarm_stop();
    buzzer_beep_async(50);
    s_alert.active = false;

    if(s_alert.preview_mode) {
        ui_alert_debug_create(s_alert.data);
        return;
    }

    ui_data_clear_error(s_alert.data);
    ui_create(s_alert.data);
}

bool ui_alert_should_show(const ui_data_state_t *state)
{
    return ui_data_state_has_alert(state);
}

static void ui_alert_refresh(void)
{
    const ui_data_state_t *state = ui_alert_get_state();
    const ui_alert_spec_t *spec;
    ui_error_code_t current_code;
    char hint_text[160];

    if (state == NULL || s_alert.screen == NULL)
    {
        return;
    }

    if (!s_alert.preview_mode && !ui_alert_should_show(state))
    {
        buzzer_alarm_stop();
        s_alert.active = false;
        ui_create(s_alert.data);
        return;
    }

    current_code = ui_alert_get_current_code(state);
    spec = ui_alert_get_spec(current_code);

    lv_label_set_text(s_alert.headline_label, ui_alert_spec_headline(spec));
    lv_obj_set_style_text_color(s_alert.headline_label, lv_color_hex(spec->accent_color), 0);
    (void)snprintf(hint_text,
                   sizeof(hint_text),
                   ui_t(UI_STR_ERR_CODE_FMT),
                   (int)current_code,
                   ui_alert_spec_hint(spec));
    lv_label_set_text(s_alert.hint_label, hint_text);

    ui_alert_apply_colors(spec);
}

void ui_alert_create(ui_data_t *data)
{
    const ui_data_state_t *state;
    const ui_alert_spec_t *spec;
    lv_obj_t *screen;
    lv_obj_t *button;
    lv_obj_t *label;

    if (data == NULL)
    {
        return;
    }

    state = ui_data_get_state(data);
    if (!ui_alert_should_show(state))
    {
        ui_create(data);
        return;
    }

    spec = ui_alert_get_spec(state->error_code);

    lv_memzero(&s_alert, sizeof(s_alert));

    s_alert.active = true;
    s_alert.preview_mode = false;
    s_alert.data = data;
    s_alert.preview_code = state->error_code;
    buzzer_alarm_start();
    screen = lv_screen_active();
    s_alert.screen = screen;

    ui_screen_reset(screen);

    /* ---- Alert icon: thin circle + "!" ---- */
    s_alert.icon_disc = lv_obj_create(screen);
    ui_prepare_transparent_obj(s_alert.icon_disc);
    lv_obj_set_size(s_alert.icon_disc, 44, 44);
    lv_obj_set_style_radius(s_alert.icon_disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_alert.icon_disc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_alert.icon_disc, 2, 0);
    lv_obj_align(s_alert.icon_disc, LV_ALIGN_TOP_MID, 0, 25);

    s_alert.icon_label = lv_label_create(s_alert.icon_disc);
    lv_label_set_text(s_alert.icon_label, "!");
    lv_obj_set_style_text_font(s_alert.icon_label, &egg_ui_font_16, 0);
    lv_obj_set_style_text_letter_space(s_alert.icon_label, 0, 0);
    lv_obj_set_style_transform_scale(s_alert.icon_label, 230, 0);
    lv_obj_center(s_alert.icon_label);

    /* ---- Headline ---- */
    s_alert.headline_label = lv_label_create(screen);
    lv_obj_set_width(s_alert.headline_label, 270);
    lv_label_set_long_mode(s_alert.headline_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_alert.headline_label, &ui_alert_font_16, 0);
    lv_obj_set_style_text_color(s_alert.headline_label, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_set_style_text_align(s_alert.headline_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(s_alert.headline_label, 6, 0);
    lv_obj_align(s_alert.headline_label, LV_ALIGN_TOP_MID, 0, 85);

    /* ---- Hint ---- */
    s_alert.hint_label = lv_label_create(screen);
    lv_obj_set_width(s_alert.hint_label, 260);
    lv_label_set_long_mode(s_alert.hint_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_alert.hint_label, &ui_alert_font_16, 0);
    lv_obj_set_style_text_color(s_alert.hint_label, lv_color_hex(UI_C_TEXT_DIM), 0);
    lv_obj_set_style_text_align(s_alert.hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_alert.hint_label, LV_ALIGN_BOTTOM_MID, 0, -65);

    /* ---- Back button ---- */
    button = lv_button_create(screen);
    ui_alert_style_button(button, 108, 34, 17, spec->accent_color);
    lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_add_event_cb(button, ui_alert_back_event_cb, LV_EVENT_CLICKED, NULL);

    label = lv_label_create(button);
    lv_label_set_text(label, ui_t(UI_STR_BACK_PLAIN));
    lv_obj_set_style_text_font(label, &ui_alert_font_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_center(label);

    ui_alert_refresh();
}

void ui_alert_create_preview(ui_data_t *data, ui_error_code_t code)
{
    const ui_alert_spec_t *spec;
    lv_obj_t *screen;
    lv_obj_t *button;
    lv_obj_t *label;

    if(data == NULL) {
        return;
    }

    spec = ui_alert_get_spec(code);

    lv_memzero(&s_alert, sizeof(s_alert));

    s_alert.active = true;
    s_alert.preview_mode = true;
    s_alert.data = data;
    s_alert.preview_code = code;
    screen = lv_screen_active();
    s_alert.screen = screen;

    ui_screen_reset(screen);

    s_alert.icon_disc = lv_obj_create(screen);
    ui_prepare_transparent_obj(s_alert.icon_disc);
    lv_obj_set_size(s_alert.icon_disc, 44, 44);
    lv_obj_set_style_radius(s_alert.icon_disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_alert.icon_disc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_alert.icon_disc, 2, 0);
    lv_obj_align(s_alert.icon_disc, LV_ALIGN_TOP_MID, 0, 25);

    s_alert.icon_label = lv_label_create(s_alert.icon_disc);
    lv_label_set_text(s_alert.icon_label, "!");
    lv_obj_set_style_text_font(s_alert.icon_label, &egg_ui_font_16, 0);
    lv_obj_set_style_text_letter_space(s_alert.icon_label, 0, 0);
    lv_obj_set_style_transform_scale(s_alert.icon_label, 230, 0);
    lv_obj_center(s_alert.icon_label);

    s_alert.headline_label = lv_label_create(screen);
    lv_obj_set_width(s_alert.headline_label, 270);
    lv_label_set_long_mode(s_alert.headline_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_alert.headline_label, &ui_alert_font_16, 0);
    lv_obj_set_style_text_color(s_alert.headline_label, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_set_style_text_align(s_alert.headline_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(s_alert.headline_label, 6, 0);
    lv_obj_align(s_alert.headline_label, LV_ALIGN_TOP_MID, 0, 85);

    s_alert.hint_label = lv_label_create(screen);
    lv_obj_set_width(s_alert.hint_label, 260);
    lv_label_set_long_mode(s_alert.hint_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_alert.hint_label, &ui_alert_font_16, 0);
    lv_obj_set_style_text_color(s_alert.hint_label, lv_color_hex(UI_C_TEXT_DIM), 0);
    lv_obj_set_style_text_align(s_alert.hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_alert.hint_label, LV_ALIGN_BOTTOM_MID, 0, -65);

    button = lv_button_create(screen);
    ui_alert_style_button(button, 108, 34, 17, spec->accent_color);
    lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_add_event_cb(button, ui_alert_back_event_cb, LV_EVENT_CLICKED, NULL);

    label = lv_label_create(button);
    lv_label_set_text(label, ui_t(UI_STR_BACK_PLAIN));
    lv_obj_set_style_text_font(label, &ui_alert_font_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_center(label);

    ui_alert_refresh();
}

void ui_alert_on_data_changed(void)
{
    if (!s_alert.active)
    {
        return;
    }

    ui_alert_refresh();
}
