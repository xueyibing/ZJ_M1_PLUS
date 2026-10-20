#include "ui_settings.h"
#include "ui_alert.h"
#include "ui_common.h"
#include "ui.h"
#include "ui_i18n.h"
#include "ui_uart_bridge.h"
#include "ui_wifi.h"
#include "buzzer.h"
#include "data_manager.h"
#include "lcd.h"
#include "wifi_manager.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define OFFSET_COUNT 5
#define OFFSET_OIL    0
#define OFFSET_DON    1
#define OFFSET_POWER_FIRST  2
#define OFFSET_POWER_SECOND 3
#define OFFSET_INIT_DW 4
#define CONTROL_ROW_COUNT (OFFSET_COUNT)

#define UI_SETTINGS_PANEL_WIDTH    288
#define UI_SETTINGS_ROW_HEIGHT     34
#define UI_SETTINGS_FONT_H         16
#define UI_SETTINGS_BTN_SIZE       26
#define UI_SETTINGS_HEADER_HEIGHT  40
#define UI_SETTINGS_CONTENT_GAP    8
#define UI_SETTINGS_HEADER_BTN_Y   5
#define UI_SETTINGS_HEADER_BTN_H   30
#define UI_SETTINGS_HEADER_TEXT_Y  8
#define UI_SETTINGS_EGG_PANEL_TOP_Y 22
#define UI_SETTINGS_SECTION_Y      \
    (UI_SETTINGS_EGG_PANEL_TOP_Y + CONTROL_ROW_COUNT * UI_SETTINGS_ROW_HEIGHT + 4)
#define UI_SETTINGS_SYSTEM_TITLE_Y (UI_SETTINGS_SECTION_Y + 6)
#define UI_SETTINGS_SYSTEM_TOP_Y   (UI_SETTINGS_SYSTEM_TITLE_Y + 24)
#define UI_SETTINGS_GROUP_LINE_W   90
#define UI_SETTINGS_GROUP_SIDE_PAD 30
#define UI_SETTINGS_GROUP_LINE_Y   7
#define UI_SETTINGS_SYSTEM_ROW_H   34
#define UI_SETTINGS_SEG_BTN_W      58
#define UI_SETTINGS_SEG_BTN_H      26
#define UI_SETTINGS_LANG_COUNT     2
#define UI_SETTINGS_SCROLL_TOP_PX  50
#define UI_SETTINGS_SCROLL_H_PX    270
#define UI_SETTINGS_BRIGHTNESS_MIN 20
#define UI_SETTINGS_BRIGHTNESS_MAX 100
#define UI_SETTINGS_BRIGHTNESS_STEP 10

typedef struct {
    bool active;
    lv_obj_t *screen;
    lv_obj_t *value_labels[OFFSET_COUNT];
    lv_obj_t *minus_btns[OFFSET_COUNT];
    lv_obj_t *plus_btns[OFFSET_COUNT];
    lv_obj_t *lang_buttons[UI_SETTINGS_LANG_COUNT];
    lv_obj_t *laser_check_switch;
    lv_obj_t *second_oil_switch;
    lv_obj_t *wifi_status_label;
    lv_obj_t *brightness_label;
    lv_timer_t *wifi_timer;
    ui_language_t language;
    bool laser_check_enabled;
    bool second_oil_enabled;
    uint8_t brightness;
    ui_data_t *data;
} ui_settings_view_t;

static ui_settings_view_t s_setting;

static const int s_default_offsets[OFFSET_COUNT] = {100, 100, 100, 100, 5};
static int  s_offsets[OFFSET_COUNT] = {100, 100, 100, 100, 5};
static const int  s_steps[OFFSET_COUNT]  = {10, 10, 10, 10, 1};
static const int  s_mins[OFFSET_COUNT]   = {60, 60, 50, 50, 4};
static const int  s_maxs[OFFSET_COUNT]   = {200, 200, 500, 500, 8};
static const char *s_cmds[OFFSET_COUNT]  = {"OLO", "DNO", "PWO1", "PWO2", "DWO"};
static const bool s_is_pct[OFFSET_COUNT] = {true, true, true, true, false};
static const ui_str_key_t s_row_keys[OFFSET_COUNT] = {
    UI_STR_OIL_OFFSET, UI_STR_DON_OFFSET, UI_STR_POWER_FIRST,
    UI_STR_POWER_SECOND, UI_STR_INIT_LEVEL
};

static void ui_settings_refresh(void);
static int ui_settings_clamp_offset(int idx, int value);
static void ui_settings_update_label(int idx);
static void ui_settings_update_laser_switch(void);
static void ui_settings_update_second_oil_switch(void);
static void ui_settings_style_label(lv_obj_t *label, uint32_t color,
                                           lv_text_align_t align);

static void ui_settings_deactivate(void)
{
    s_setting.active = false;
    if(s_setting.wifi_timer != NULL) {
        lv_timer_delete(s_setting.wifi_timer);
        s_setting.wifi_timer = NULL;
    }
}

static const char *ui_settings_wifi_status_text(
    const wifi_manager_snapshot_t *snapshot)
{
    if(!snapshot->enabled) {
        return ui_t(UI_STR_WIFI_OFF);
    }
    switch(snapshot->state) {
    case WIFI_MANAGER_STATE_SCANNING:
        return ui_t(UI_STR_WIFI_SCANNING);
    case WIFI_MANAGER_STATE_CONNECTING:
    case WIFI_MANAGER_STATE_WAITING_IP:
    case WIFI_MANAGER_STATE_RETRY_WAIT:
        return ui_t(UI_STR_WIFI_CONNECTING);
    case WIFI_MANAGER_STATE_CONNECTED:
        return ui_t(UI_STR_WIFI_CONNECTED);
    case WIFI_MANAGER_STATE_FAILED:
        return ui_t(UI_STR_WIFI_FAILED);
    default:
        return snapshot->has_saved_network ? ui_t(UI_STR_WIFI_DISCONNECTED)
                                           : ui_t(UI_STR_WIFI_NOT_CONFIGURED);
    }
}

static void ui_settings_update_wifi_status(void)
{
    wifi_manager_snapshot_t snapshot;

    if(s_setting.wifi_status_label == NULL) {
        return;
    }
    wifi_manager_get_snapshot(&snapshot);
    lv_label_set_text_fmt(s_setting.wifi_status_label, "%s  >",
                          ui_settings_wifi_status_text(&snapshot));
}

static void ui_settings_wifi_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    if(s_setting.active) {
        ui_settings_update_wifi_status();
    }
}

static void ui_settings_update_segments(void)
{
    for(int i = 0; i < UI_SETTINGS_LANG_COUNT; i++) {
        lv_obj_t *btn = s_setting.lang_buttons[i];
        lv_obj_t *label;
        const bool selected = ((ui_language_t)i == s_setting.language);

        if(btn == NULL) {
            continue;
        }
        lv_obj_set_style_bg_opa(btn, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        if(selected) {
            lv_obj_set_style_bg_color(btn, lv_color_hex(UI_C_YELLOW), 0);
        }
        label = lv_obj_get_child(btn, 0);
        if(label != NULL) {
            lv_obj_set_style_text_color(label,
                                        lv_color_hex(selected ? UI_C_BG : UI_C_TEXT_MED), 0);
        }
    }
}

static void ui_settings_update_laser_switch(void)
{
    if(s_setting.laser_check_switch == NULL) {
        return;
    }
    if(s_setting.laser_check_enabled) {
        lv_obj_add_state(s_setting.laser_check_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(s_setting.laser_check_switch, LV_STATE_CHECKED);
    }
}

static void ui_settings_update_second_oil_switch(void)
{
    if(s_setting.second_oil_switch == NULL) {
        return;
    }
    if(s_setting.second_oil_enabled) {
        lv_obj_add_state(s_setting.second_oil_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(s_setting.second_oil_switch, LV_STATE_CHECKED);
    }
}

static bool ui_settings_is_default_offset(int idx)
{
    if(idx < 0 || idx >= OFFSET_COUNT) {
        return true;
    }

    return s_offsets[idx] == s_default_offsets[idx];
}

static void ui_settings_sync_offsets_from_remote(void)
{
    ui_uart_heartbeat_state_t hb_state;
    int i;

    ui_uart_bridge_get_heartbeat_state(&hb_state);
    if(!hb_state.has_remote_offsets) {
        return;
    }

    s_offsets[OFFSET_OIL]   = hb_state.remote_olo;
    s_offsets[OFFSET_DON]   = hb_state.remote_dno;
    s_offsets[OFFSET_POWER_FIRST] = hb_state.remote_power_offset_first;
    s_offsets[OFFSET_POWER_SECOND] = hb_state.remote_power_offset_second;
    s_offsets[OFFSET_INIT_DW] = hb_state.remote_dw_adjust;
    s_setting.laser_check_enabled = hb_state.remote_laser_check;
    s_setting.second_oil_enabled = hb_state.remote_second_oil;
    ui_settings_update_laser_switch();
    ui_settings_update_second_oil_switch();

    for(i = 0; i < OFFSET_COUNT; i++) {
        s_offsets[i] = ui_settings_clamp_offset(i, s_offsets[i]);
        ui_settings_update_label(i);
    }
}

/* ---- helpers ------------------------------------------------------------ */

static lv_obj_t *ui_settings_create_separator(lv_obj_t *parent, lv_coord_t width,
                                             lv_coord_t height, uint32_t color)
{
    lv_obj_t *sep = lv_obj_create(parent);
    ui_reset_base_obj(sep);
    lv_obj_set_size(sep, width, height);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sep, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    return sep;
}

static void ui_settings_create_group_header(lv_obj_t *parent,
                                                   const char *title,
                                                   lv_coord_t y)
{
    lv_obj_t *line;
    lv_obj_t *label;

    line = ui_settings_create_separator(parent, UI_SETTINGS_GROUP_LINE_W, 1, UI_C_LINE);
    lv_obj_set_style_bg_opa(line, LV_OPA_70, 0);
    lv_obj_align(line, LV_ALIGN_TOP_LEFT, UI_SETTINGS_GROUP_SIDE_PAD,
                 y + UI_SETTINGS_GROUP_LINE_Y);

    label = lv_label_create(parent);
    lv_label_set_text(label, title);
    ui_settings_style_label(label, UI_C_TEXT_DIM, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_opa(label, LV_OPA_80, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);

    line = ui_settings_create_separator(parent, UI_SETTINGS_GROUP_LINE_W, 1, UI_C_LINE);
    lv_obj_set_style_bg_opa(line, LV_OPA_70, 0);
    lv_obj_align(line, LV_ALIGN_TOP_RIGHT, -UI_SETTINGS_GROUP_SIDE_PAD,
                 y + UI_SETTINGS_GROUP_LINE_Y);
}

static void ui_settings_style_label(lv_obj_t *label, uint32_t color, lv_text_align_t align)
{
    lv_obj_set_style_text_font(label, &ui_setting_font_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
}

static void ui_settings_style_button(lv_obj_t *button, lv_coord_t radius)
{
    ui_reset_base_obj(button);
    lv_obj_set_style_radius(button, radius, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_10, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_C_PANEL), LV_STATE_PRESSED);
}

static const ui_data_state_t *ui_settings_get_state(void)
{
    if(s_setting.data == NULL) {
        return NULL;
    }
    return ui_data_get_state(s_setting.data);
}

/* 煎蛋进行中（已 start、PreHeating..MAKING 范围）禁止调整油量/熟度等偏移，
   避免 calOilTime/calHeatToalTime 实时读取时出现双锅时长不一致。 */
static bool ui_settings_can_adjust_offsets(void)
{
    const ui_data_state_t *state = ui_settings_get_state();

    if(state == NULL || ui_data_state_has_alert(state)) {
        return false;
    }
    /* 只允许在完全空闲时（未 start 且 total_s==0）调整。
       - idle/已取消完成（total_s>0）视为可调回默认；
         但取消后用户应进入设置调整，不属于"运行中"，故允许；
       - 真正禁用的是"运行中"。 */
    return ui_data_state_is_idle(state);
}

static void ui_settings_apply_button_state(void)
{
    bool can_adjust = ui_settings_can_adjust_offsets();

    for(int i = 0; i < OFFSET_COUNT; i++) {
        if(s_setting.minus_btns[i] != NULL) {
            lv_obj_set_style_opa(s_setting.minus_btns[i],
                                 can_adjust ? LV_OPA_COVER : LV_OPA_60, 0);
            if(can_adjust) {
                lv_obj_remove_state(s_setting.minus_btns[i], LV_STATE_DISABLED);
            } else {
                lv_obj_add_state(s_setting.minus_btns[i], LV_STATE_DISABLED);
            }
        }
        if(s_setting.plus_btns[i] != NULL) {
            lv_obj_set_style_opa(s_setting.plus_btns[i],
                                 can_adjust ? LV_OPA_COVER : LV_OPA_60, 0);
            if(can_adjust) {
                lv_obj_remove_state(s_setting.plus_btns[i], LV_STATE_DISABLED);
            } else {
                lv_obj_add_state(s_setting.plus_btns[i], LV_STATE_DISABLED);
            }
        }
    }
}

static int ui_settings_clamp_offset(int idx, int value)
{
    if(idx < 0 || idx >= OFFSET_COUNT) {
        return value;
    }
    if(value < s_mins[idx]) {
        return s_mins[idx];
    }
    if(value > s_maxs[idx]) {
        return s_maxs[idx];
    }
    return value;
}

static void ui_settings_update_label(int idx)
{
    uint32_t color;

    if(idx < 0 || idx >= OFFSET_COUNT || s_setting.value_labels[idx] == NULL) {
        return;
    }

    if(s_is_pct[idx]) {
        lv_label_set_text_fmt(s_setting.value_labels[idx], "%d%%", s_offsets[idx]);
    } else {
        lv_label_set_text_fmt(s_setting.value_labels[idx], "%d", s_offsets[idx]);
    }

    color = ui_settings_is_default_offset(idx) ? UI_C_TEXT_STRONG : UI_C_YELLOW;
    lv_obj_set_style_text_color(s_setting.value_labels[idx], lv_color_hex(color), 0);
}

static void ui_settings_send_offset(int idx)
{
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "%s %d", s_cmds[idx], s_offsets[idx]);
    ui_uart_bridge_send_raw_command(cmd, true);
}

/* ---- callbacks ---------------------------------------------------------- */

static void ui_settings_back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    buzzer_beep_async(50);
    ui_settings_deactivate();
    ui_create(s_setting.data);
}

static void ui_settings_language_cb(lv_event_t *e)
{
    const ui_language_t language =
        (ui_language_t)(intptr_t)lv_event_get_user_data(e);

    if(language == s_setting.language) {
        return;
    }
    buzzer_beep_async(50);
    if(!ui_i18n_set_language(language)) {
        return;
    }
    ui_settings_deactivate();
    ui_settings_create(s_setting.data);
}

static void ui_settings_wifi_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    buzzer_beep_async(50);
    ui_settings_deactivate();
    ui_wifi_create(s_setting.data);
}

static void ui_settings_laser_check_cb(lv_event_t *e)
{
    lv_obj_t *switch_obj = lv_event_get_target(e);
    const bool enabled = lv_obj_has_state(switch_obj, LV_STATE_CHECKED);
    char command[12];

    (void)snprintf(command, sizeof(command), "LSR %d", enabled ? 1 : 0);
    if(ui_uart_bridge_send_raw_command(command, true) != ESP_OK) {
        if(enabled) {
            lv_obj_remove_state(switch_obj, LV_STATE_CHECKED);
        } else {
            lv_obj_add_state(switch_obj, LV_STATE_CHECKED);
        }
        return;
    }
    s_setting.laser_check_enabled = enabled;
    buzzer_beep_async(50);
}

static void ui_settings_second_oil_cb(lv_event_t *e)
{
    lv_obj_t *switch_obj = lv_event_get_target(e);
    const bool enabled = lv_obj_has_state(switch_obj, LV_STATE_CHECKED);
    char command[12];

    (void)snprintf(command, sizeof(command), "OIL2 %d", enabled ? 1 : 0);
    if(ui_uart_bridge_send_raw_command(command, true) != ESP_OK) {
        if(enabled) {
            lv_obj_remove_state(switch_obj, LV_STATE_CHECKED);
        } else {
            lv_obj_add_state(switch_obj, LV_STATE_CHECKED);
        }
        return;
    }
    s_setting.second_oil_enabled = enabled;
    buzzer_beep_async(50);
}

static void ui_settings_update_brightness_label(void)
{
    if(s_setting.brightness_label != NULL) {
        lv_label_set_text_fmt(s_setting.brightness_label,
                              "%u%%", (unsigned)s_setting.brightness);
    }
}

static void ui_settings_brightness_cb(lv_event_t *e)
{
    const int delta = (int)(intptr_t)lv_event_get_user_data(e);
    int next = (int)s_setting.brightness + delta * UI_SETTINGS_BRIGHTNESS_STEP;

    if(next < UI_SETTINGS_BRIGHTNESS_MIN) {
        next = UI_SETTINGS_BRIGHTNESS_MIN;
    } else if(next > UI_SETTINGS_BRIGHTNESS_MAX) {
        next = UI_SETTINGS_BRIGHTNESS_MAX;
    }
    if(next == s_setting.brightness) {
        return;
    }

    if(data_manager_set_brightness((uint8_t)next) != ESP_OK) {
        buzzer_beep_async(120);
        return;
    }

    s_setting.brightness = (uint8_t)next;
    lcd_set_brightness(s_setting.brightness);
    ui_settings_update_brightness_label();
    buzzer_beep_async(40);
}

static void ui_settings_minus_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    int next;
    if(idx < 0 || idx >= OFFSET_COUNT) return;
    buzzer_beep_async(50);
    next = ui_settings_clamp_offset(idx, s_offsets[idx] - s_steps[idx]);
    if(next != s_offsets[idx]) {
        s_offsets[idx] = next;
        ui_settings_update_label(idx);
        ui_settings_send_offset(idx);
    }
}

static void ui_settings_plus_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    int next;
    if(idx < 0 || idx >= OFFSET_COUNT) return;
    buzzer_beep_async(50);
    next = ui_settings_clamp_offset(idx, s_offsets[idx] + s_steps[idx]);
    if(next != s_offsets[idx]) {
        s_offsets[idx] = next;
        ui_settings_update_label(idx);
        ui_settings_send_offset(idx);
    }
}

/* ---- refresh (alert check only) ---------------------------------------- */

static void ui_settings_refresh(void)
{
    const ui_data_state_t *state = ui_settings_get_state();
    if(state == NULL) return;

    /* 煎蛋中或告警时禁用所有偏移按钮 */
    ui_settings_apply_button_state();

    if(ui_alert_should_show(state)) {
        ui_settings_deactivate();
        ui_alert_create(s_setting.data);
    }
}

/* ---- create ------------------------------------------------------------- */

void ui_settings_create(ui_data_t *data)
{
    lv_obj_t *screen;
    lv_obj_t *content;
    lv_obj_t *panel;
    lv_obj_t *label;
    lv_obj_t *btn;
    lv_obj_t *sep;
    const ui_data_state_t *state;
    int i;

    if(data == NULL) return;

    state = ui_data_get_state(data);
    if(ui_alert_should_show(state)) {
        ui_alert_create(data);
        return;
    }

    lv_memzero(&s_setting, sizeof(s_setting));
    s_setting.active = true;
    s_setting.data   = data;
    s_setting.language = ui_i18n_get_language();
    s_setting.brightness = data_manager_get_brightness();
    s_setting.second_oil_enabled = true;

    /* sync offsets from cached remote settings state */
    {
        ui_uart_heartbeat_state_t hb_state;
        ui_uart_bridge_get_heartbeat_state(&hb_state);
        if(hb_state.has_remote_offsets) {
            s_offsets[OFFSET_OIL]   = hb_state.remote_olo;
            s_offsets[OFFSET_DON]   = hb_state.remote_dno;
            s_offsets[OFFSET_POWER_FIRST] = hb_state.remote_power_offset_first;
            s_offsets[OFFSET_POWER_SECOND] = hb_state.remote_power_offset_second;
            s_offsets[OFFSET_INIT_DW] = hb_state.remote_dw_adjust;
            s_setting.laser_check_enabled = hb_state.remote_laser_check;
            s_setting.second_oil_enabled = hb_state.remote_second_oil;
        }
    }
    for(i = 0; i < OFFSET_COUNT; i++) {
        s_offsets[i] = ui_settings_clamp_offset(i, s_offsets[i]);
    }

    screen = lv_screen_active();
    s_setting.screen = screen;
    ui_screen_reset(screen);

    /* ---- Top bar: back button ---- */
    btn = lv_button_create(screen);
    ui_settings_style_button(btn, 12);
    lv_obj_set_size(btn, 60, UI_SETTINGS_HEADER_BTN_H);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 8, UI_SETTINGS_HEADER_BTN_Y);
    lv_obj_add_event_cb(btn, ui_settings_back_cb, LV_EVENT_CLICKED, NULL);

    label = lv_label_create(btn);
    lv_label_set_text(label, ui_t(UI_STR_BACK));
    ui_settings_style_label(label, UI_C_TEXT_MED, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(label);

    /* ---- Top bar: title ---- */
    label = lv_label_create(screen);
    lv_label_set_text(label, u8"设置");
    ui_settings_style_label(label, UI_C_TEXT_STRONG, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_font(label, &ui_notice_font_16, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, UI_SETTINGS_HEADER_TEXT_Y);

    /* Fixed header + vertically scrollable settings content. Native-pixel
     * geometry keeps the viewport exactly below the header on 480x320. */
    content = lv_obj_create(screen);
    ui_reset_base_obj(content);
    (lv_obj_set_size)(content, 480, UI_SETTINGS_SCROLL_H_PX);
    (lv_obj_set_pos)(content, 0, UI_SETTINGS_SCROLL_TOP_PX);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_ON);
    lv_obj_set_style_width(content, 5, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(content, LV_RADIUS_CIRCLE, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(content, lv_color_hex(UI_C_YELLOW), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(content, LV_OPA_70, LV_PART_SCROLLBAR);

    /* Section 1: subtle centered title with balanced divider lines. */
    ui_settings_create_group_header(content, ui_t(UI_STR_JIANDAN_TITLE), 0);

    /* ---- Main panel ---- */
    panel = lv_obj_create(content);
    ui_prepare_transparent_obj(panel);
    lv_obj_set_size(panel, UI_SETTINGS_PANEL_WIDTH, CONTROL_ROW_COUNT * UI_SETTINGS_ROW_HEIGHT);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, UI_SETTINGS_EGG_PANEL_TOP_Y);

    /* ---- Offset rows ---- */
    for(i = 0; i < OFFSET_COUNT; i++) {
        lv_coord_t row_y = i * UI_SETTINGS_ROW_HEIGHT;
        lv_coord_t btn_y = row_y + (UI_SETTINGS_ROW_HEIGHT - UI_SETTINGS_BTN_SIZE) / 2;
        lv_coord_t lbl_y = row_y + (UI_SETTINGS_ROW_HEIGHT - UI_SETTINGS_FONT_H) / 2;

        /* separator between rows */
        if(i > 0) {
            sep = ui_settings_create_separator(panel, UI_SETTINGS_PANEL_WIDTH - 28, 1, UI_C_LINE);
            lv_obj_set_style_bg_opa(sep, LV_OPA_70, 0);
            lv_obj_align(sep, LV_ALIGN_TOP_MID, 0, row_y);
        }

        /* row label */
        label = lv_label_create(panel);
        lv_label_set_text(label, ui_t(s_row_keys[i]));
        ui_settings_style_label(label, UI_C_TEXT_MED, LV_TEXT_ALIGN_LEFT);
        if(i == OFFSET_INIT_DW && ui_i18n_get_language() == UI_LANG_ZH) {
            lv_obj_set_style_text_font(label, &ui_init_dw_font_12, 0);
        }
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 14, lbl_y);

        /* minus button */
        btn = lv_button_create(panel);
        ui_settings_style_button(btn, 7);
        lv_obj_set_size(btn, UI_SETTINGS_BTN_SIZE, UI_SETTINGS_BTN_SIZE);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 148, btn_y);
        lv_obj_add_event_cb(btn, ui_settings_minus_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_setting.minus_btns[i] = btn;

        label = lv_label_create(btn);
        lv_label_set_text(label, "-");
        lv_obj_set_style_text_font(label, &egg_ui_font_16, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_MED), 0);
        lv_obj_center(label);

        /* value label */
        s_setting.value_labels[i] = lv_label_create(panel);
        ui_settings_style_label(s_setting.value_labels[i], UI_C_TEXT_STRONG, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(s_setting.value_labels[i], 52);
        lv_obj_align(s_setting.value_labels[i], LV_ALIGN_TOP_LEFT, 179, lbl_y);

        /* plus button */
        btn = lv_button_create(panel);
        ui_settings_style_button(btn, 7);
        lv_obj_set_size(btn, UI_SETTINGS_BTN_SIZE, UI_SETTINGS_BTN_SIZE);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 244, btn_y);
        lv_obj_add_event_cb(btn, ui_settings_plus_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_setting.plus_btns[i] = btn;

        label = lv_label_create(btn);
        lv_label_set_text(label, "+");
        lv_obj_set_style_text_font(label, &egg_ui_font_16, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_MED), 0);
        lv_obj_center(label);
    }

    /* set initial value labels */
    for(i = 0; i < OFFSET_COUNT; i++) {
        ui_settings_update_label(i);
    }

    /* Section 2: use the same restrained centered group divider. */
    ui_settings_create_group_header(content, ui_t(UI_STR_SYSTEM_TITLE),
                                           UI_SETTINGS_SYSTEM_TITLE_Y);

    /* System settings are merged into this page; the content viewport scrolls
     * vertically when these rows extend beyond the physical screen. */
    {
        static const char *const lang_texts[UI_SETTINGS_LANG_COUNT] = {
            u8"中文", "English"
        };
        lv_obj_t *system_panel = lv_obj_create(content);

        ui_prepare_transparent_obj(system_panel);
        lv_obj_set_size(system_panel,
                        UI_SETTINGS_PANEL_WIDTH,
                        5 * UI_SETTINGS_SYSTEM_ROW_H);
        lv_obj_set_style_pad_all(system_panel, 0, 0);
        lv_obj_align(system_panel, LV_ALIGN_TOP_MID, 0, UI_SETTINGS_SYSTEM_TOP_Y);

        label = lv_label_create(system_panel);
        lv_label_set_text(label, ui_t(UI_STR_LANGUAGE));
        ui_settings_style_label(label, UI_C_TEXT_MED, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 14,
                     (UI_SETTINGS_SYSTEM_ROW_H - UI_SETTINGS_FONT_H) / 2);

        for(i = 0; i < UI_SETTINGS_LANG_COUNT; i++) {
            const lv_coord_t seg_x =
                -14 - (lv_coord_t)(UI_SETTINGS_LANG_COUNT - 1 - i) *
                      (UI_SETTINGS_SEG_BTN_W + 6);

            btn = lv_button_create(system_panel);
            ui_reset_base_obj(btn);
            lv_obj_set_style_radius(btn, 6, 0);
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(UI_C_TEXT_MED), 0);
            lv_obj_set_style_border_opa(btn, LV_OPA_70, 0);
            lv_obj_set_size(btn, UI_SETTINGS_SEG_BTN_W, UI_SETTINGS_SEG_BTN_H);
            lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, seg_x,
                         (UI_SETTINGS_SYSTEM_ROW_H - UI_SETTINGS_SEG_BTN_H) / 2);
            lv_obj_add_event_cb(btn, ui_settings_language_cb,
                                LV_EVENT_CLICKED, (void *)(intptr_t)i);

            label = lv_label_create(btn);
            lv_label_set_text(label, lang_texts[i]);
            ui_settings_style_label(label, UI_C_TEXT_MED, LV_TEXT_ALIGN_CENTER);
            lv_obj_center(label);
            s_setting.lang_buttons[i] = btn;
        }
        ui_settings_update_segments();

        sep = ui_settings_create_separator(system_panel,
                                                   UI_SETTINGS_PANEL_WIDTH - 28,
                                                   1, UI_C_LINE);
        lv_obj_set_style_bg_opa(sep, LV_OPA_70, 0);
        lv_obj_align(sep, LV_ALIGN_TOP_MID, 0, UI_SETTINGS_SYSTEM_ROW_H);

        btn = lv_button_create(system_panel);
        ui_reset_base_obj(btn);
        lv_obj_set_size(btn, UI_SETTINGS_PANEL_WIDTH, UI_SETTINGS_SYSTEM_ROW_H);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 0, UI_SETTINGS_SYSTEM_ROW_H);
        lv_obj_set_style_bg_opa(btn, LV_OPA_10, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(btn, lv_color_hex(UI_C_PANEL), LV_STATE_PRESSED);
        lv_obj_add_event_cb(btn, ui_settings_wifi_cb, LV_EVENT_CLICKED, NULL);

        label = lv_label_create(btn);
        lv_label_set_text(label, ui_t(UI_STR_WIFI));
        ui_settings_style_label(label, UI_C_TEXT_MED, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 14, 0);

        s_setting.wifi_status_label = lv_label_create(btn);
        ui_settings_style_label(s_setting.wifi_status_label,
                                       UI_C_TEXT_STRONG, LV_TEXT_ALIGN_RIGHT);
        lv_obj_align(s_setting.wifi_status_label, LV_ALIGN_RIGHT_MID, -14, 0);
        ui_settings_update_wifi_status();

        sep = ui_settings_create_separator(system_panel,
                                                   UI_SETTINGS_PANEL_WIDTH - 28,
                                                   1, UI_C_LINE);
        lv_obj_set_style_bg_opa(sep, LV_OPA_70, 0);
        lv_obj_align(sep, LV_ALIGN_TOP_MID, 0, 2 * UI_SETTINGS_SYSTEM_ROW_H);

        label = lv_label_create(system_panel);
        lv_label_set_text(label, ui_t(UI_STR_LASER_CHECK));
        ui_settings_style_label(label, UI_C_TEXT_MED, LV_TEXT_ALIGN_LEFT);
        if(ui_i18n_get_language() == UI_LANG_ZH) {
            lv_obj_set_style_text_font(label, &ui_laser_check_font_12, 0);
        }
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 14,
                     2 * UI_SETTINGS_SYSTEM_ROW_H +
                     (UI_SETTINGS_SYSTEM_ROW_H - UI_SETTINGS_FONT_H) / 2);

        s_setting.laser_check_switch = lv_switch_create(system_panel);
        lv_obj_set_size(s_setting.laser_check_switch, 42, 22);
        lv_obj_align(s_setting.laser_check_switch, LV_ALIGN_TOP_RIGHT, -14,
                     2 * UI_SETTINGS_SYSTEM_ROW_H + 5);
        lv_obj_set_style_bg_color(s_setting.laser_check_switch,
                                  lv_color_hex(UI_C_LINE), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s_setting.laser_check_switch,
                                  lv_color_hex(UI_C_YELLOW),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(s_setting.laser_check_switch,
                                LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s_setting.laser_check_switch,
                                  lv_color_hex(UI_C_TEXT_STRONG), LV_PART_KNOB);
        ui_settings_update_laser_switch();
        lv_obj_add_event_cb(s_setting.laser_check_switch,
                            ui_settings_laser_check_cb,
                            LV_EVENT_VALUE_CHANGED, NULL);

        sep = ui_settings_create_separator(system_panel,
                                                   UI_SETTINGS_PANEL_WIDTH - 28,
                                                   1, UI_C_LINE);
        lv_obj_set_style_bg_opa(sep, LV_OPA_70, 0);
        lv_obj_align(sep, LV_ALIGN_TOP_MID, 0, 3 * UI_SETTINGS_SYSTEM_ROW_H);

        label = lv_label_create(system_panel);
        lv_label_set_text(label, ui_t(UI_STR_SECOND_OIL));
        ui_settings_style_label(label, UI_C_TEXT_MED, LV_TEXT_ALIGN_LEFT);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 14,
                 3 * UI_SETTINGS_SYSTEM_ROW_H +
                 (UI_SETTINGS_SYSTEM_ROW_H - UI_SETTINGS_FONT_H) / 2);

        s_setting.second_oil_switch = lv_switch_create(system_panel);
        lv_obj_set_size(s_setting.second_oil_switch, 42, 22);
        lv_obj_align(s_setting.second_oil_switch, LV_ALIGN_TOP_RIGHT, -14,
                 3 * UI_SETTINGS_SYSTEM_ROW_H + 5);
        lv_obj_set_style_bg_color(s_setting.second_oil_switch,
                      lv_color_hex(UI_C_LINE), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s_setting.second_oil_switch,
                      lv_color_hex(UI_C_YELLOW),
                      LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(s_setting.second_oil_switch,
                    LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s_setting.second_oil_switch,
                      lv_color_hex(UI_C_TEXT_STRONG), LV_PART_KNOB);
        ui_settings_update_second_oil_switch();
        lv_obj_add_event_cb(s_setting.second_oil_switch,
                    ui_settings_second_oil_cb,
                    LV_EVENT_VALUE_CHANGED, NULL);

        sep = ui_settings_create_separator(system_panel,
                               UI_SETTINGS_PANEL_WIDTH - 28,
                               1, UI_C_LINE);
        lv_obj_set_style_bg_opa(sep, LV_OPA_70, 0);
        lv_obj_align(sep, LV_ALIGN_TOP_MID, 0, 4 * UI_SETTINGS_SYSTEM_ROW_H);

        /* Brightness is local to the UI board and remains adjustable while idle
         * or running. Changes are applied immediately and stored in NVS. */
        label = lv_label_create(system_panel);
        lv_label_set_text(label,
                          ui_i18n_get_language() == UI_LANG_ZH ? u8"亮度" : "Brightness");
        ui_settings_style_label(label, UI_C_TEXT_MED, LV_TEXT_ALIGN_LEFT);
        if(ui_i18n_get_language() == UI_LANG_ZH) {
            lv_obj_set_style_text_font(label, &ui_brightness_font_12, 0);
        }
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 14,
                     4 * UI_SETTINGS_SYSTEM_ROW_H +
                     (UI_SETTINGS_SYSTEM_ROW_H - UI_SETTINGS_FONT_H) / 2);

        btn = lv_button_create(system_panel);
        ui_settings_style_button(btn, 7);
        lv_obj_set_size(btn, UI_SETTINGS_BTN_SIZE, UI_SETTINGS_BTN_SIZE);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 148,
                     4 * UI_SETTINGS_SYSTEM_ROW_H +
                     (UI_SETTINGS_SYSTEM_ROW_H - UI_SETTINGS_BTN_SIZE) / 2);
        lv_obj_add_event_cb(btn, ui_settings_brightness_cb,
                            LV_EVENT_CLICKED, (void *)(intptr_t)-1);
        label = lv_label_create(btn);
        lv_label_set_text(label, "-");
        lv_obj_set_style_text_font(label, &egg_ui_font_16, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_MED), 0);
        lv_obj_center(label);

        s_setting.brightness_label = lv_label_create(system_panel);
        ui_settings_style_label(s_setting.brightness_label,
                                       UI_C_TEXT_STRONG, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(s_setting.brightness_label, 52);
        lv_obj_align(s_setting.brightness_label, LV_ALIGN_TOP_LEFT, 179,
                     4 * UI_SETTINGS_SYSTEM_ROW_H +
                     (UI_SETTINGS_SYSTEM_ROW_H - UI_SETTINGS_FONT_H) / 2);
        ui_settings_update_brightness_label();

        btn = lv_button_create(system_panel);
        ui_settings_style_button(btn, 7);
        lv_obj_set_size(btn, UI_SETTINGS_BTN_SIZE, UI_SETTINGS_BTN_SIZE);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 244,
                     4 * UI_SETTINGS_SYSTEM_ROW_H +
                     (UI_SETTINGS_SYSTEM_ROW_H - UI_SETTINGS_BTN_SIZE) / 2);
        lv_obj_add_event_cb(btn, ui_settings_brightness_cb,
                            LV_EVENT_CLICKED, (void *)(intptr_t)1);
        label = lv_label_create(btn);
        lv_label_set_text(label, "+");
        lv_obj_set_style_text_font(label, &egg_ui_font_16, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_MED), 0);
        lv_obj_center(label);
    }

    (void)ui_uart_bridge_request_settings_sync();
    s_setting.wifi_timer =
        lv_timer_create(ui_settings_wifi_timer_cb, 500U, NULL);

    /* 页面创建时同步按钮可用状态（煎蛋中打开设置页应直接灰显） */
    ui_settings_apply_button_state();
}

void ui_settings_on_data_changed(void)
{
    if(!s_setting.active) {
        return;
    }
    ui_settings_refresh();
}

void ui_settings_on_remote_settings_synced(void)
{
    if(!s_setting.active) {
        return;
    }

    ui_settings_sync_offsets_from_remote();
}
