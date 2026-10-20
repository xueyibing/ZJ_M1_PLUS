#include "ui.h"
#include "ui_home.h"
#include "ui_alert.h"
#include "ui_common.h"
#include "ui_debug.h"
#include "ui_alert_debug.h"
#include "ui_notice.h"
#include "ui_settings.h"
#include "ui_cleaning.h"
#include "ui_uart_bridge.h"
#include "ui_i18n.h"
#include "ui_wifi.h"
#include "buzzer.h"
#include "data_manager.h"
#include "wifi_manager.h"
#include "ds18b20.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#define UI_C_LINE_SOFT       0x232323
#define UI_COLOR_SEG_ON      0xF2F2F2
#define UI_COLOR_PROGRESS_ON UI_C_ACCENT
#define UI_COLOR_CONN_ERR    0xE05050
#define UI_CONN_CHECK_MS     500
#define UI_DEBUG_UNLOCK_TAP_COUNT 5U
#define UI_DEBUG_UNLOCK_TAP_GAP_MS 800U
#define UI_STATUS_TEXT_MAX   32
#define UI_DEBUG_BUTTON_W    58
#define UI_DEBUG_BUTTON_H    24
#define UI_DEBUG_BUTTON_X    10
#define UI_DEBUG_BUTTON_Y    30
#define UI_DEBUG_METRIC_Y    33
#define UI_DEBUG_INFO_X      72
#define UI_DEBUG_INFO_W      76
#define UI_DEBUG_INFO_GAP    4
#define UI_STATUS_BOX_W      UI_DEBUG_INFO_W
#define UI_STATUS_BOX_H      26
#define UI_TOPBAR_ELEMENT_GAP 10
#define UI_TOPBAR_RIGHT_MARGIN 8
#define UI_TOPBAR_CLOCK_W    50
#define UI_TOPBAR_CLOCK_EXT_CLICK 4
#define UI_TOPBAR_ENV_W      80
#define UI_TOPBAR_CONN_SIZE  5
#define UI_TOPBAR_CONN_CLOCK_GAP 0
#define UI_TOPBAR_ENV_CONN_GAP 16
#define UI_POT_METRIC_LABEL_W UI_DEBUG_INFO_W
#define UI_POT_METRIC_TEXT_MAX 32
#define UI_POTB_METRIC_X     UI_DEBUG_INFO_X
#define UI_POTA_METRIC_X     (UI_POTB_METRIC_X + UI_DEBUG_INFO_W + UI_DEBUG_INFO_GAP)
#define UI_STATUS_BOX_X      (UI_POTA_METRIC_X + UI_DEBUG_INFO_W + UI_DEBUG_INFO_GAP)
#define UI_COOK_ANIM_DOT_COUNT 5U
#define UI_COOK_ANIM_PHASE_UNIT 256U
#define UI_COOK_ANIM_CYCLE_UNITS (UI_COOK_ANIM_DOT_COUNT * UI_COOK_ANIM_PHASE_UNIT)
#define UI_COOK_ANIM_PULSE_WIDTH (UI_COOK_ANIM_PHASE_UNIT * 3U / 2U)
#define UI_COOK_ANIM_CYCLE_MS    1800U
#define UI_QTY_BUTTON_W      58
#define UI_QTY_BUTTON_H      50
#define UI_QTY_BUTTON_RADIUS 6
#define UI_QTY_BUTTON_OFFSET_X 38
#define UI_QTY_BUTTON_OFFSET_Y 56

typedef struct {
    bool active;
    lv_obj_t *screen;
    lv_obj_t *debug_button;
    lv_obj_t *status_box;
    lv_obj_t *status_value_label;
    lv_obj_t *potb_temp_label;
    lv_obj_t *pota_temp_label;
    lv_obj_t *progress_track;
    /* 设计稿“制作中”整屏进度浮层。 */
    lv_obj_t *cook_overlay;
    lv_obj_t *cook_title_label;
    lv_obj_t *cook_sub_label;
    lv_obj_t *cook_remaining_label;
    lv_obj_t *cook_anim_dots[UI_COOK_ANIM_DOT_COUNT];
    lv_obj_t *cook_btn_row;      /* 紧急停止/取消后续按钮行 */
    lv_obj_t *cook_estop_btn;
    lv_obj_t *cook_cancel_rest_btn;
    lv_obj_t *start_btn;
    lv_obj_t *start_label;
    lv_obj_t *cancel_label;
    lv_obj_t *don_group;
    lv_obj_t *don_value_label;
    lv_obj_t *don_left_btn;
    lv_obj_t *don_right_btn;
    lv_obj_t *don_dots[UI_DON_COUNT];
    /* 设计稿主界面：整屏煎蛋图 + 信息卡 + 分页点。 */
    lv_obj_t *bg_viewport;       /* Fixed, clipped viewport for image sliding. */
    lv_obj_t *selector_overlay;  /* Fixed foreground navigation controls. */
    lv_obj_t *bg_page;
    lv_obj_t *bg_page_incoming;
    lv_obj_t *bg_image;
    lv_obj_t *bg_image_incoming; /* 拖拽切换时从边缘跟手进入的邻档整屏图 */
    lv_obj_t *name_label;
    lv_obj_t *en_label;
    lv_obj_t *desc_label;
    lv_obj_t *name_label_incoming;
    lv_obj_t *en_label_incoming;
    lv_obj_t *desc_label_incoming;
    lv_obj_t *egg_dots[4];
    lv_obj_t *reserve_btn;
    lv_obj_t *reserve_label;
    int egg_tile_idx;
    int bg_image_idx;      /* 当前主背景真实使用的图片档位 */
    int egg_tile_shown;    /* 上次已呈现的熟度档位，仅在切换时播放动画 */
    int selector_controls_idx;
    int8_t selector_adjustable_shown;
    /* 口味整屏图左右拖拽（跟手/粘手）切换状态。 */
    bool egg_drag_active;   /* 正在拖拽切换口味 */
    int egg_drag_start_x;   /* 拖拽起始触点 X */
    int egg_drag_from_idx;  /* 拖拽起始档位 */
    int egg_drag_target_idx;/* 拖拽期间跟手进入的邻档，-1=无 */
    /* 设计稿弹窗：数量选择面板 + 预约时间面板。 */
    int egg_drag_rendered_dx;        /* Last displacement sent to LVGL. */
    uint32_t egg_drag_last_frame_ms; /* Coalesce raw samples to frames. */
    lv_obj_t *qty_mask;
    lv_obj_t *qty_panel;
    lv_obj_t *qty_num_row;
    lv_obj_t *qty_num_btns[UI_MAX_MAKE_QUANTITY];
    lv_obj_t *qty_cook_btn;
    lv_obj_t *qty_reserve_btn;
    lv_obj_t *qty_reserve_cancel_btn; /* 操作行左侧的取消按钮 */
    lv_obj_t *qty_reserve_cancel_label;
    lv_obj_t *qty_title_label;        /* 数量面板标题（制作/预约动态切换） */
    lv_obj_t *qty_reserve_section;    /* 数量面板内嵌的预约时间步进器（预约态才显示） */
    lv_obj_t *reserve_time_label;     /* 面板中央大号时间 "08:00" */
    lv_obj_t *reserve_meridiem_label; /* 面板中央 AM/PM */
    int qty_selected;      /* 当前选中的数量 1~6 */
    int qty_panel_intent;  /* 0=制作，1=预约 */
    int reserve_hour;      /* 预约小时 0~23，-1=未预约 */
    int reserve_minute;    /* 预约分钟：0 或 30 */
    int reserve_qty;       /* 已确认预约的制作数量 */
    int reserve_edit_hour;   /* 预约面板中正在编辑的小时 */
    int reserve_edit_minute; /* 预约面板中正在编辑的分钟 */
    bool panel_open;       /* 任一弹窗打开时为真，用于拦截滑动/手势 */
    lv_obj_t *qty_buttons[2];
    lv_obj_t *qty_button_labels[2];
    lv_obj_t *digit_segments[7];
    lv_obj_t *conn_indicator;
    lv_obj_t *status_bar;       /* 顶部状态栏容器 */
    lv_obj_t *clock_label;      /* 右上角实时时间 HH:MM */
    lv_obj_t *environment_group; /* 时间左侧的环境温度组合 */
    lv_obj_t *temperature_label;
    lv_obj_t *wifi_icon_label;  /* 时间左侧的 WiFi 状态图标 */
    lv_obj_t *wifi_off_line;    /* WiFi 未连接时的短竖线 */
    lv_obj_t *time_mask;
    lv_obj_t *time_panel;
    lv_obj_t *time_hour_label;
    lv_obj_t *time_minute_label;
    int manual_hour;
    int manual_minute;
    lv_timer_t *countdown_timer;
    lv_timer_t *blink_timer;
    lv_timer_t *conn_check_timer;
    ui_data_t *data;
    uint8_t blink_phase;
    uint8_t debug_unlock_tap_count;
    uint32_t debug_unlock_last_tap_ms;
    char last_status_text[UI_STATUS_TEXT_MAX];
    uint32_t status_text_color_shown;
    uint32_t status_border_color_shown;
    bool status_style_valid;
    uint8_t start_visual_state;
    int8_t wifi_connected_shown;
} egg_ui_view_t;

static egg_ui_view_t s_ui;
static bool s_debug_mode_unlocked;
static bool s_cancel_stop_requested;
static void ui_refresh(void);
static void ui_update_reserve_label(void);
static void ui_sync_reservation_from_remote(const ui_uart_heartbeat_state_t *heartbeat);
static void ui_lock_debug_mode(void);
static void ui_unlock_debug_mode(void);
static void ui_refresh_status_display(const ui_data_state_t *state);
static void ui_refresh_pot_temp_display(void);
static void ui_apply_debug_button_visibility(void);
static void ui_apply_debug_metrics_visibility(void);
static void ui_reset_debug_unlock_sequence(void);
static void ui_doneness_slide(int target_idx, int dir);
static void ui_track_cancel_debug_unlock_tap(void);
static bool ui_should_show_cancel_notice(const ui_data_state_t *state);
static void ui_open_cancel_confirmation(void);
static void ui_open_cancel_notice(void);
static bool ui_confirm_cancel(void *user_data);
static bool ui_continue_making(void *user_data);
static bool ui_confirm_emergency_recovery(void *user_data);
static void ui_cook_estop_cb(lv_event_t *e);
static void ui_cook_cancel_rest_cb(lv_event_t *e);
static void ui_open_cancel_rest_confirmation(void);
static bool ui_confirm_cancel_rest(void *user_data);
static void ui_open_qty_panel(int intent);   /* intent: 0=制作, 1=预约 */
static void ui_close_qty_panel(void);
static void ui_open_time_panel(void);
static void ui_close_time_panel(void);

static void anim_x_cb(void *var, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)var, v);
}

static const uint8_t k_digit_map[10][7] = {
    {1, 1, 1, 1, 1, 1, 0},
    {0, 1, 1, 0, 0, 0, 0},
    {1, 1, 0, 1, 1, 0, 1},
    {1, 1, 1, 1, 0, 0, 1},
    {0, 1, 1, 0, 0, 1, 1},
    {1, 0, 1, 1, 0, 1, 1},
    {1, 0, 1, 1, 1, 1, 1},
    {1, 1, 1, 0, 0, 0, 0},
    {1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 0, 1, 1},
};

static const ui_data_state_t *ui_get_state(void)
{
    if(s_ui.data == NULL) {
        return NULL;
    }

    return ui_data_get_state(s_ui.data);
}

static const char *ui_get_status_text(const ui_data_state_t *state)
{
    if(state == NULL) {
        return "--";
    }

    if(ui_data_state_has_alert(state)) {
        return ui_t(UI_STR_ST_FAULT);
    }

    switch(state->maker_state) {
    case UI_MAKER_IDLE:
        return state->start ? ui_t(UI_STR_ST_PENDING) : ui_t(UI_STR_ST_IDLE);
    case UI_MAKER_CHECKING_TMP_SENSOR:
        return ui_t(UI_STR_ST_CHECK_TEMP);
    case UI_MAKER_CHECKING_VL53:
        return ui_t(UI_STR_ST_CHECK_DIST);
    case UI_MAKER_CHECKING_POT:
        return ui_t(UI_STR_ST_CHECK_POT);
    case UI_MAKER_HOMEING:
        return ui_t(UI_STR_ST_HOMING);
    case UI_MAKER_PRE_HEATING:
        return ui_t(UI_STR_ST_PREHEAT);
    case UI_MAKER_GIVING_EGG:
        return ui_t(UI_STR_ST_GIVE_EGG);
    case UI_MAKER_GIVING_OIL:
        return ui_t(UI_STR_ST_GIVE_OIL);
    case UI_MAKER_MAKING:
        return ui_t(UI_STR_ST_MAKING);
    case UI_MAKER_STOPPING:
        return ui_t(UI_STR_ST_STOPPING);
    case UI_MAKER_STOPPED_NEEDS_RECOVERY:
        return ui_t(UI_STR_ST_STOPPED_RECOVERY);
    }

    return ui_t(UI_STR_ST_UNKNOWN);
}

static void ui_label_set_text_if_changed(lv_obj_t *label, const char *text)
{
    const char *current;

    if(label == NULL || text == NULL) {
        return;
    }

    current = lv_label_get_text(label);
    if(current == NULL || strcmp(current, text) != 0) {
        lv_label_set_text(label, text);
    }
}

static void ui_set_status_text(const char *text)
{
    const char *safe_text = (text != NULL && text[0] != '\0') ? text : "--";

    if(s_ui.status_value_label == NULL) {
        return;
    }

    if(strcmp(s_ui.last_status_text, safe_text) != 0) {
        strlcpy(s_ui.last_status_text, safe_text, sizeof(s_ui.last_status_text));
        lv_label_set_text(s_ui.status_value_label, safe_text);
    }
}

static void ui_refresh_status_display(const ui_data_state_t *state)
{
    uint32_t text_color = UI_C_YELLOW;
    uint32_t border_color = UI_C_LINE;

    if(s_ui.status_value_label == NULL || s_ui.status_box == NULL) {
        return;
    }

    ui_set_status_text(ui_get_status_text(state));

    if(state != NULL && ui_data_state_has_alert(state)) {
        border_color = UI_COLOR_CONN_ERR;
    }

    if(!s_ui.status_style_valid || s_ui.status_text_color_shown != text_color) {
        lv_obj_set_style_text_color(s_ui.status_value_label, lv_color_hex(text_color), 0);
        s_ui.status_text_color_shown = text_color;
    }
    if(!s_ui.status_style_valid || s_ui.status_border_color_shown != border_color) {
        lv_obj_set_style_border_color(s_ui.status_box, lv_color_hex(border_color), 0);
        s_ui.status_border_color_shown = border_color;
    }
    if(!s_ui.status_style_valid) {
        lv_obj_set_style_border_opa(s_ui.status_box, LV_OPA_70, 0);
        s_ui.status_style_valid = true;
    }
}

static bool ui_is_valid_pot_temp(int32_t temp)
{
    return temp > 0 && temp <= 400;
}

static bool ui_is_valid_pot_dw(int32_t dw)
{
    return dw >= 0 && dw <= 8;
}

static bool ui_is_valid_pot_remaining_heat_s(int32_t seconds)
{
    return seconds >= 0 && seconds <= 999;
}

static void ui_set_pot_metric_label(lv_obj_t *label,
                                    const char *prefix,
                                    bool online,
                                    bool has_temp,
                                    int32_t temp,
                                    bool has_igbt_temp,
                                    int32_t igbt_temp,
                                    bool has_dw,
                                    int32_t dw,
                                    bool has_remaining,
                                    int32_t remaining_s)
{
    char temp_text[12];
    char dw_text[6];
    char remaining_text[8];
    char text[UI_POT_METRIC_TEXT_MAX];

    if(label == NULL || prefix == NULL) {
        return;
    }

    if(!online || !has_temp || !ui_is_valid_pot_temp(temp)) {
        strlcpy(temp_text, "--", sizeof(temp_text));
    } else if(!has_igbt_temp || !ui_is_valid_pot_temp(igbt_temp)) {
        snprintf(temp_text, sizeof(temp_text), "%ld/--", (long)temp);
    } else {
        snprintf(temp_text, sizeof(temp_text), "%ld/%ld", (long)temp, (long)igbt_temp);
    }

    if(!online || !has_dw || !ui_is_valid_pot_dw(dw)) {
        strlcpy(dw_text, "D--", sizeof(dw_text));
    } else {
        snprintf(dw_text, sizeof(dw_text), "D%ld", (long)dw);
    }

    if(!online || !has_remaining || !ui_is_valid_pot_remaining_heat_s(remaining_s)) {
        strlcpy(remaining_text, "--", sizeof(remaining_text));
    } else {
        snprintf(remaining_text, sizeof(remaining_text), "%ld", (long)remaining_s);
    }

    snprintf(text, sizeof(text), "%s:%s %s %s",
             prefix, temp_text, dw_text, remaining_text);
    ui_label_set_text_if_changed(label, text);
}

static void ui_refresh_pot_temp_display(void)
{
    ui_uart_heartbeat_state_t hb;

    if(s_ui.potb_temp_label == NULL || s_ui.pota_temp_label == NULL) {
        return;
    }

    ui_uart_bridge_get_heartbeat_state(&hb);
    ui_set_pot_metric_label(s_ui.potb_temp_label,
                            "B", hb.online,
                            hb.has_remote_pot_temps, hb.remote_potb_temp,
                            hb.has_remote_pot_igbt_temps, hb.remote_potb_igbt_temp,
                            hb.has_remote_pot_dw, hb.remote_potb_dw,
                            hb.has_remote_pot_remaining_heat_s,
                            hb.remote_potb_remaining_heat_s);
    ui_set_pot_metric_label(s_ui.pota_temp_label,
                            "A", hb.online,
                            hb.has_remote_pot_temps, hb.remote_pota_temp,
                            hb.has_remote_pot_igbt_temps, hb.remote_pota_igbt_temp,
                            hb.has_remote_pot_dw, hb.remote_pota_dw,
                            hb.has_remote_pot_remaining_heat_s,
                            hb.remote_pota_remaining_heat_s);
}


static lv_obj_t *ui_create_separator(lv_obj_t *parent, lv_coord_t width, lv_coord_t height, uint32_t color)
{
    lv_obj_t *separator = lv_obj_create(parent);

    ui_reset_base_obj(separator);
    lv_obj_set_size(separator, width, height);
    lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(separator, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(separator, 0, 0);
    return separator;
}


static void ui_style_pressable_button(lv_obj_t *button, lv_coord_t width, lv_coord_t height, lv_coord_t radius)
{
    ui_reset_base_obj(button);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, radius, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_10, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_C_PANEL), LV_STATE_PRESSED);
}


static void ui_set_segment_state(lv_obj_t *segment, bool on)
{
    lv_obj_set_style_bg_opa(segment, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(segment, lv_color_hex(UI_COLOR_SEG_ON), 0);
    lv_obj_set_style_shadow_width(segment, on ? 10 : 0, 0);
    lv_obj_set_style_shadow_color(segment, lv_color_hex(UI_COLOR_SEG_ON), 0);
    lv_obj_set_style_shadow_opa(segment, on ? LV_OPA_30 : LV_OPA_TRANSP, 0);
}

static void ui_clear_digit(void)
{
    if(s_ui.digit_segments[0] == NULL) {
        return;
    }
    for(int i = 0; i < 7; i++) {
        ui_set_segment_state(s_ui.digit_segments[i], false);
    }
}

static void ui_show_digit(uint8_t value)
{
    const uint8_t *pattern = k_digit_map[value % 10U];

    if(s_ui.digit_segments[0] == NULL) {
        return;
    }
    for(int i = 0; i < 7; i++) {
        ui_set_segment_state(s_ui.digit_segments[i], pattern[i] != 0);
    }
}

/* ===== 主界面：四档整屏口味图卡片 ======================================== */
#define UI_EGG_TILE_COUNT 4

typedef struct {
    ui_doneness_t don;
    const lv_image_dsc_t *img;
    const char *name; /* UTF-8 中文名 */
    const char *en;   /* English subtitle */
    const char *desc; /* UTF-8 描述 */
} egg_tile_t;

static const egg_tile_t k_egg_tiles[UI_EGG_TILE_COUNT] = {
    { UI_DON_WELL,      &egg_img_well,       u8"全熟",   "WELL DONE",     u8"双面全熟 · 蛋黄凝固紧实" },
    { UI_DON_JAMMY,     &egg_img_easy,       u8"糖心",   "OVER EASY",     u8"双面轻煎 · 蛋黄软糯流心" },
    { UI_DON_SOFT_YOLK, &egg_img_sunny,      u8"软嫩",   "SOFT YOLK",     u8"双面嫩煎 · 蛋黄柔软鲜嫩" },
    { UI_DON_SUNNY,     &egg_img_sunny_side, u8"太阳蛋", "SUNNY SIDE UP", u8"单面煎制 · 蛋黄鲜亮流心" },
};

/* ===== 整屏图 PSRAM 预载 =================================================
 * 四张 480x320 RGB565 图各 300KB，const 链接在 QSPI flash（mmap 读取）。
 * 拖拽滑动的每一帧需要合成两张整屏图（~600KB），flash DIO 流式读取
 * 带宽只有 ~10-15MB/s，取数就占 ~50ms —— 滑动帧率被卡到 ~10fps 的
 * 主要原因。开机一次性拷入 PSRAM（80MHz Quad，~40MB/s+），滑动热路径
 * 不再磁 flash。任一张分配失败则该张回退 flash 原图，不影响功能。 */
static lv_image_dsc_t s_egg_img_psram[UI_EGG_TILE_COUNT];
static bool s_egg_img_psram_ok[UI_EGG_TILE_COUNT];
static bool s_egg_img_preload_done;

static const lv_image_dsc_t *ui_egg_tile_img(int idx)
{
    if(idx >= 0 && idx < UI_EGG_TILE_COUNT && s_egg_img_psram_ok[idx]) {
        return &s_egg_img_psram[idx];
    }
    return k_egg_tiles[idx].img;
}

static void ui_egg_preload_images(void)
{
    int i;
    int loaded = 0;

    if(s_egg_img_preload_done) {
        return; /* 已预载（页面重建时不重复拷贝） */
    }
    s_egg_img_preload_done = true;
    for(i = 0; i < UI_EGG_TILE_COUNT; i++) {
        const lv_image_dsc_t *src = k_egg_tiles[i].img;
        uint8_t *dst;

        if(src == NULL || src->data_size == 0) {
            continue;
        }
        dst = heap_caps_aligned_alloc(64, src->data_size, MALLOC_CAP_SPIRAM);
        if(dst == NULL) {
            ESP_LOGW("ui_home", "egg img %d PSRAM preload skipped (no mem)", i);
            continue;
        }
        memcpy(dst, src->data, src->data_size);
        s_egg_img_psram[i] = *src;
        s_egg_img_psram[i].data = dst;
        s_egg_img_psram_ok[i] = true;
        loaded++;
    }
    ESP_LOGI("ui_home", "egg tiles preloaded to PSRAM (%d/%d)",
             loaded, UI_EGG_TILE_COUNT);
}

static int ui_egg_tile_for_doneness(ui_doneness_t don)
{
    int i;
    for(i = 0; i < UI_EGG_TILE_COUNT; i++) {
        if(k_egg_tiles[i].don == don) {
            return i;
        }
    }
    return -1;
}

/* 熟度切换动画：新内容按方向从侧边滑入并淡入；仅在档位真正变化时触发，
 * 常规刷新走非动画分支，保证刷新性能。 */
static void ui_anim_translate_x_cb(void *var, int32_t v)
{
    lv_obj_set_style_translate_x((lv_obj_t *)var, v, 0);
}

static void ui_anim_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

/* 一条主时间轴驱动全部制作指示点。亮度波峰连续向右移动，并在末点与
 * 首点之间循环衔接，避免多条独立动画逐渐同步后一起明暗。 */
static void ui_cook_anim_wave_cb(void *var, int32_t phase)
{
    const int32_t cycle = (int32_t)UI_COOK_ANIM_CYCLE_UNITS;
    const int32_t pulse_width = (int32_t)UI_COOK_ANIM_PULSE_WIDTH;

    (void)var;
    if(cycle <= 0 || pulse_width <= 0) {
        return;
    }

    phase %= cycle;
    if(phase < 0) {
        phase += cycle;
    }

    for(uint32_t i = 0; i < UI_COOK_ANIM_DOT_COUNT; i++) {
        int32_t distance = phase - (int32_t)(i * UI_COOK_ANIM_PHASE_UNIT);
        int32_t opacity = LV_OPA_30;

        if(distance < 0) {
            distance = -distance;
        }
        if(distance > cycle / 2) {
            distance = cycle - distance;
        }
        if(distance < pulse_width) {
            opacity += ((LV_OPA_COVER - LV_OPA_30) *
                        (pulse_width - distance)) / pulse_width;
        }

        if(s_ui.cook_anim_dots[i] != NULL) {
            lv_obj_set_style_opa(s_ui.cook_anim_dots[i], (lv_opa_t)opacity, 0);
        }
    }
}

#if 0 /* Legacy transition retained for reference; low-resource build uses instant switching. */
static void ui_play_doneness_transition(int dir)
{
    const int32_t from = (dir > 0) ? 48 : -48;
    lv_obj_t *slide[3] = { s_ui.name_label, s_ui.en_label, s_ui.desc_label };
    size_t i;

    /* 背景整屏图仅做淡入，避免平移露出边缘。 */
    if(s_ui.bg_image != NULL) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_ui.bg_image);
        lv_anim_set_exec_cb(&a, ui_anim_opa_cb);
        lv_anim_set_values(&a, LV_OPA_50, LV_OPA_COVER);
        lv_anim_set_duration(&a, 200);
        lv_anim_start(&a);
    }

    /* 信息卡文字：横向滑入 + 淡入。 */
    for(i = 0; i < sizeof(slide) / sizeof(slide[0]); i++) {
        lv_obj_t *t = slide[i];
        lv_anim_t sx;
        lv_anim_t op;
        if(t == NULL) {
            continue;
        }
        lv_anim_init(&sx);
        lv_anim_set_var(&sx, t);
        lv_anim_set_exec_cb(&sx, ui_anim_translate_x_cb);
        lv_anim_set_values(&sx, from, 0);
        lv_anim_set_duration(&sx, 220);
        lv_anim_set_path_cb(&sx, lv_anim_path_ease_out);
        lv_anim_start(&sx);

        lv_anim_init(&op);
        lv_anim_set_var(&op, t);
        lv_anim_set_exec_cb(&op, ui_anim_opa_cb);
        lv_anim_set_values(&op, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_duration(&op, 200);
        lv_anim_start(&op);
    }
}

#endif

static void ui_apply_doneness_selector(const ui_data_state_t *state, bool adjustable)
{
    int idx = s_ui.egg_tile_idx;
    int mapped;
    const egg_tile_t *tile;
    bool content_changed;
    int i;

    /* 若外部（持久化/UART）改变了熟度且落在三档内，则同步页面选中项。 */
    mapped = ui_egg_tile_for_doneness(state->doneness);
    if(mapped >= 0) {
        idx = mapped;
    }
    if(idx < 0 || idx >= UI_EGG_TILE_COUNT) {
        idx = 0;
    }
    s_ui.egg_tile_idx = idx;
    tile = &k_egg_tiles[idx];
    content_changed = (s_ui.bg_image_idx != idx);

    /* lv_image_set_src() 即使源指针不变也会让整个 480x320 图片失效。
     * 仅在档位真正改变时更新图片和配套文案，遥测刷新不再触发整屏重绘。 */
    if(content_changed) {
        if(s_ui.bg_image != NULL) {
            lv_image_set_src(s_ui.bg_image, ui_egg_tile_img(idx));
        }
        for(i = 0; i < UI_EGG_TILE_COUNT; i++) {
            bool on = (i == idx);
            if(s_ui.egg_dots[i] == NULL) {
                continue;
            }
            lv_obj_set_size(s_ui.egg_dots[i], on ? 24 : 8, 8);
            lv_obj_set_style_radius(s_ui.egg_dots[i], on ? 4 : LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(s_ui.egg_dots[i],
                                      lv_color_hex(on ? UI_C_ACCENT : 0xFFFFFF), 0);
            lv_obj_set_style_bg_opa(s_ui.egg_dots[i], on ? LV_OPA_COVER : LV_OPA_40, 0);
        }
        s_ui.bg_image_idx = idx;
    }
    /* 文案比较不会触发重绘，同时允许语言变化在档位不变时生效。 */
    ui_label_set_text_if_changed(s_ui.name_label, tile->name);
    ui_label_set_text_if_changed(s_ui.en_label, tile->en);
    ui_label_set_text_if_changed(s_ui.desc_label, tile->desc);

    if(s_ui.selector_controls_idx != idx ||
       s_ui.selector_adjustable_shown != (int8_t)adjustable) {
        if(s_ui.don_left_btn != NULL) {
            if(adjustable && idx > 0) {
                lv_obj_clear_flag(s_ui.don_left_btn, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_ui.don_left_btn, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if(s_ui.don_right_btn != NULL) {
            if(adjustable && idx < UI_EGG_TILE_COUNT - 1) {
                lv_obj_clear_flag(s_ui.don_right_btn, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_ui.don_right_btn, LV_OBJ_FLAG_HIDDEN);
            }
        }
        s_ui.selector_controls_idx = idx;
        s_ui.selector_adjustable_shown = (int8_t)adjustable;
    }

    /* 仅在档位相对上次呈现发生变化时播放切换动画（初次呈现不动画）。 */
    s_ui.egg_tile_shown = idx;
}

static void ui_apply_start_button_state(const ui_data_state_t *state)
{
    lv_obj_t *btn = s_ui.start_btn;
    lv_obj_t *lbl = s_ui.start_label;
    const char *label_text;
    uint8_t visual_state;

    if(btn == NULL || lbl == NULL || state == NULL) {
        return;
    }

    if(ui_data_state_is_running(state)) {
        visual_state = 1U;
        label_text = ui_t(UI_STR_COOKING_WAIT);
    } else if(ui_data_state_is_finished(state)) {
        visual_state = 2U;
        label_text = ui_t(UI_STR_DONE_CHECK);
    } else {
        visual_state = 0U;
        label_text = ui_t(UI_STR_START_MAKE);
    }

    if(s_ui.start_visual_state == visual_state) {
        ui_label_set_text_if_changed(lbl, label_text);
        return;
    }
    s_ui.start_visual_state = visual_state;

    if(visual_state == 1U) {
        /* 制作中：深蓝灰渐变底 + 橙黄字，提示“请稍候…”。 */
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1E293B), 0);
        lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0x0F172A), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(UI_C_ACCENT), 0);
        lv_obj_set_style_border_opa(btn, LV_OPA_40, 0);
        lv_obj_set_style_shadow_width(btn, 10, 0);
        lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_40, 0);
        lv_obj_set_style_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(UI_C_ACCENT), 0);
        lv_obj_set_style_text_opa(lbl, LV_OPA_COVER, 0);
        ui_label_set_text_if_changed(lbl, label_text);
    } else if(visual_state == 2U) {
        /* 完成：绿色渐变底 + 深绿字，提示“✓ 已完成”。 */
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x10B981), 0);
        lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0x059669), 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 14, 0);
        lv_obj_set_style_shadow_color(btn, lv_color_hex(0x10B981), 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_50, 0);
        lv_obj_set_style_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x064E3B), 0);
        lv_obj_set_style_text_opa(lbl, LV_OPA_COVER, 0);
        ui_label_set_text_if_changed(lbl, label_text);
    } else {
        /* 待机：金色渐变底 + 深棕字；数量在后续弹窗设置，主页按钮始终保持全亮可读。 */
        lv_obj_set_style_bg_color(btn, lv_color_hex(UI_C_PRIMARY_TOP), 0);
        lv_obj_set_style_bg_grad_color(btn, lv_color_hex(UI_C_PRIMARY_BOTTOM), 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 14, 0);
        lv_obj_set_style_shadow_color(btn, lv_color_hex(UI_C_PRIMARY_BOTTOM), 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_50, 0);
        lv_obj_set_style_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x422006), 0);
        lv_obj_set_style_text_opa(lbl, LV_OPA_COVER, 0);
        ui_label_set_text_if_changed(lbl, label_text);
    }
}

static void ui_update_controls_visuals(const ui_data_state_t *state)
{
    bool recipe_adjustable;

    if(state == NULL) {
        return;
    }

    /* 配方（熟度）只在完全空闲时允许调整；煎蛋过程中禁用。 */
    recipe_adjustable = !ui_data_state_has_alert(state) &&
                        ui_data_state_is_idle(state);
    ui_apply_doneness_selector(state, recipe_adjustable);

    /* 数量选择已移除；开始按钮按待机/制作中/完成三态切换颜色与文案。 */
    ui_apply_start_button_state(state);
}

static void ui_stop_conn_check_timer(void)
{
    if(s_ui.conn_check_timer != NULL) {
        lv_timer_delete(s_ui.conn_check_timer);
        s_ui.conn_check_timer = NULL;
    }
}

/* 刷新右上角的时间与 WiFi 状态图标。 */
static void ui_refresh_topbar(void)
{
    if(s_ui.temperature_label != NULL) {
        ds18b20_reading_t reading;
        char temperature_text[20];

        if(ds18b20_get_reading(&reading)) {
            snprintf(temperature_text, sizeof(temperature_text), "%.1f°C",
                     (double)reading.temperature_centi_c / 100.0);
        } else {
            strlcpy(temperature_text, "--°C", sizeof(temperature_text));
        }
        ui_label_set_text_if_changed(s_ui.temperature_label, temperature_text);
    }

    if(s_ui.clock_label != NULL) {
        time_t now = time(NULL);
        struct tm tm_now;
        char buf[8];
        localtime_r(&now, &tm_now);
        if((tm_now.tm_year + 1900) < 2021) {
            strlcpy(buf, "--:--", sizeof(buf));
        } else {
            snprintf(buf, sizeof(buf), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
        }
        ui_label_set_text_if_changed(s_ui.clock_label, buf);
    }

    if(s_ui.wifi_icon_label != NULL) {
        bool connected = wifi_manager_is_connected();
        if(s_ui.wifi_connected_shown != (int8_t)connected) {
            lv_obj_set_style_text_color(s_ui.wifi_icon_label,
                                        lv_color_hex(connected ? 0x10B981 : UI_C_TEXT_MED),
                                        LV_PART_MAIN);
            if(s_ui.wifi_off_line != NULL) {
                if(connected) {
                    lv_obj_add_flag(s_ui.wifi_off_line, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_clear_flag(s_ui.wifi_off_line, LV_OBJ_FLAG_HIDDEN);
                }
            }
            s_ui.wifi_connected_shown = (int8_t)connected;
        }
    }
}

static void ui_conn_check_timer_cb(lv_timer_t *timer)
{
    ui_uart_heartbeat_state_t hb;
    const ui_data_state_t *state;

    LV_UNUSED(timer);

    ui_uart_bridge_get_heartbeat_state(&hb);
    ui_sync_reservation_from_remote(&hb);
    if(s_ui.conn_indicator != NULL) {
        lv_obj_set_style_bg_color(s_ui.conn_indicator,
                                  lv_color_hex(hb.online ? 0x10B981 : UI_COLOR_CONN_ERR),
                                  0);
    }

    ui_refresh_topbar();

    state = ui_get_state();
    ui_refresh_status_display(state);
    ui_refresh_pot_temp_display();
}

static void ui_stop_countdown_timer(void)
{
    if(s_ui.countdown_timer != NULL) {
        lv_timer_delete(s_ui.countdown_timer);
        s_ui.countdown_timer = NULL;
    }
}

static void ui_stop_blink_timer(void)
{
    if(s_ui.blink_timer != NULL) {
        lv_timer_delete(s_ui.blink_timer);
        s_ui.blink_timer = NULL;
    }

    s_ui.blink_phase = 0;
}

static void ui_wifi_icon_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if(s_ui.data == NULL) {
        return;
    }
    buzzer_beep_async(50);
    s_ui.active = false;
    ui_stop_countdown_timer();
    ui_stop_blink_timer();
    ui_stop_conn_check_timer();
    ui_wifi_create(s_ui.data);
}

static void ui_clean_mode_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if(s_ui.data == NULL) {
        return;
    }

    buzzer_beep_async(50);
    s_ui.active = false;
    ui_stop_countdown_timer();
    ui_stop_blink_timer();
    ui_stop_conn_check_timer();
    ui_cleaning_create(s_ui.data);
    ui_apply_debug_button_visibility();
}

static void ui_update_manual_time_labels(void)
{
    if(s_ui.time_hour_label != NULL) {
        lv_label_set_text_fmt(s_ui.time_hour_label, "%02d", s_ui.manual_hour);
    }
    if(s_ui.time_minute_label != NULL) {
        lv_label_set_text_fmt(s_ui.time_minute_label, "%02d", s_ui.manual_minute);
    }
}

static void ui_time_adjust_cb(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    int total_minutes = s_ui.manual_hour * 60 + s_ui.manual_minute + delta;

    buzzer_beep_async(40);
    total_minutes = (total_minutes % (24 * 60) + (24 * 60)) % (24 * 60);
    s_ui.manual_hour = total_minutes / 60;
    s_ui.manual_minute = total_minutes % 60;
    ui_update_manual_time_labels();
}

static void ui_time_cancel_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    buzzer_beep_async(40);
    ui_close_time_panel();
}

static void ui_time_confirm_cb(lv_event_t *e)
{
    time_t now = time(NULL);
    struct tm tm_now;
    struct timeval value;

    LV_UNUSED(e);
    localtime_r(&now, &tm_now);
    if((tm_now.tm_year + 1900) < 2021) {
        memset(&tm_now, 0, sizeof(tm_now));
        tm_now.tm_year = 2026 - 1900;
        tm_now.tm_mon = 0;
        tm_now.tm_mday = 1;
    }
    tm_now.tm_hour = s_ui.manual_hour;
    tm_now.tm_min = s_ui.manual_minute;
    tm_now.tm_sec = 0;
    tm_now.tm_isdst = -1;

    value.tv_sec = mktime(&tm_now);
    value.tv_usec = 0;
    if(value.tv_sec >= 0 && settimeofday(&value, NULL) == 0) {
        buzzer_beep_async(50);
        ui_close_time_panel();
        ui_refresh_topbar();
    } else {
        buzzer_beep_async(100);
    }
}

static void ui_clock_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    buzzer_beep_async(50);
    ui_open_time_panel();
}

static void ui_open_time_panel(void)
{
    time_t now;
    struct tm tm_now;

    if(s_ui.time_panel == NULL || s_ui.time_mask == NULL) {
        return;
    }

    now = time(NULL);
    localtime_r(&now, &tm_now);
    if((tm_now.tm_year + 1900) < 2021) {
        s_ui.manual_hour = 12;
        s_ui.manual_minute = 0;
    } else {
        s_ui.manual_hour = tm_now.tm_hour;
        s_ui.manual_minute = tm_now.tm_min;
    }
    ui_update_manual_time_labels();
    lv_obj_clear_flag(s_ui.time_mask, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_ui.time_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_ui.time_mask);
    lv_obj_move_foreground(s_ui.time_panel);
    s_ui.panel_open = true;
    ui_apply_debug_button_visibility();
}

static void ui_close_time_panel(void)
{
    if(s_ui.time_panel == NULL || s_ui.time_mask == NULL) {
        return;
    }
    lv_obj_add_flag(s_ui.time_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.time_mask, LV_OBJ_FLAG_HIDDEN);
    s_ui.panel_open = false;
}

static void ui_blink_timer_cb(lv_timer_t *timer)
{
    const ui_data_state_t *state;

    LV_UNUSED(timer);

    state = ui_get_state();
    if(state == NULL) {
        return;
    }

    s_ui.blink_phase++;
    if((s_ui.blink_phase & 0x01U) != 0U) {
        ui_clear_digit();
    } else {
        ui_show_digit(state->needMake);
    }

    if(s_ui.blink_phase >= 8U) {
        ui_data_t *data = s_ui.data;

        ui_stop_blink_timer();
        ui_data_ack_finished(data);
        ui_refresh();
    }
}

static void ui_start_blink_sequence(void)
{
    if(s_ui.blink_timer == NULL) {
        s_ui.blink_phase = 0;
        s_ui.blink_timer = lv_timer_create(ui_blink_timer_cb, 350, NULL);
    }
}

static void ui_countdown_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if(s_ui.data != NULL && ui_data_tick(s_ui.data, 1U)) {
        ui_refresh();
    }
}


static void ui_doneness_step(int delta)
{
    const ui_data_state_t *state;
    int idx;

    ui_reset_debug_unlock_sequence();

    if(s_ui.data == NULL) {
        return;
    }
    /* 弹窗打开时屏蔽熟度切换，避免面板后方误滑档。 */
    if(s_ui.panel_open) {
        return;
    }
    state = ui_data_get_state(s_ui.data);
    if(state == NULL) {
        return;
    }
    /* 仅在空闲且无告警时允许调节熟度。 */
    if(!ui_data_state_is_idle(state) || ui_data_state_has_alert(state)) {
        return;
    }
    idx = s_ui.egg_tile_idx + delta;
    if(idx < 0 || idx >= UI_EGG_TILE_COUNT || idx == s_ui.egg_tile_idx) {
        return;
    }
    /* 与拖拽切换一致的整屏滑动动画；落位回调负责设档/蜂鸣/刷新。 */
    if(s_ui.bg_page != NULL && s_ui.bg_page_incoming != NULL) {
        ui_doneness_slide(idx, delta);
        return;
    }
    s_ui.egg_tile_idx = idx;
    ui_data_set_doneness(s_ui.data, k_egg_tiles[idx].don);
    buzzer_beep_async(50);
    ui_refresh();
}

static void ui_doneness_prev_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_doneness_step(-1);
}

static void ui_doneness_next_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_doneness_step(1);
}

/* ===== 口味整屏图左右拖拽（跟手/粘手）切换 =============================== */
#define UI_EGG_DRAG_COMMIT_PX 96   /* 触发落位切换的最小拖拽距离（px） */
#define UI_EGG_SNAP_MS        180  /* 落位/回弹动画时长 */
#define UI_EGG_ARROW_MS       240  /* 箭头点击切换的滑入时长 */

/* 拖拽开始的准入条件，与 ui_doneness_step 保持一致。 */
#define UI_EGG_DRAG_FRAME_MS  16U  /* Maximum layout update rate: 60 Hz. */

static bool ui_doneness_can_drag(void)
{
    const ui_data_state_t *state;

    if(s_ui.data == NULL || s_ui.panel_open) {
        return false;
    }
    state = ui_data_get_state(s_ui.data);
    if(state == NULL) {
        return false;
    }
    if(!ui_data_state_is_idle(state) || ui_data_state_has_alert(state)) {
        return false;
    }
    return true;
}

/* 把整屏图与信息卡文字复位到中心、恢复不透明度，并隐藏进入图。 */
static void ui_egg_drag_mark_reset(void)
{
    s_ui.egg_drag_rendered_dx = 0;
    s_ui.egg_drag_last_frame_ms = lv_tick_get();
}

static void ui_egg_reset_transforms(void)
{
    lv_obj_t *labels[3] = { s_ui.name_label, s_ui.en_label, s_ui.desc_label };
    size_t i;

    if(s_ui.bg_page != NULL) {
        lv_obj_clear_flag(s_ui.bg_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_translate_x(s_ui.bg_page, 0, 0);
    }
    if(s_ui.bg_page_incoming != NULL) {
        lv_obj_add_flag(s_ui.bg_page_incoming, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_translate_x(s_ui.bg_page_incoming, 0, 0);
    }
    if(s_ui.bg_image != NULL) {
        lv_obj_set_style_opa(s_ui.bg_image, LV_OPA_COVER, 0);
    }
    if(s_ui.bg_image_incoming != NULL) {
        lv_obj_set_style_opa(s_ui.bg_image_incoming, LV_OPA_COVER, 0);
    }
    for(i = 0; i < 3; i++) {
        if(labels[i] != NULL) {
            lv_obj_set_style_translate_x(labels[i], 0, 0);
            lv_obj_set_style_opa(labels[i], LV_OPA_COVER, 0);
        }
    }
    ui_egg_drag_mark_reset();
}

/* 依据当前跟手位移 dx 摆放主图/进入图/文字，并按进度淡出当前文字。 */
static void ui_egg_drag_layout(int dx)
{
    int w = (s_ui.bg_page != NULL) ? lv_obj_get_width(s_ui.bg_page) : 480;

    if(w <= 0) {
        w = 480;
    }

    if(s_ui.bg_page != NULL) {
        lv_obj_set_style_translate_x(s_ui.bg_page, dx, 0);
    }
    if(s_ui.egg_drag_target_idx >= 0 && s_ui.bg_page_incoming != NULL) {
        /* 向左拖（dx<0）取右侧邻档从 +w 处跟入；向右拖取左侧邻档从 -w。 */
        int base = (dx < 0) ? w : -w;
        lv_obj_set_style_translate_x(s_ui.bg_page_incoming, base + dx, 0);
    }
    s_ui.egg_drag_rendered_dx = dx;
    s_ui.egg_drag_last_frame_ms = lv_tick_get();
}

/* 落位完成：切换到目标档位，复位形变，播放蜂鸣并刷新。 */
static void ui_egg_commit_ready_cb(lv_anim_t *a)
{
    int idx = s_ui.egg_drag_target_idx;
    lv_obj_t *tmp;

    LV_UNUSED(a);

    if(idx < 0 || idx >= UI_EGG_TILE_COUNT) {
        ui_egg_reset_transforms();
        s_ui.egg_drag_target_idx = -1;
        return;
    }

    tmp = s_ui.bg_page;
    s_ui.bg_page = s_ui.bg_page_incoming;
    s_ui.bg_page_incoming = tmp;
    tmp = s_ui.bg_image;
    s_ui.bg_image = s_ui.bg_image_incoming;
    s_ui.bg_image_incoming = tmp;
    tmp = s_ui.name_label;
    s_ui.name_label = s_ui.name_label_incoming;
    s_ui.name_label_incoming = tmp;
    tmp = s_ui.en_label;
    s_ui.en_label = s_ui.en_label_incoming;
    s_ui.en_label_incoming = tmp;
    tmp = s_ui.desc_label;
    s_ui.desc_label = s_ui.desc_label_incoming;
    s_ui.desc_label_incoming = tmp;

    s_ui.egg_tile_idx = idx;
    s_ui.bg_image_idx = idx;
    s_ui.egg_tile_shown = idx; /* 抑制 ui_apply 的滑入动画，跟手动画已完成落位 */
    s_ui.egg_drag_target_idx = -1;

    if(s_ui.data != NULL) {
        ui_data_set_doneness(s_ui.data, k_egg_tiles[idx].don);
    }
    buzzer_beep_async(50);

    ui_egg_reset_transforms();
    ui_refresh();
}

/* 回弹完成：隐藏进入图并复位。 */
static void ui_egg_cancel_ready_cb(lv_anim_t *a)
{
    LV_UNUSED(a);
    s_ui.egg_drag_target_idx = -1;
    ui_egg_reset_transforms();
}

/* 从对象当前 translate_x 起，平滑动画到目标位移。 */
static void ui_egg_anim_translate(lv_obj_t *o, int32_t to, lv_anim_ready_cb_t rdy)
{
    lv_anim_t a;

    if(o == NULL) {
        return;
    }
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, ui_anim_translate_x_cb);
    lv_anim_set_values(&a, lv_obj_get_style_translate_x(o, 0), to);
    lv_anim_set_duration(&a, UI_EGG_SNAP_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    if(rdy != NULL) {
        lv_anim_set_ready_cb(&a, rdy);
    }
    lv_anim_start(&a);
}

/* 松手：越过阈值则落位到邻档，否则回弹到当前档。 */
static void ui_egg_drag_finish(int dx)
{
    int w = (s_ui.bg_page != NULL) ? lv_obj_get_width(s_ui.bg_page) : 480;
    int adx = (dx < 0) ? -dx : dx;

    if(w <= 0) {
        w = 480;
    }

    if(s_ui.egg_drag_target_idx >= 0 && adx >= UI_EGG_DRAG_COMMIT_PX) {
        /* 落位：主图滑出、进入图归位、文字淡出。 */
        int out = (dx < 0) ? -w : w;
        ui_egg_anim_translate(s_ui.bg_page, out, NULL);
        ui_egg_anim_translate(s_ui.bg_page_incoming, 0, ui_egg_commit_ready_cb);
    } else {
        /* 回弹：主图/文字复位，进入图退回边缘后隐藏。 */
        ui_egg_anim_translate(s_ui.bg_page, 0, NULL);
        if(s_ui.egg_drag_target_idx >= 0 && s_ui.bg_page_incoming != NULL) {
            int base = (dx < 0) ? w : -w;
            ui_egg_anim_translate(s_ui.bg_page_incoming, base, ui_egg_cancel_ready_cb);
        } else {
            ui_egg_cancel_ready_cb(NULL);
        }
    }
}

/* 箭头点击切换：复用拖拽的整屏滑动。dir>0 下一档（新图自右滑入），dir<0 上一档。
 * 落位复用 ui_egg_commit_ready_cb，完成页面指针交换/设档/蜂鸣/刷新。 */
static void ui_doneness_slide(int target_idx, int dir)
{
    int w = (s_ui.bg_page != NULL) ? lv_obj_get_width(s_ui.bg_page) : 480;
    int base;

    if(target_idx < 0 || target_idx >= UI_EGG_TILE_COUNT) {
        return;
    }

    /* 低资源设备采用无动画切换：只提交目标档位并绘制一次最终画面。 */
    LV_UNUSED(dir);
    s_ui.egg_tile_idx = target_idx;
    if(s_ui.data != NULL) {
        ui_data_set_doneness(s_ui.data, k_egg_tiles[target_idx].don);
    }
    buzzer_beep_async(50);
    ui_refresh();
    return;

    if(w <= 0) {
        w = 480;
    }
    /* 打断在途动画并回到稳态，再从稳态起滑。 */
    lv_anim_delete(s_ui.bg_page, ui_anim_translate_x_cb);
    lv_anim_delete(s_ui.bg_page_incoming, ui_anim_translate_x_cb);
    ui_egg_reset_transforms();

    s_ui.egg_drag_target_idx = target_idx;
    if(s_ui.bg_image_incoming != NULL) {
        lv_image_set_src(s_ui.bg_image_incoming, ui_egg_tile_img(target_idx));
        ui_label_set_text_if_changed(s_ui.name_label_incoming, k_egg_tiles[target_idx].name);
        ui_label_set_text_if_changed(s_ui.en_label_incoming, k_egg_tiles[target_idx].en);
        ui_label_set_text_if_changed(s_ui.desc_label_incoming, k_egg_tiles[target_idx].desc);
    }
    /* 单层平移：仅新图从侧边滑入覆盖旧图，旧图保持静止不重绘，
     * 每帧重绘量约减半，在 N16R2 的 Quad PSRAM 上更顺。需确保进入图在顶层。 */
    base = (dir > 0) ? w : -w;
    if(s_ui.bg_page_incoming != NULL) {
        lv_obj_clear_flag(s_ui.bg_page_incoming, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_ui.bg_page_incoming);
        lv_obj_set_style_translate_x(s_ui.bg_page_incoming, base, 0);
    }
    /* 箭头切换用更长的 ease_in_out，起止都平滑，避免观感过快生硬。 */
    {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_ui.bg_page_incoming);
        lv_anim_set_exec_cb(&a, ui_anim_translate_x_cb);
        lv_anim_set_values(&a, base, 0);
        lv_anim_set_duration(&a, UI_EGG_ARROW_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_set_ready_cb(&a, ui_egg_commit_ready_cb);
        lv_anim_start(&a);
    }
}

static void ui_doneness_drag_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p;
    int dx;
    int want;
    int w;
    bool target_changed;

    /* 回调挂在常驻 screen 上，离开煎蛋主页后其他页面的触摸仍会触发；
     * 此时 s_ui 里的对象指针已随 ui_screen_reset 失效，必须直接忽略，
     * 否则 PRESSING 里 lv_obj_get_width(s_ui.bg_image) 会访问野指针。 */
    if(!s_ui.active) {
        s_ui.egg_drag_active = false;
        return;
    }

    if(indev == NULL) {
        return;
    }

    /* 无动画手势：按下时只记录坐标，松手越过阈值后直接切换最终画面。
     * PRESSING 期间不移动或合成两张整屏图片。 */
    if(code == LV_EVENT_PRESSED) {
        if(!ui_doneness_can_drag()) {
            s_ui.egg_drag_active = false;
            return;
        }
        lv_indev_get_point(indev, &p);
        s_ui.egg_drag_active = true;
        s_ui.egg_drag_start_x = p.x;
        s_ui.egg_drag_from_idx = s_ui.egg_tile_idx;
        return;
    }
    if(code == LV_EVENT_PRESSING) {
        return;
    }
    if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if(!s_ui.egg_drag_active) {
            return;
        }
        s_ui.egg_drag_active = false;
        lv_indev_get_point(indev, &p);
        dx = p.x - s_ui.egg_drag_start_x;
        if(dx <= -UI_EGG_DRAG_COMMIT_PX &&
           s_ui.egg_drag_from_idx < UI_EGG_TILE_COUNT - 1) {
            ui_doneness_slide(s_ui.egg_drag_from_idx + 1, 1);
        } else if(dx >= UI_EGG_DRAG_COMMIT_PX && s_ui.egg_drag_from_idx > 0) {
            ui_doneness_slide(s_ui.egg_drag_from_idx - 1, -1);
        }
        return;
    }

    if(code == LV_EVENT_PRESSED) {
        if(!ui_doneness_can_drag()) {
            s_ui.egg_drag_active = false;
            return;
        }
        /* 打断可能在途的落位/回弹动画，从当前视觉状态重新起拖。 */
        lv_anim_delete(s_ui.bg_page, ui_anim_translate_x_cb);
        lv_anim_delete(s_ui.bg_page_incoming, ui_anim_translate_x_cb);
        lv_anim_delete(s_ui.name_label, ui_anim_translate_x_cb);
        lv_anim_delete(s_ui.en_label, ui_anim_translate_x_cb);
        lv_anim_delete(s_ui.desc_label, ui_anim_translate_x_cb);
        lv_anim_delete(s_ui.bg_image, ui_anim_opa_cb);
        lv_anim_delete(s_ui.name_label, ui_anim_opa_cb);
        lv_anim_delete(s_ui.en_label, ui_anim_opa_cb);
        lv_anim_delete(s_ui.desc_label, ui_anim_opa_cb);
        ui_egg_reset_transforms();
        lv_indev_get_point(indev, &p);
        s_ui.egg_drag_active = true;
        s_ui.egg_drag_start_x = p.x;
        s_ui.egg_drag_from_idx = s_ui.egg_tile_idx;
        s_ui.egg_drag_target_idx = -1;
        return;
    }

    if(!s_ui.egg_drag_active) {
        return;
    }

    if(code == LV_EVENT_PRESSING) {
        lv_indev_get_point(indev, &p);
        dx = p.x - s_ui.egg_drag_start_x;
        target_changed = false;
        w = (s_ui.bg_page != NULL) ? lv_obj_get_width(s_ui.bg_page) : 480;
        if(w <= 0) {
            w = 480;
        }

        /* 判定跟手方向对应的邻档；无邻档则做橡皮筋阻尼。 */
        want = -1;
        if(dx < 0 && s_ui.egg_drag_from_idx < UI_EGG_TILE_COUNT - 1) {
            want = s_ui.egg_drag_from_idx + 1;
        } else if(dx > 0 && s_ui.egg_drag_from_idx > 0) {
            want = s_ui.egg_drag_from_idx - 1;
        }

        if(want < 0) {
            /* 边界档：阻尼衰减并隐藏进入图。 */
            dx /= 3;
            target_changed = (s_ui.egg_drag_target_idx >= 0);
            if(target_changed && s_ui.bg_page_incoming != NULL) {
                lv_obj_add_flag(s_ui.bg_page_incoming, LV_OBJ_FLAG_HIDDEN);
            }
            s_ui.egg_drag_target_idx = -1;
        } else if(want != s_ui.egg_drag_target_idx) {
            target_changed = true;
            s_ui.egg_drag_target_idx = want;
            if(s_ui.bg_image_incoming != NULL) {
                lv_image_set_src(s_ui.bg_image_incoming, ui_egg_tile_img(want));
                ui_label_set_text_if_changed(s_ui.name_label_incoming,
                                             k_egg_tiles[want].name);
                ui_label_set_text_if_changed(s_ui.en_label_incoming,
                                             k_egg_tiles[want].en);
                ui_label_set_text_if_changed(s_ui.desc_label_incoming,
                                             k_egg_tiles[want].desc);
                lv_obj_clear_flag(s_ui.bg_page_incoming, LV_OBJ_FLAG_HIDDEN);
            }
        }

        if(dx > w) {
            dx = w;
        } else if(dx < -w) {
            dx = -w;
        }
        if(dx != s_ui.egg_drag_rendered_dx &&
           (target_changed ||
            lv_tick_elaps(s_ui.egg_drag_last_frame_ms) >= UI_EGG_DRAG_FRAME_MS)) {
            ui_egg_drag_layout(dx);
        }
        return;
    }

    if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_ui.egg_drag_active = false;
        lv_indev_get_point(indev, &p);
        ui_egg_drag_finish(p.x - s_ui.egg_drag_start_x);
        return;
    }
}

/* ===== 设计稿弹窗：数量选择面板 + 预约时间面板 ============================ */
#define UI_QTY_MAX ((int)UI_MAX_MAKE_QUANTITY)
#define UI_RESERVE_HOUR_DEFAULT 8   /* 打开面板时的默认整点 */
#define UI_RESERVE_STEP_MINUTES 30
#define UI_QTY_PANEL_H_MAKE UI_OVERLAY_PANEL_H
#define UI_QTY_PANEL_H_RESERVE UI_OVERLAY_PANEL_H
#define UI_QTY_ROW_Y_MAKE 72
#define UI_QTY_ROW_Y_RESERVE 44
#define UI_RESERVE_SECTION_Y 90
#define UI_RESERVE_SECTION_W 400
#define UI_RESERVE_SECTION_H 54
#define UI_RESERVE_ADJUST_BTN_W 36
#define UI_RESERVE_ADJUST_BTN_H 28
#define UI_RESERVE_TIME_DISPLAY_W 168
#define UI_RESERVE_TIME_DISPLAY_H 52
#define UI_RESERVE_CONTROL_GAP 6
#define UI_RESERVE_TIME_GAP 10

/* 把 0~23 的小时拆成 12 小时制显示与 AM/PM。 */
static int ui_reserve_hour12(int hour)
{
    if(hour == 0) {
        return 12;
    }
    return (hour > 12) ? (hour - 12) : hour;
}

static const char *ui_reserve_meridiem(int hour)
{
    return (hour < 12) ? "AM" : "PM";
}

static void ui_style_pill(lv_obj_t *btn, bool selected)
{
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if(selected) {
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(UI_C_PRIMARY_TOP), 0);
        lv_obj_set_style_bg_grad_color(btn, lv_color_hex(UI_C_PRIMARY_BOTTOM), 0);
        lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        if(lbl != NULL) {
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x422006), 0);
        }
    } else {
        lv_obj_set_style_bg_opa(btn, LV_OPA_40, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1E293B), 0);
        lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(UI_C_ACCENT), 0);
        lv_obj_set_style_border_opa(btn, LV_OPA_40, 0);
        if(lbl != NULL) {
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        }
    }
}

static void ui_refresh_qty_selection(void)
{
    int i;
    const ui_data_state_t *state = ui_get_state();
    int max_quantity = state != NULL ?
                       (int)ui_data_max_make_quantity(state->doneness) : UI_QTY_MAX;

    if(s_ui.qty_selected > max_quantity) {
        s_ui.qty_selected = max_quantity;
    }

    for(i = 0; i < UI_QTY_MAX; i++) {
        if(s_ui.qty_num_btns[i] != NULL) {
            bool enabled = (i + 1) <= max_quantity;

            ui_style_pill(s_ui.qty_num_btns[i],
                          enabled && (i + 1) == s_ui.qty_selected);
            if(enabled) {
                lv_obj_remove_state(s_ui.qty_num_btns[i], LV_STATE_DISABLED);
                lv_obj_set_style_opa(s_ui.qty_num_btns[i], LV_OPA_COVER, 0);
            } else {
                lv_obj_add_state(s_ui.qty_num_btns[i], LV_STATE_DISABLED);
                lv_obj_set_style_opa(s_ui.qty_num_btns[i], LV_OPA_30, 0);
            }
        }
    }
}

static void ui_update_reserve_label(void)
{
    if(s_ui.reserve_label == NULL) {
        return;
    }

    if(s_ui.reserve_hour < 0) {
        lv_label_set_text(s_ui.reserve_label, u8"预约");
        lv_obj_set_style_text_font(s_ui.reserve_label, &ui_egg_btn_20, 0);
        lv_obj_set_style_text_letter_space(s_ui.reserve_label, 4, 0);
    } else {
        char buf[48];
        int q = (s_ui.reserve_qty >= 1) ? s_ui.reserve_qty : 1;
        /* 主页按钮采用 24 小时制 "08:00 · N枚"（ui_cook_font_20 含冒号）。 */
        snprintf(buf, sizeof(buf), u8"%02d:%02d · %d枚",
                 s_ui.reserve_hour % 24, s_ui.reserve_minute, q);
        lv_label_set_text(s_ui.reserve_label, buf);
        lv_obj_set_style_text_font(s_ui.reserve_label, &ui_cook_font_20, 0);
        lv_obj_set_style_text_letter_space(s_ui.reserve_label, 1, 0);
    }
}

/* 刷新面板中央的大号时间与 AM/PM。 */
static void ui_sync_reservation_from_remote(const ui_uart_heartbeat_state_t *heartbeat)
{
    int hour = -1;
    int minute = 0;
    int quantity = 1;

    if(heartbeat == NULL || !heartbeat->has_remote_reservation) {
        return;
    }

    if(heartbeat->remote_reservation_active &&
       heartbeat->remote_reservation_epoch >= 1704067200UL &&
       heartbeat->remote_reservation_qty >= 1U &&
       heartbeat->remote_reservation_qty <= UI_QTY_MAX) {
        time_t reserved_time = (time_t)heartbeat->remote_reservation_epoch;
        struct tm local_reserved;
        localtime_r(&reserved_time, &local_reserved);
        hour = local_reserved.tm_hour;
        minute = local_reserved.tm_min;
        quantity = heartbeat->remote_reservation_qty;
    }

    if(s_ui.reserve_hour != hour || s_ui.reserve_minute != minute ||
       s_ui.reserve_qty != quantity) {
        s_ui.reserve_hour = hour;
        s_ui.reserve_minute = minute;
        s_ui.reserve_qty = quantity;
        ui_update_reserve_label();
    }
}

static void ui_update_reserve_time_display(void)
{
    char buf[8];
    int hour = s_ui.reserve_edit_hour;

    if(s_ui.reserve_time_label != NULL) {
        snprintf(buf, sizeof(buf), "%02d:%02d",
                 ui_reserve_hour12(hour), s_ui.reserve_edit_minute);
        lv_label_set_text(s_ui.reserve_time_label, buf);
    }
    if(s_ui.reserve_meridiem_label != NULL) {
        lv_label_set_text(s_ui.reserve_meridiem_label, ui_reserve_meridiem(hour));
    }
}

static void ui_open_qty_panel(int intent)
{
    const ui_data_state_t *state = ui_get_state();

    if(s_ui.qty_panel == NULL) {
        return;
    }

    if(state == NULL) {
        ESP_LOGW("ui_home", "qty panel ignored: state unavailable");
        return;
    }

    /* A completed local countdown retains total_s until it is acknowledged.
     * Treat the next Start/Reserve tap as that acknowledgement, then continue
     * opening the requested panel instead of silently rejecting the tap. */
    if(ui_data_state_is_finished(state)) {
        (void)ui_data_ack_finished(s_ui.data);
        state = ui_get_state();
    }

    /* Emergency stop recovery is actionable from either main entry. */
    if(state != NULL &&
       (state->maker_state == UI_MAKER_STOPPING ||
        state->maker_state == UI_MAKER_STOPPED_NEEDS_RECOVERY)) {
        ESP_LOGI("ui_home", "qty panel redirected to stop recovery: state=%d",
                 (int)state->maker_state);
        ui_open_cancel_notice();
        return;
    }

    if(state == NULL || !ui_data_state_is_idle(state) || ui_data_state_has_alert(state)) {
        ESP_LOGW("ui_home",
                 "qty panel blocked: intent=%d start=%d total=%u maker=%d err=%d",
                 intent,
                 state != NULL ? (int)state->start : -1,
                 state != NULL ? (unsigned)state->total_s : 0U,
                 state != NULL ? (int)state->maker_state : -1,
                 state != NULL ? (int)state->error_code : -1);
        buzzer_beep_async(100);
        return;
    }

    ESP_LOGI("ui_home", "opening qty panel: intent=%d", intent);

    /* 默认数量：沿用当前 needMake（1~6），否则 1。 */
    if(intent == 1 && s_ui.reserve_hour >= 0 &&
       s_ui.reserve_qty >= 1 && s_ui.reserve_qty <= UI_QTY_MAX) {
        s_ui.qty_selected = s_ui.reserve_qty;
    } else {
        s_ui.qty_selected = (state->needMake >= 1U && state->needMake <= UI_QTY_MAX) ?
                            (int)state->needMake : 1;
    }
    s_ui.qty_panel_intent = intent;
    ui_refresh_qty_selection();

    /* 标题随意图切换。 */
    if(s_ui.qty_title_label != NULL) {
        lv_label_set_text(s_ui.qty_title_label, intent == 1 ? u8"预约时间" : u8"选择数量");
    }
    if(s_ui.qty_num_row != NULL) {
        lv_obj_align(s_ui.qty_num_row, LV_ALIGN_TOP_MID, 0,
                     intent == 1 ? UI_QTY_ROW_Y_RESERVE : UI_QTY_ROW_Y_MAKE);
    }

    /* 意图分流：制作路径显示“开始”，预约路径显示步进器+“预约”确认。 */
    /* 隐藏的 flex 子项不参与布局，面板高度随之收放。 */
    if(s_ui.qty_reserve_section != NULL) {
        if(intent == 1) {
            uint8_t saved_hour = UI_RESERVE_HOUR_DEFAULT;
            uint8_t saved_minute = 0U;

            if(s_ui.reserve_hour >= 0) {
                s_ui.reserve_edit_hour = s_ui.reserve_hour;
                s_ui.reserve_edit_minute = s_ui.reserve_minute;
            } else if(data_manager_get_last_reservation_time(&saved_hour, &saved_minute)) {
                s_ui.reserve_edit_hour = saved_hour;
                s_ui.reserve_edit_minute = saved_minute;
            } else {
                s_ui.reserve_edit_hour = UI_RESERVE_HOUR_DEFAULT;
                s_ui.reserve_edit_minute = 0;
            }
            ui_update_reserve_time_display();
            lv_obj_clear_flag(s_ui.qty_reserve_section, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ui.qty_reserve_section, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if(s_ui.qty_cook_btn != NULL) {
        if(intent == 0) {
            lv_obj_clear_flag(s_ui.qty_cook_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ui.qty_cook_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if(s_ui.qty_reserve_btn != NULL) {
        if(intent == 1) {
            lv_obj_clear_flag(s_ui.qty_reserve_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ui.qty_reserve_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    /* 制作和预约都在操作行左侧显示取消按钮。 */
    if(s_ui.qty_reserve_cancel_btn != NULL) {
        lv_obj_clear_flag(s_ui.qty_reserve_cancel_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if(s_ui.qty_reserve_cancel_label != NULL) {
        lv_label_set_text(s_ui.qty_reserve_cancel_label,
                          (intent == 1 && s_ui.reserve_hour >= 0) ?
                          u8"取消预约" : u8"取消");
    }
    lv_obj_set_height(s_ui.qty_panel, intent == 1 ? UI_QTY_PANEL_H_RESERVE : UI_QTY_PANEL_H_MAKE);
    lv_obj_align(s_ui.qty_panel, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_clear_flag(s_ui.qty_mask, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_ui.qty_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_ui.qty_mask);
    lv_obj_move_foreground(s_ui.qty_panel);
    s_ui.panel_open = true;
    ui_apply_debug_button_visibility();
}

static void ui_close_qty_panel(void)
{
    if(s_ui.qty_panel == NULL) {
        return;
    }
    lv_obj_add_flag(s_ui.qty_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.qty_mask, LV_OBJ_FLAG_HIDDEN);
    s_ui.panel_open = false;
}

static void ui_set_need_make_target(int target)
{
    const ui_data_state_t *state = ui_get_state();
    int max_quantity = state != NULL ?
                       (int)ui_data_max_make_quantity(state->doneness) : UI_QTY_MAX;

    if(target >= 1 && target <= max_quantity && s_ui.data != NULL) {
        (void)ui_data_set_need_make(s_ui.data, (uint8_t)target);
    }
}

static void ui_qty_num_cb(lv_event_t *e)
{
    int n = (int)(intptr_t)lv_event_get_user_data(e);
    const ui_data_state_t *state = ui_get_state();
    int max_quantity = state != NULL ?
                       (int)ui_data_max_make_quantity(state->doneness) : UI_QTY_MAX;

    if(n >= 1 && n <= max_quantity) {
        s_ui.qty_selected = n;
        ui_refresh_qty_selection();
        buzzer_beep_async(40);
    }
}

static void ui_qty_cook_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    ui_reset_debug_unlock_sequence();
    ui_set_need_make_target(s_ui.qty_selected);
    ui_close_qty_panel();
    if(s_ui.data != NULL && ui_data_start(s_ui.data)) {
        buzzer_beep_async(50);
    }
    ui_refresh();
}

static void ui_qty_reserve_cb(lv_event_t *e)
{
    const ui_data_state_t *state;
    time_t now;
    time_t reserved_time;
    struct tm local_reserved;
    esp_err_t err;

    LV_UNUSED(e);

    ui_reset_debug_unlock_sequence();
    state = ui_get_state();
    now = time(NULL);
    if(state == NULL || now < (time_t)1704067200) {
        buzzer_beep_async(100);
        ui_close_qty_panel();
        ui_open_time_panel();
        return;
    }

    localtime_r(&now, &local_reserved);
    local_reserved.tm_hour = s_ui.reserve_edit_hour;
    local_reserved.tm_min = s_ui.reserve_edit_minute;
    local_reserved.tm_sec = 0;
    reserved_time = mktime(&local_reserved);
    if(reserved_time < now + 30) {
        local_reserved.tm_mday += 1;
        reserved_time = mktime(&local_reserved);
    }
    if(reserved_time < (time_t)1704067200 || reserved_time > (time_t)UINT32_MAX) {
        buzzer_beep_async(100);
        return;
    }

    err = ui_uart_bridge_set_reservation((uint32_t)reserved_time,
                                         (uint8_t)s_ui.qty_selected,
                                         state->oil_mode,
                                         state->doneness);
    if(err != ESP_OK) {
        ESP_LOGW("ui_home", "reservation enqueue failed: %s", esp_err_to_name(err));
        buzzer_beep_async(100);
        return;
    }

    err = data_manager_set_last_reservation_time((uint8_t)s_ui.reserve_edit_hour,
                                                 (uint8_t)s_ui.reserve_edit_minute);
    if(err != ESP_OK) {
        ESP_LOGW("ui_home", "reservation preference save failed: %s",
                 esp_err_to_name(err));
    }

    buzzer_beep_async(40);
    /* 预约选择器已内嵌在本面板：直接确认预约并关闭。 */
    ui_close_qty_panel();
}

static void ui_qty_cancel_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);

    buzzer_beep_async(40);
    /* 点击操作按钮才取消已有预约；点击弹窗外遮罩只关闭面板。 */
    if(target == s_ui.qty_reserve_cancel_btn &&
       s_ui.qty_panel_intent == 1 && s_ui.reserve_hour >= 0) {
        esp_err_t err = ui_uart_bridge_cancel_reservation();
        if(err != ESP_OK) {
            ESP_LOGW("ui_home", "reservation cancel enqueue failed: %s", esp_err_to_name(err));
            buzzer_beep_async(100);
            return;
        }
    }
    ui_close_qty_panel();
}

static void ui_reserve_minus_cb(lv_event_t *e)
{
    int total_minutes;

    LV_UNUSED(e);
    buzzer_beep_async(40);
    total_minutes = s_ui.reserve_edit_hour * 60 + s_ui.reserve_edit_minute;
    total_minutes = (total_minutes - UI_RESERVE_STEP_MINUTES + 24 * 60) % (24 * 60);
    s_ui.reserve_edit_hour = total_minutes / 60;
    s_ui.reserve_edit_minute = total_minutes % 60;
    ui_update_reserve_time_display();
}

static void ui_reserve_plus_cb(lv_event_t *e)
{
    int total_minutes;

    LV_UNUSED(e);
    buzzer_beep_async(40);
    total_minutes = s_ui.reserve_edit_hour * 60 + s_ui.reserve_edit_minute;
    total_minutes = (total_minutes + UI_RESERVE_STEP_MINUTES) % (24 * 60);
    s_ui.reserve_edit_hour = total_minutes / 60;
    s_ui.reserve_edit_minute = total_minutes % 60;
    ui_update_reserve_time_display();
}

static void ui_start_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    ui_reset_debug_unlock_sequence();
    /* 设计稿流程：点“开始制作”先弹数量选择面板。 */
    ui_open_qty_panel(0);
}

static void ui_reserve_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    ui_reset_debug_unlock_sequence();
    /* 设计稿流程：点“预约”同样先弹数量面板（高亮预约时间）。 */
    ui_open_qty_panel(1);
}

static bool ui_should_show_cancel_notice(const ui_data_state_t *state)
{
    ui_uart_heartbeat_state_t hb;

    if(state != NULL &&
       (state->start ||
        (state->maker_state >= UI_MAKER_CHECKING_TMP_SENSOR &&
         state->maker_state <= UI_MAKER_MAKING))) {
        return true;
    }

    ui_uart_bridge_get_heartbeat_state(&hb);
    if(hb.online &&
       (hb.remote_start ||
        (hb.has_remote_maker_state &&
         hb.remote_maker_state >= (int32_t)UI_MAKER_CHECKING_TMP_SENSOR &&
         hb.remote_maker_state <= (int32_t)UI_MAKER_MAKING))) {
        return true;
    }

    return false;
}

static void ui_open_cancel_confirmation(void)
{
    if(s_ui.data == NULL) {
        return;
    }

    s_ui.active = false;
    ui_stop_countdown_timer();
    ui_stop_blink_timer();
    ui_stop_conn_check_timer();
    ui_notice_create_with_two_actions(s_ui.data,
                                      ui_t(UI_STR_NOTICE),
                                      ui_t(UI_STR_CANCEL_CONFIRM_TIP),
                                      ui_t(UI_STR_CONFIRM_CANCEL),
                                      ui_confirm_cancel,
                                      NULL,
                                      ui_t(UI_STR_CONTINUE_MAKING),
                                      ui_continue_making,
                                      NULL);
    ui_apply_debug_button_visibility();
}

static bool ui_confirm_cancel(void *user_data)
{
    const ui_data_state_t *state = ui_get_state();
    ui_uart_heartbeat_state_t hb;

    LV_UNUSED(user_data);
    ui_uart_bridge_get_heartbeat_state(&hb);

    if(!hb.online || state == NULL) {
        buzzer_beep_async(100);
        return false;
    }

    if(ui_should_show_cancel_notice(state)) {
        const char *stop_command = state->doneness == UI_DON_SUNNY ? "sun.stop" : "STA 0";
        if(ui_uart_bridge_send_raw_command(stop_command, true) != ESP_OK) {
            buzzer_beep_async(100);
            return false;
        }
        s_cancel_stop_requested = true;
        return true;
    }

    if(state->maker_state == UI_MAKER_IDLE && !state->start && state->needMake > 0U) {
        esp_err_t clear_err = state->doneness == UI_DON_SUNNY ?
                              ui_uart_bridge_send_raw_command("sun.clear", true) :
                              ui_uart_bridge_clear_need_make(state->needMake);
        if(clear_err != ESP_OK ||
           !ui_data_clear_need_make(s_ui.data)) {
            buzzer_beep_async(100);
            return false;
        }
    }

    return true;
}

static bool ui_continue_making(void *user_data)
{
    LV_UNUSED(user_data);
    s_cancel_stop_requested = false;
    return true;
}

static void ui_open_cancel_notice(void)
{
    if(s_ui.data == NULL) {
        return;
    }

    s_ui.active = false;
    ui_stop_countdown_timer();
    ui_stop_blink_timer();
    ui_stop_conn_check_timer();
    ui_notice_create_with_action(s_ui.data,
                                 ui_t(UI_STR_NOTICE),
                                 ui_t(UI_STR_CANCEL_TIP),
                                 ui_t(UI_STR_CONFIRM),
                                 ui_confirm_emergency_recovery,
                                 NULL);
    ui_apply_debug_button_visibility();
}

static bool ui_confirm_emergency_recovery(void *user_data)
{
    const ui_data_state_t *state = ui_get_state();
    ui_uart_heartbeat_state_t hb;

    LV_UNUSED(user_data);
    ui_uart_bridge_get_heartbeat_state(&hb);

    if(!hb.online || state == NULL) {
        buzzer_beep_async(100);
        return false;
    }

    if(state->maker_state == UI_MAKER_STOPPING) {
        // 任务尚未完全退出，保持提示页，避免过早解除停机锁。
        buzzer_beep_async(100);
        return false;
    }

    if(state->maker_state == UI_MAKER_STOPPED_NEEDS_RECOVERY) {
        if(ui_uart_bridge_send_raw_command("REC 1", true) != ESP_OK) {
            buzzer_beep_async(100);
            return false;
        }
        return true;
    }

    return state->maker_state == UI_MAKER_IDLE;
}

/* ===== 紧急停止 / 取消后续（制作中浮层操作） =====
 * 紧急停止：立即停机（“STA 0”，当前蛋送回），停稳后走既有的
 * “待清理确认”提示页（s_cancel_stop_requested + REC 流程）。
 * 取消后续：当前这枚做完后自动结束（大数量 SUB 借助 MainControl
 * 的 needMakeFloor 下限钳制，不会影响正在制作的这枚蛋）。 */

static void ui_cook_estop_cb(lv_event_t *e)
{
    const ui_data_state_t *state;

    LV_UNUSED(e);

    buzzer_beep_async(50);

    ui_reset_debug_unlock_sequence();

    if(s_ui.data == NULL) {
        return;
    }

    state = ui_get_state();
    if(state != NULL &&
       (state->maker_state == UI_MAKER_STOPPING ||
        state->maker_state == UI_MAKER_STOPPED_NEEDS_RECOVERY)) {
        /* 已在取消中/待确认：直接给出既有提示页。 */
        s_cancel_stop_requested = false;
        ui_open_cancel_notice();
        return;
    }

    ui_open_cancel_confirmation();
}

static void ui_cook_cancel_rest_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);

    ui_reset_debug_unlock_sequence();

    if(s_ui.data == NULL) {
        return;
    }

    ui_open_cancel_rest_confirmation();
}

static void ui_open_cancel_rest_confirmation(void)
{
    if(s_ui.data == NULL) {
        return;
    }

    s_ui.active = false;
    ui_stop_countdown_timer();
    ui_stop_blink_timer();
    ui_stop_conn_check_timer();
    ui_notice_create_with_two_actions(s_ui.data,
                                      ui_t(UI_STR_NOTICE),
                                      ui_t(UI_STR_CANCEL_REST_TIP),
                                      ui_t(UI_STR_CONFIRM),
                                      ui_confirm_cancel_rest,
                                      NULL,
                                      ui_t(UI_STR_CONTINUE_MAKING),
                                      ui_continue_making,
                                      NULL);
    ui_apply_debug_button_visibility();
}

static bool ui_confirm_cancel_rest(void *user_data)
{
    const ui_data_state_t *state = ui_get_state();
    ui_uart_heartbeat_state_t hb;

    LV_UNUSED(user_data);
    ui_uart_bridge_get_heartbeat_state(&hb);

    if(!hb.online || state == NULL) {
        buzzer_beep_async(100);
        return false;
    }

    /* 大数量 SUB：MainControl 侧 needMakeFloor 会把正在制作的
     * 这枚保住（下限钳制），剩余数量清零后本轮自动收尾。 */
    if(ui_uart_bridge_clear_need_make(255U) != ESP_OK) {
        buzzer_beep_async(100);
        return false;
    }

    return true;
}

static void ui_apply_debug_button_visibility(void)
{
    const bool show_debug = s_debug_mode_unlocked && !ui_notice_is_active();

    if(s_ui.debug_button == NULL) {
        return;
    }

    if(show_debug) {
        lv_obj_clear_flag(s_ui.debug_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_ui.debug_button);
    } else {
        lv_obj_add_flag(s_ui.debug_button, LV_OBJ_FLAG_HIDDEN);
    }

    ui_apply_debug_metrics_visibility();
}

static void ui_apply_debug_metrics_visibility(void)
{
    const bool show_debug = s_debug_mode_unlocked && !ui_notice_is_active();
    lv_obj_t *metrics[] = {
        s_ui.potb_temp_label,
        s_ui.pota_temp_label,
        s_ui.status_box,
    };

    for(size_t i = 0; i < sizeof(metrics) / sizeof(metrics[0]); i++) {
        if(metrics[i] == NULL) {
            continue;
        }
        if(show_debug) {
            lv_obj_clear_flag(metrics[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(metrics[i]);
        } else {
            lv_obj_add_flag(metrics[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void ui_reset_debug_unlock_sequence(void)
{
    s_ui.debug_unlock_tap_count = 0U;
    s_ui.debug_unlock_last_tap_ms = 0U;
}

static void ui_track_cancel_debug_unlock_tap(void)
{
    if(s_ui.debug_unlock_tap_count == 0U ||
       lv_tick_elaps(s_ui.debug_unlock_last_tap_ms) > UI_DEBUG_UNLOCK_TAP_GAP_MS) {
        s_ui.debug_unlock_tap_count = 1U;
    } else if(s_ui.debug_unlock_tap_count < UINT8_MAX) {
        s_ui.debug_unlock_tap_count++;
    }

    s_ui.debug_unlock_last_tap_ms = lv_tick_get();

    if(s_ui.debug_unlock_tap_count >= UI_DEBUG_UNLOCK_TAP_COUNT) {
        if(s_debug_mode_unlocked) {
            ui_lock_debug_mode();
        } else {
            ui_unlock_debug_mode();
        }
    }
}

static void ui_lock_debug_mode(void)
{
    s_debug_mode_unlocked = false;
    ui_reset_debug_unlock_sequence();
    ui_apply_debug_button_visibility();
}

static void ui_unlock_debug_mode(void)
{
    s_debug_mode_unlocked = true;
    ui_reset_debug_unlock_sequence();
    ui_apply_debug_button_visibility();
}

static void ui_brand_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    /* 隐藏的 debug 解锁手势：连续点击顶部品牌标题（原取消按钮已移除）。 */
    ui_track_cancel_debug_unlock_tap();
}

static void ui_debug_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    s_ui.active = false;
    ui_stop_countdown_timer();
    ui_stop_blink_timer();
    ui_stop_conn_check_timer();
    ui_debug_create(s_ui.data);
}

static void ui_settings_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    s_ui.active = false;
    ui_stop_countdown_timer();
    ui_stop_blink_timer();
    ui_stop_conn_check_timer();
    ui_settings_create(s_ui.data);
}

static void ui_build_settings_icon(lv_obj_t *parent)
{
    static const int8_t k_offsets[3] = {-7, 0, 7};

    for(int i = 0; i < 3; i++) {
        lv_obj_t *dot = lv_obj_create(parent);
        ui_reset_base_obj(dot);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(dot, 4, 4);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_align(dot, LV_ALIGN_CENTER, k_offsets[i], 0);
    }
}

static void ui_build_clean_icon(lv_obj_t *parent)
{
    static const lv_point_precise_t k_handle_pts[] = {{1, 0}, {1, 9}};
    static const lv_point_precise_t k_head_pts[] = {
        {4, 0}, {16, 0}, {20, 13}, {0, 13}, {4, 0},
    };
    static const lv_point_precise_t k_band_pts[] = {{0, 0}, {14, 0}};
    static const lv_point_precise_t k_bristle_pts[] = {{1, 0}, {1, 4}};
    const lv_color_t color = lv_color_hex(0xFFFFFF);

    lv_obj_t *handle = lv_line_create(parent);
    lv_line_set_points(handle, k_handle_pts, 2);
    lv_obj_set_style_line_width(handle, 2, 0);
    lv_obj_set_style_line_color(handle, color, 0);
    lv_obj_set_style_line_rounded(handle, true, 0);
    lv_obj_clear_flag(handle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(handle, LV_ALIGN_TOP_MID, 0, 3);

    lv_obj_t *head = lv_line_create(parent);
    lv_line_set_points(head, k_head_pts, 5);
    lv_obj_set_style_line_width(head, 2, 0);
    lv_obj_set_style_line_color(head, color, 0);
    lv_obj_set_style_line_rounded(head, true, 0);
    lv_obj_clear_flag(head, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(head, LV_ALIGN_CENTER, 0, 2);

    lv_obj_t *band = lv_line_create(parent);
    lv_line_set_points(band, k_band_pts, 2);
    lv_obj_set_style_line_width(band, 2, 0);
    lv_obj_set_style_line_color(band, color, 0);
    lv_obj_set_style_line_rounded(band, true, 0);
    lv_obj_clear_flag(band, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(band, LV_ALIGN_CENTER, 0, 0);

    for(int i = -1; i <= 1; i += 2) {
        lv_obj_t *bristle = lv_line_create(parent);
        lv_line_set_points(bristle, k_bristle_pts, 2);
        lv_obj_set_style_line_width(bristle, 2, 0);
        lv_obj_set_style_line_color(bristle, color, 0);
        lv_obj_set_style_line_rounded(bristle, true, 0);
        lv_obj_clear_flag(bristle, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(bristle, LV_ALIGN_CENTER, i * 3, 6);
    }
}

static void ui_build_temperature_icon(lv_obj_t *parent)
{
    const lv_color_t color = lv_color_hex(0xFFFFFF);
    lv_obj_t *tube;
    lv_obj_t *mercury;
    lv_obj_t *bulb;

    /* Native-pixel geometry keeps the symbol visually aligned with the
     * unscaled 16 px bitmap text. */
    (lv_obj_set_size)(parent, 10, 18);
    lv_obj_set_style_bg_opa(parent, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(parent, 0, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_CLICKABLE);

    tube = lv_obj_create(parent);
    ui_reset_base_obj(tube);
    (lv_obj_set_size)(tube, 4, 12);
    (lv_obj_set_pos)(tube, 3, 0);
    lv_obj_set_style_radius(tube, 2, 0);
    lv_obj_set_style_border_width(tube, 1, 0);
    lv_obj_set_style_border_color(tube, color, 0);
    lv_obj_set_style_bg_opa(tube, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(tube, LV_OBJ_FLAG_CLICKABLE);

    mercury = lv_obj_create(parent);
    ui_reset_base_obj(mercury);
    (lv_obj_set_size)(mercury, 2, 9);
    (lv_obj_set_pos)(mercury, 4, 5);
    lv_obj_set_style_radius(mercury, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(mercury, color, 0);
    lv_obj_set_style_bg_opa(mercury, LV_OPA_COVER, 0);
    lv_obj_clear_flag(mercury, LV_OBJ_FLAG_CLICKABLE);

    bulb = lv_obj_create(parent);
    ui_reset_base_obj(bulb);
    (lv_obj_set_size)(bulb, 8, 8);
    (lv_obj_set_pos)(bulb, 1, 10);
    lv_obj_set_style_radius(bulb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(bulb, color, 0);
    lv_obj_set_style_bg_opa(bulb, LV_OPA_COVER, 0);
    lv_obj_clear_flag(bulb, LV_OBJ_FLAG_CLICKABLE);
}

static void ui_draw_flavor_triangle_cb(lv_event_t *e)
{
    const bool points_right = (bool)(uintptr_t)lv_event_get_user_data(e);
    lv_obj_t *obj = lv_event_get_target(e);
    lv_area_t coords;
    lv_draw_triangle_dsc_t triangle;

    lv_obj_get_coords(obj, &coords);
    lv_draw_triangle_dsc_init(&triangle);
    triangle.color = lv_color_hex(0xFFFFFF);
    triangle.opa = LV_OPA_COVER;

    /* 15 px 宽、36 px 高的细长实心三角形，左右两侧严格镜像。 */
    triangle.p[0].x = coords.x1 + (points_right ? 16 : 1);
    triangle.p[0].y = coords.y1 + 18;
    triangle.p[1].x = coords.x1 + (points_right ? 1 : 16);
    triangle.p[1].y = coords.y1 + 1;
    triangle.p[2].x = coords.x1 + (points_right ? 1 : 16);
    triangle.p[2].y = coords.y1 + 35;

    lv_draw_triangle(lv_event_get_layer(e), &triangle);
}

static void ui_build_chevron_icon(lv_obj_t *parent, bool points_right)
{
    lv_obj_t *triangle = lv_obj_create(parent);

    lv_obj_remove_style_all(triangle);
    lv_obj_clear_flag(triangle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(triangle, 18, 37);
    lv_obj_align(triangle, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(triangle, ui_draw_flavor_triangle_cb, LV_EVENT_DRAW_MAIN,
                        (void *)(uintptr_t)points_right);
}

static void ui_build_doneness_selector(lv_obj_t *screen)
{
    lv_obj_t *img;
    lv_obj_t *viewport;
    lv_obj_t *overlay;
    lv_obj_t *page;

    /* 决定初始选中项：优先跟随已存熟度，落不在三档内则默认全熟。 */
    {
        const ui_data_state_t *st = ui_data_get_state(s_ui.data);
        int mapped = (st != NULL) ? ui_egg_tile_for_doneness(st->doneness) : -1;
        s_ui.egg_tile_idx = (mapped >= 0) ? mapped : 0;
    }

    viewport = lv_obj_create(screen);
    ui_prepare_transparent_obj(viewport);
    lv_obj_set_size(viewport, 480, 320);
    lv_obj_set_pos(viewport, 0, 0);
    lv_obj_clear_flag(viewport, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(viewport, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_move_background(viewport);
    s_ui.bg_viewport = viewport;

    overlay = lv_obj_create(screen);
    ui_prepare_transparent_obj(overlay);
    lv_obj_set_size(overlay, 480, 320);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    s_ui.selector_overlay = overlay;

    page = lv_obj_create(viewport);
    ui_prepare_transparent_obj(page);
    lv_obj_set_size(page, 480, 320);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_CLICKABLE);
    s_ui.bg_page = page;

    page = lv_obj_create(viewport);
    ui_prepare_transparent_obj(page);
    lv_obj_set_size(page, 480, 320);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    s_ui.bg_page_incoming = page;

    /* 整屏煎蛋背景图（置于最底层，其余控件浮于其上）。 */
    img = lv_image_create(s_ui.bg_page);
    lv_image_set_src(img, ui_egg_tile_img(s_ui.egg_tile_idx));
    lv_obj_set_pos(img, 0, 0);
    lv_obj_move_background(img);
    s_ui.bg_image = img;
    s_ui.bg_image_idx = s_ui.egg_tile_idx;

    /* 拖拽切换时跟手进入的邻档整屏图，默认隐藏、贴在最底层。 */
    img = lv_image_create(s_ui.bg_page_incoming);
    lv_image_set_src(img, ui_egg_tile_img(s_ui.egg_tile_idx));
    lv_obj_set_pos(img, 0, 0);
    lv_obj_move_background(img);
    s_ui.bg_image_incoming = img;
    s_ui.egg_drag_target_idx = -1;

    /* 30% 暗化已在 RGB888 资源中预合成，避免 RGB565 图像在运行时再次
     * 混合、量化而放大白色盘子和蛋白上的彩色色阶。 */

    /* 信息卡：中文名称 + 英文副标题 + 中文描述，底部居中。 */
    s_ui.name_label = lv_label_create(s_ui.bg_page);
    lv_label_set_text(s_ui.name_label, k_egg_tiles[s_ui.egg_tile_idx].name);
    lv_obj_set_style_text_font(s_ui.name_label, &ui_egg_name_38, 0);
    lv_obj_set_style_text_color(s_ui.name_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_letter_space(s_ui.name_label, 4, 0);
    lv_obj_set_style_text_align(s_ui.name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ui.name_label, LV_ALIGN_BOTTOM_MID, 0, -150);

    s_ui.en_label = lv_label_create(s_ui.bg_page);
    lv_label_set_text(s_ui.en_label, k_egg_tiles[s_ui.egg_tile_idx].en);
    lv_obj_set_style_text_font(s_ui.en_label, &ui_egg_desc_19, 0);
    lv_obj_set_style_text_color(s_ui.en_label, lv_color_hex(UI_C_ACCENT), 0);
    lv_obj_set_style_text_opa(s_ui.en_label, LV_OPA_90, 0);
    lv_obj_set_style_text_letter_space(s_ui.en_label, 4, 0);
    lv_obj_set_style_text_align(s_ui.en_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ui.en_label, LV_ALIGN_BOTTOM_MID, 0, -122);

    s_ui.desc_label = lv_label_create(s_ui.bg_page);
    lv_label_set_text(s_ui.desc_label, k_egg_tiles[s_ui.egg_tile_idx].desc);
    lv_obj_set_style_text_font(s_ui.desc_label, &ui_egg_desc_19, 0);
    lv_obj_set_style_text_color(s_ui.desc_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(s_ui.desc_label, LV_OPA_80, 0);
    lv_obj_set_style_text_letter_space(s_ui.desc_label, 1, 0);
    lv_obj_set_style_text_align(s_ui.desc_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ui.desc_label, LV_ALIGN_BOTTOM_MID, 0, -96);

    s_ui.name_label_incoming = lv_label_create(s_ui.bg_page_incoming);
    lv_label_set_text(s_ui.name_label_incoming, k_egg_tiles[s_ui.egg_tile_idx].name);
    lv_obj_set_style_text_font(s_ui.name_label_incoming, &ui_egg_name_38, 0);
    lv_obj_set_style_text_color(s_ui.name_label_incoming, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_letter_space(s_ui.name_label_incoming, 4, 0);
    lv_obj_set_style_text_align(s_ui.name_label_incoming, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ui.name_label_incoming, LV_ALIGN_BOTTOM_MID, 0, -150);

    s_ui.en_label_incoming = lv_label_create(s_ui.bg_page_incoming);
    lv_label_set_text(s_ui.en_label_incoming, k_egg_tiles[s_ui.egg_tile_idx].en);
    lv_obj_set_style_text_font(s_ui.en_label_incoming, &ui_egg_desc_19, 0);
    lv_obj_set_style_text_color(s_ui.en_label_incoming, lv_color_hex(UI_C_ACCENT), 0);
    lv_obj_set_style_text_opa(s_ui.en_label_incoming, LV_OPA_90, 0);
    lv_obj_set_style_text_letter_space(s_ui.en_label_incoming, 4, 0);
    lv_obj_set_style_text_align(s_ui.en_label_incoming, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ui.en_label_incoming, LV_ALIGN_BOTTOM_MID, 0, -122);

    s_ui.desc_label_incoming = lv_label_create(s_ui.bg_page_incoming);
    lv_label_set_text(s_ui.desc_label_incoming, k_egg_tiles[s_ui.egg_tile_idx].desc);
    lv_obj_set_style_text_font(s_ui.desc_label_incoming, &ui_egg_desc_19, 0);
    lv_obj_set_style_text_color(s_ui.desc_label_incoming, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(s_ui.desc_label_incoming, LV_OPA_80, 0);
    lv_obj_set_style_text_letter_space(s_ui.desc_label_incoming, 1, 0);
    lv_obj_set_style_text_align(s_ui.desc_label_incoming, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ui.desc_label_incoming, LV_ALIGN_BOTTOM_MID, 0, -96);

    /* 左箭头按钮（上一档）。 */
    s_ui.don_left_btn = lv_button_create(overlay);
    ui_reset_base_obj(s_ui.don_left_btn);
    lv_obj_set_size(s_ui.don_left_btn, 44, 64);
    lv_obj_set_style_bg_opa(s_ui.don_left_btn, LV_OPA_TRANSP, 0);
    lv_obj_align(s_ui.don_left_btn, LV_ALIGN_LEFT_MID, 2, -8);
    lv_obj_add_event_cb(s_ui.don_left_btn, ui_doneness_prev_cb, LV_EVENT_CLICKED, NULL);
    ui_build_chevron_icon(s_ui.don_left_btn, false);

    /* 右箭头按钮（下一档）。 */
    s_ui.don_right_btn = lv_button_create(overlay);
    ui_reset_base_obj(s_ui.don_right_btn);
    lv_obj_set_size(s_ui.don_right_btn, 44, 64);
    lv_obj_set_style_bg_opa(s_ui.don_right_btn, LV_OPA_TRANSP, 0);
    lv_obj_align(s_ui.don_right_btn, LV_ALIGN_RIGHT_MID, -2, -8);
    lv_obj_add_event_cb(s_ui.don_right_btn, ui_doneness_next_cb, LV_EVENT_CLICKED, NULL);
    ui_build_chevron_icon(s_ui.don_right_btn, true);
}

static const char *ui_cook_doneness_name(ui_doneness_t don)
{
    switch(don) {
    case UI_DON_WELL:
        return u8"全熟";
    case UI_DON_JAMMY:
        return u8"糖心";
    case UI_DON_SOFT_YOLK:
        return u8"软嫩";
    case UI_DON_SUNNY:
        return u8"太阳蛋";
    default:
        return u8"全熟";
    }
}

/* 刷新“制作中”浮层：标题显示熟度，副标题显示当前阶段，核心信息显示
 * 主控按整枚向上取整后的剩余制作数量。 */
static void ui_update_cook_overlay(const ui_data_state_t *state)
{
    char title[64];
    char remaining_text[32];

    if(s_ui.cook_overlay == NULL) {
        return;
    }

    if(state == NULL || !ui_data_state_is_running(state)) {
        if(!lv_obj_has_flag(s_ui.cook_overlay, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(s_ui.cook_overlay, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    if(lv_obj_has_flag(s_ui.cook_overlay, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(s_ui.cook_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_ui.cook_overlay);
        ui_apply_debug_button_visibility();
    }

    /* 标题：正在制作 · <熟度>。 */
    snprintf(title, sizeof(title), u8"正在制作 \xC2\xB7 %s",
             ui_cook_doneness_name(state->doneness));
    if(s_ui.cook_title_label != NULL) {
        ui_label_set_text_if_changed(s_ui.cook_title_label, title);
    }

    /* 副标题：当前阶段文案（复用状态映射）。 */
    if(s_ui.cook_sub_label != NULL) {
        ui_label_set_text_if_changed(s_ui.cook_sub_label, ui_get_status_text(state));
    }

    if(s_ui.cook_remaining_label != NULL) {
        if(state->doneness == UI_DON_SUNNY) {
            snprintf(remaining_text, sizeof(remaining_text), u8"正在煎制 %u 枚",
                     (unsigned)state->needMake);
        } else {
            snprintf(remaining_text, sizeof(remaining_text), u8"剩余 %u 枚",
                     (unsigned)state->needMake);
        }
        ui_label_set_text_if_changed(s_ui.cook_remaining_label, remaining_text);
    }

    if(s_ui.cook_cancel_rest_btn != NULL) {
        if(state->doneness == UI_DON_SUNNY) {
            lv_obj_add_flag(s_ui.cook_cancel_rest_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_ui.cook_cancel_rest_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void ui_refresh(void)
{
    const ui_data_state_t *state = ui_get_state();

    if(s_ui.screen == NULL || state == NULL) {
        return;
    }

    if(ui_alert_should_show(state)) {
        s_ui.active = false;
        ui_stop_countdown_timer();
        ui_stop_blink_timer();
        ui_stop_conn_check_timer();
        ui_alert_create(s_ui.data);
        return;
    }

    if(ui_data_state_is_idle(state)) {
        ui_stop_countdown_timer();
        ui_stop_blink_timer();
        lv_obj_add_flag(s_ui.progress_track, LV_OBJ_FLAG_HIDDEN);
        ui_show_digit(state->needMake);
    } else if(ui_data_state_is_running(state)) {
        ui_stop_blink_timer();
        if(s_ui.countdown_timer == NULL) {
            s_ui.countdown_timer = lv_timer_create(ui_countdown_timer_cb, 1000, NULL);
        }
        /* 制作中改用整屏进度浮层，隐藏旧的滑动灯条。 */
        lv_obj_add_flag(s_ui.progress_track, LV_OBJ_FLAG_HIDDEN);
        ui_show_digit(state->needMake);
    } else if(ui_data_state_is_finished(state)) {
        ui_stop_countdown_timer();
        lv_obj_add_flag(s_ui.progress_track, LV_OBJ_FLAG_HIDDEN);
        ui_start_blink_sequence();
        if((s_ui.blink_phase & 0x01U) != 0U) {
            ui_clear_digit();
        } else {
            ui_show_digit(state->needMake);
        }
    }

    ui_update_cook_overlay(state);
    ui_update_controls_visuals(state);
    ui_refresh_status_display(state);
    ui_refresh_pot_temp_display();
}

/* “制作中”半悬浮浮层：半透明遮罩（背后主界面可见）+ 底部深色
 * 圆角卡片，卡片内含熟度标题、阶段副标题、流水动画和剩余枚数。
 * 遮罩可点击以吞掉触摸，避免误触下层按钮。 */
static void ui_build_cook_overlay(lv_obj_t *screen)
{
    lv_obj_t *overlay;
    lv_obj_t *panel;
    lv_obj_t *title;
    lv_obj_t *sub;
    lv_obj_t *anim_row;
    lv_obj_t *remaining;
    lv_obj_t *row;
    lv_obj_t *estop;
    lv_obj_t *cancel_rest;
    lv_obj_t *lbl;

    /* 半透明遮罩：全屏，背后主界面透出，制作中不可点空白关闭。 */
    overlay = lv_obj_create(screen);
    ui_reset_base_obj(overlay);
    lv_obj_set_size(overlay, 480, 320);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, UI_OVERLAY_BG_OPA, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    s_ui.cook_overlay = overlay;

    /* 底部半悬浮卡片：与数量/预约弹窗一致的深色圆角面板。 */
    panel = lv_obj_create(overlay);
    ui_reset_base_obj(panel);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(panel, 480, UI_OVERLAY_PANEL_H);
    lv_obj_align(panel, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 24, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(UI_C_ACCENT), 0);
    lv_obj_set_style_border_opa(panel, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);

    /* 标题：正在制作 · <熟度>。 */
    title = lv_label_create(panel);
    lv_label_set_text(title, u8"正在制作");
    lv_obj_set_style_text_font(title, &ui_cook_font_20, 0);
    lv_obj_set_style_text_letter_space(title, 2, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_C_ACCENT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, UI_OVERLAY_TITLE_TOP);
    s_ui.cook_title_label = title;

    /* 副标题：当前阶段文案，复用状态字体。 */
    sub = lv_label_create(panel);
    lv_label_set_text(sub, "--");
    lv_obj_set_style_text_font(sub, &ui_status_font_12, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(0x94A3B8), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 56);
    s_ui.cook_sub_label = sub;

    /* 流水灯：所有点保留低亮底光，亮度波峰平滑地从左向右循环，
     * 表达设备持续工作，同时不暗示不可靠的时间百分比。 */
    anim_row = lv_obj_create(panel);
    ui_reset_base_obj(anim_row);
    lv_obj_clear_flag(anim_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(anim_row, 66, 10);
    lv_obj_set_style_bg_opa(anim_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(anim_row, 0, 0);
    lv_obj_set_style_pad_column(anim_row, 7, 0);
    lv_obj_set_flex_flow(anim_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(anim_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_align(anim_row, LV_ALIGN_TOP_MID, 0, 84);

    for(uint32_t i = 0; i < UI_COOK_ANIM_DOT_COUNT; i++) {
        lv_obj_t *dot = lv_obj_create(anim_row);

        ui_reset_base_obj(dot);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(dot, 6, 6);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(UI_C_ACCENT), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_opa(dot, LV_OPA_30, 0);
        s_ui.cook_anim_dots[i] = dot;
    }

    {
        lv_anim_t anim;

        ui_cook_anim_wave_cb(anim_row, 0);
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, anim_row);
        lv_anim_set_exec_cb(&anim, ui_cook_anim_wave_cb);
        lv_anim_set_values(&anim, 0, (int32_t)UI_COOK_ANIM_CYCLE_UNITS - 1);
        lv_anim_set_duration(&anim, UI_COOK_ANIM_CYCLE_MS);
        lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&anim, lv_anim_path_linear);
        lv_anim_start(&anim);
    }

    /* 主信息：剩余需要完成的整枚数量。 */
    remaining = lv_label_create(panel);
    lv_label_set_text(remaining, u8"剩余 -- 枚");
    lv_obj_set_style_text_font(remaining, &ui_cook_font_20, 0);
    lv_obj_set_style_text_color(remaining, lv_color_hex(UI_C_ACCENT), 0);
    lv_obj_set_style_text_letter_space(remaining, 2, 0);
    lv_obj_align(remaining, LV_ALIGN_TOP_MID, 0, 106);
    s_ui.cook_remaining_label = remaining;

    /* 操作行：紧急停止（红，醒目）/ 取消后续（暗底描边）。 */
    row = lv_obj_create(panel);
    ui_reset_base_obj(row);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_size(row, 400, UI_OVERLAY_ACTION_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 16, 0);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -UI_OVERLAY_ACTION_BOTTOM);
    s_ui.cook_btn_row = row;

    estop = lv_button_create(row);
    ui_reset_base_obj(estop);
    lv_obj_set_height(estop, UI_OVERLAY_ACTION_H);
    lv_obj_set_flex_grow(estop, 1);
    lv_obj_set_style_radius(estop, 23, 0);
    lv_obj_set_style_bg_opa(estop, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(estop, lv_color_hex(0xDC2626), 0);
    lv_obj_set_style_bg_grad_color(estop, lv_color_hex(0x991B1B), 0);
    lv_obj_set_style_bg_grad_dir(estop, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_width(estop, 12, 0);
    lv_obj_set_style_shadow_color(estop, lv_color_hex(0xDC2626), 0);
    lv_obj_set_style_shadow_opa(estop, LV_OPA_60, 0);
    lv_obj_add_event_cb(estop, ui_cook_estop_cb, LV_EVENT_CLICKED, NULL);
    s_ui.cook_estop_btn = estop;
    lbl = lv_label_create(estop);
    lv_label_set_text(lbl, ui_t(UI_STR_ESTOP));
    lv_obj_set_style_text_font(lbl, &ui_cook_font_20, 0);
    lv_obj_set_style_text_letter_space(lbl, 2, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl);

    cancel_rest = lv_button_create(row);
    ui_reset_base_obj(cancel_rest);
    lv_obj_set_height(cancel_rest, UI_OVERLAY_ACTION_H);
    lv_obj_set_flex_grow(cancel_rest, 1);
    lv_obj_set_style_radius(cancel_rest, 23, 0);
    lv_obj_set_style_bg_opa(cancel_rest, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cancel_rest, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_bg_grad_color(cancel_rest, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_grad_dir(cancel_rest, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(cancel_rest, 1, 0);
    lv_obj_set_style_border_color(cancel_rest, lv_color_hex(UI_C_ACCENT), 0);
    lv_obj_set_style_border_opa(cancel_rest, LV_OPA_50, 0);
    lv_obj_add_event_cb(cancel_rest, ui_cook_cancel_rest_cb, LV_EVENT_CLICKED, NULL);
    s_ui.cook_cancel_rest_btn = cancel_rest;
    lbl = lv_label_create(cancel_rest);
    lv_label_set_text(lbl, ui_t(UI_STR_CANCEL_REST));
    lv_obj_set_style_text_font(lbl, &ui_cook_font_20, 0);
    lv_obj_set_style_text_letter_space(lbl, 2, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xCBD5E1), 0);
    lv_obj_center(lbl);
}

/* 通用底部弹层容器：整屏遮罩 + 底部深色圆角面板。 */
static lv_obj_t *ui_build_panel_container(lv_obj_t *screen, lv_obj_t **out_mask,
                                          lv_event_cb_t mask_cb)
{
    lv_obj_t *mask;
    lv_obj_t *panel;

    mask = lv_obj_create(screen);
    ui_reset_base_obj(mask);
    lv_obj_set_size(mask, 480, 320);
    lv_obj_set_pos(mask, 0, 0);
    lv_obj_set_style_bg_color(mask, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(mask, UI_OVERLAY_BG_OPA, 0);
    lv_obj_set_style_border_width(mask, 0, 0);
    lv_obj_add_flag(mask, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mask, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(mask, mask_cb, LV_EVENT_CLICKED, NULL);
    *out_mask = mask;

    panel = lv_obj_create(screen);
    ui_reset_base_obj(panel);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(panel, 480, UI_OVERLAY_PANEL_H);
    lv_obj_align(panel, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 24, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(UI_C_ACCENT), 0);
    lv_obj_set_style_border_opa(panel, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    return panel;
}

static void ui_build_qty_panel(lv_obj_t *screen)
{
    lv_obj_t *panel;
    lv_obj_t *nums;
    lv_obj_t *section;
    lv_obj_t *minus;
    lv_obj_t *plus;
    lv_obj_t *disp;
    lv_obj_t *acts;
    lv_obj_t *cook;
    lv_obj_t *reserve;
    lv_obj_t *reserve_cancel;
    lv_obj_t *lbl;
    int i;

    panel = ui_build_panel_container(screen, &s_ui.qty_mask, ui_qty_cancel_cb);
    s_ui.qty_panel = panel;

    /* 标题、内容和操作区使用固定纵向基准，预约控件显示时不推移标题和按钮。 */
    lv_obj_set_height(panel, UI_QTY_PANEL_H_RESERVE);
    lv_obj_set_style_pad_all(panel, 0, 0);

    /* 标题（制作/预约动态切换）。 */
    s_ui.qty_title_label = lv_label_create(panel);
    lv_label_set_text(s_ui.qty_title_label, u8"选择数量");
    lv_obj_set_style_text_font(s_ui.qty_title_label, &ui_panel_20, 0);
    lv_obj_set_style_text_letter_space(s_ui.qty_title_label, 3, 0);
    lv_obj_set_style_text_color(s_ui.qty_title_label, lv_color_hex(UI_C_ACCENT), 0);
    lv_obj_align(s_ui.qty_title_label, LV_ALIGN_TOP_MID, 0, UI_OVERLAY_TITLE_TOP);

    /* 1~6 数字圆形按钮：均匀排布，保留充足触控间距。 */
    nums = lv_obj_create(panel);
    ui_reset_base_obj(nums);
    lv_obj_clear_flag(nums, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(nums, LV_OPA_TRANSP, 0);
    lv_obj_set_size(nums, 400, 40);
    lv_obj_set_flex_flow(nums, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(nums, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_align(nums, LV_ALIGN_TOP_MID, 0, UI_QTY_ROW_Y_MAKE);
    s_ui.qty_num_row = nums;
    for(i = 0; i < UI_QTY_MAX; i++) {
        char t[4];
        lv_obj_t *b = lv_button_create(nums);
        ui_reset_base_obj(b);
        lv_obj_set_size(b, 40, 40);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        snprintf(t, sizeof(t), "%d", i + 1);
        lbl = lv_label_create(b);
        lv_label_set_text(lbl, t);
        lv_obj_set_style_text_font(lbl, &ui_panel_20, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(b, ui_qty_num_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(i + 1));
        s_ui.qty_num_btns[i] = b;
    }

    /* 预约步进器：− / 大号时间 + AM/PM / +（仅预约态显示）。 */
    section = lv_obj_create(panel);
    ui_reset_base_obj(section);
    lv_obj_clear_flag(section, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(section, LV_OPA_TRANSP, 0);
    lv_obj_set_size(section, UI_RESERVE_SECTION_W, UI_RESERVE_SECTION_H);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(section, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(section, UI_RESERVE_CONTROL_GAP, 0);
    lv_obj_align(section, LV_ALIGN_TOP_MID, 0, UI_RESERVE_SECTION_Y);
    lv_obj_add_flag(section, LV_OBJ_FLAG_HIDDEN); /* 默认制作态隐藏 */
    s_ui.qty_reserve_section = section;

    minus = lv_button_create(section);
    ui_reset_base_obj(minus);
    lv_obj_set_size(minus, UI_RESERVE_ADJUST_BTN_W, UI_RESERVE_ADJUST_BTN_H);
    lv_obj_set_style_radius(minus, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(minus, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(minus, 2, 0);
    lv_obj_set_style_border_color(minus, lv_color_hex(UI_C_ACCENT), 0);
    lv_obj_set_style_border_opa(minus, LV_OPA_60, 0);
    lv_obj_add_event_cb(minus, ui_reserve_minus_cb, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(minus);
    lv_label_set_text(lbl, "-");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(UI_C_ACCENT), 0);
    lv_obj_center(lbl);

    disp = lv_obj_create(section);
    ui_reset_base_obj(disp);
    lv_obj_clear_flag(disp, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(disp, LV_OPA_TRANSP, 0);
    lv_obj_set_size(disp, UI_RESERVE_TIME_DISPLAY_W, UI_RESERVE_TIME_DISPLAY_H);
    lv_obj_set_flex_flow(disp, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(disp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(disp, UI_RESERVE_TIME_GAP, 0);

    s_ui.reserve_meridiem_label = lv_label_create(disp);
    lv_label_set_text(s_ui.reserve_meridiem_label, "AM");
    lv_obj_set_style_text_font(s_ui.reserve_meridiem_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_letter_space(s_ui.reserve_meridiem_label, 2, 0);
    lv_obj_set_style_text_color(s_ui.reserve_meridiem_label, lv_color_hex(0x94A3B8), 0);

    s_ui.reserve_time_label = lv_label_create(disp);
    lv_label_set_text(s_ui.reserve_time_label, "08:00");
    lv_obj_set_style_text_font(s_ui.reserve_time_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_letter_space(s_ui.reserve_time_label, 2, 0);
    lv_obj_set_style_text_color(s_ui.reserve_time_label, lv_color_hex(0xFFFFFF), 0);

    plus = lv_button_create(section);
    ui_reset_base_obj(plus);
    lv_obj_set_size(plus, UI_RESERVE_ADJUST_BTN_W, UI_RESERVE_ADJUST_BTN_H);
    lv_obj_set_style_radius(plus, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(plus, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(plus, 2, 0);
    lv_obj_set_style_border_color(plus, lv_color_hex(UI_C_ACCENT), 0);
    lv_obj_set_style_border_opa(plus, LV_OPA_60, 0);
    lv_obj_add_event_cb(plus, ui_reserve_plus_cb, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(plus);
    lv_label_set_text(lbl, "+");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(UI_C_ACCENT), 0);
    lv_obj_center(lbl);

    /* 操作行：取消 + 开始（制作态）/ 取消 + 预约（预约态）。 */
    acts = lv_obj_create(panel);
    ui_reset_base_obj(acts);
    lv_obj_clear_flag(acts, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(acts, LV_OPA_TRANSP, 0);
    lv_obj_set_size(acts, 400, UI_OVERLAY_ACTION_H);
    lv_obj_set_flex_flow(acts, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(acts, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(acts, 16, 0);
    lv_obj_align(acts, LV_ALIGN_BOTTOM_MID, 0, -UI_OVERLAY_ACTION_BOTTOM);

    cook = lv_button_create(acts);
    ui_reset_base_obj(cook);
    lv_obj_set_height(cook, UI_OVERLAY_ACTION_H);
    lv_obj_set_flex_grow(cook, 1);
    lv_obj_set_style_radius(cook, 23, 0);
    lv_obj_set_style_bg_opa(cook, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cook, lv_color_hex(UI_C_PRIMARY_TOP), 0);
    lv_obj_set_style_bg_grad_color(cook, lv_color_hex(UI_C_PRIMARY_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(cook, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_width(cook, 12, 0);
    lv_obj_set_style_shadow_color(cook, lv_color_hex(UI_C_PRIMARY_BOTTOM), 0);
    lv_obj_set_style_shadow_opa(cook, LV_OPA_70, 0);
    lv_obj_add_event_cb(cook, ui_qty_cook_cb, LV_EVENT_CLICKED, NULL);
    s_ui.qty_cook_btn = cook;
    lbl = lv_label_create(cook);
    lv_label_set_text(lbl, ui_t(UI_STR_START));
    lv_obj_set_style_text_font(lbl, &ui_egg_btn_20, 0);
    lv_obj_set_style_text_letter_space(lbl, 2, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x422006), 0);
    lv_obj_center(lbl);

    /* 操作行左侧的取消按钮：制作和预约共用。 */
    reserve_cancel = lv_button_create(acts);
    ui_reset_base_obj(reserve_cancel);
    lv_obj_set_height(reserve_cancel, UI_OVERLAY_ACTION_H);
    lv_obj_set_flex_grow(reserve_cancel, 1);
    lv_obj_set_style_radius(reserve_cancel, 23, 0);
    lv_obj_set_style_bg_opa(reserve_cancel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(reserve_cancel, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_bg_grad_color(reserve_cancel, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_grad_dir(reserve_cancel, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(reserve_cancel, 1, 0);
    lv_obj_set_style_border_color(reserve_cancel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(reserve_cancel, LV_OPA_20, 0);
    lv_obj_add_flag(reserve_cancel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(reserve_cancel, ui_qty_cancel_cb, LV_EVENT_CLICKED, NULL);
    s_ui.qty_reserve_cancel_btn = reserve_cancel;
    lbl = lv_label_create(reserve_cancel);
    s_ui.qty_reserve_cancel_label = lbl;
    lv_label_set_text(lbl, u8"取消");
    lv_obj_set_style_text_font(lbl, &ui_panel_20, 0);
    lv_obj_set_style_text_letter_space(lbl, 2, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xCBD5E1), 0);
    lv_obj_center(lbl);
    lv_obj_move_to_index(reserve_cancel, 0);

    reserve = lv_button_create(acts);
    ui_reset_base_obj(reserve);
    lv_obj_set_height(reserve, UI_OVERLAY_ACTION_H);
    lv_obj_set_flex_grow(reserve, 1);
    lv_obj_set_style_radius(reserve, 23, 0);
    lv_obj_set_style_bg_opa(reserve, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(reserve, lv_color_hex(UI_C_PRIMARY_TOP), 0);
    lv_obj_set_style_bg_grad_color(reserve, lv_color_hex(UI_C_PRIMARY_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(reserve, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_width(reserve, 12, 0);
    lv_obj_set_style_shadow_color(reserve, lv_color_hex(UI_C_PRIMARY_BOTTOM), 0);
    lv_obj_set_style_shadow_opa(reserve, LV_OPA_70, 0);
    lv_obj_add_flag(reserve, LV_OBJ_FLAG_HIDDEN); /* 默认制作态隐藏 */
    lv_obj_add_event_cb(reserve, ui_qty_reserve_cb, LV_EVENT_CLICKED, NULL);
    s_ui.qty_reserve_btn = reserve;
    lbl = lv_label_create(reserve);
    lv_label_set_text(lbl, u8"预约");
    lv_obj_set_style_text_font(lbl, &ui_panel_20, 0);
    lv_obj_set_style_text_letter_space(lbl, 2, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x422006), 0);
    lv_obj_center(lbl);

    ui_refresh_qty_selection();
}

static lv_obj_t *ui_time_create_adjust_button(lv_obj_t *parent, const char *text, int delta)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;

    ui_reset_base_obj(button);
    lv_obj_set_size(button, 32, 32);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(UI_C_YELLOW), 0);
    lv_obj_set_style_border_opa(button, LV_OPA_50, 0);
    lv_obj_add_event_cb(button, ui_time_adjust_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)delta);

    label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_YELLOW), 0);
    lv_obj_center(label);
    return button;
}

static void ui_build_time_panel(lv_obj_t *screen)
{
    lv_obj_t *panel;
    lv_obj_t *title;
    lv_obj_t *picker;
    lv_obj_t *colon;
    lv_obj_t *actions;
    lv_obj_t *cancel;
    lv_obj_t *confirm;
    lv_obj_t *label;

    panel = ui_build_panel_container(screen, &s_ui.time_mask, ui_time_cancel_cb);
    s_ui.time_panel = panel;

    title = lv_label_create(panel);
    lv_label_set_text(title, u8"时间设置");
    lv_obj_set_style_text_font(title, &ui_panel_20, 0);
    lv_obj_set_style_text_letter_space(title, 3, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_C_YELLOW), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, UI_OVERLAY_TITLE_TOP);

    picker = lv_obj_create(panel);
    ui_reset_base_obj(picker);
    lv_obj_clear_flag(picker, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(picker, 380, 54);
    lv_obj_set_style_bg_opa(picker, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(picker, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(picker, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(picker, 6, 0);
    lv_obj_align(picker, LV_ALIGN_TOP_MID, 0, 55);

    (void)ui_time_create_adjust_button(picker, "-", -60);
    s_ui.time_hour_label = lv_label_create(picker);
    lv_label_set_text(s_ui.time_hour_label, "12");
    lv_obj_set_width(s_ui.time_hour_label, 40);
    lv_obj_set_style_text_font(s_ui.time_hour_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_ui.time_hour_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(s_ui.time_hour_label, LV_TEXT_ALIGN_CENTER, 0);
    (void)ui_time_create_adjust_button(picker, "+", 60);

    colon = lv_label_create(picker);
    lv_label_set_text(colon, ":");
    lv_obj_set_style_text_font(colon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(colon, lv_color_hex(UI_C_TEXT_MED), 0);

    (void)ui_time_create_adjust_button(picker, "-", -1);
    s_ui.time_minute_label = lv_label_create(picker);
    lv_label_set_text(s_ui.time_minute_label, "00");
    lv_obj_set_width(s_ui.time_minute_label, 40);
    lv_obj_set_style_text_font(s_ui.time_minute_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_ui.time_minute_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(s_ui.time_minute_label, LV_TEXT_ALIGN_CENTER, 0);
    (void)ui_time_create_adjust_button(picker, "+", 1);

    actions = lv_obj_create(panel);
    ui_reset_base_obj(actions);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(actions, 400, UI_OVERLAY_ACTION_H);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(actions, 16, 0);
    lv_obj_align(actions, LV_ALIGN_BOTTOM_MID, 0, -UI_OVERLAY_ACTION_BOTTOM);

    cancel = lv_button_create(actions);
    ui_reset_base_obj(cancel);
    lv_obj_set_height(cancel, UI_OVERLAY_ACTION_H);
    lv_obj_set_flex_grow(cancel, 1);
    lv_obj_set_style_radius(cancel, 21, 0);
    lv_obj_set_style_bg_opa(cancel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_width(cancel, 1, 0);
    lv_obj_set_style_border_color(cancel, lv_color_hex(UI_C_YELLOW), 0);
    lv_obj_set_style_border_opa(cancel, LV_OPA_50, 0);
    lv_obj_add_event_cb(cancel, ui_time_cancel_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(cancel);
    lv_label_set_text(label, ui_t(UI_STR_CANCEL));
    lv_obj_set_style_text_font(label, &ui_setting_font_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_center(label);

    confirm = lv_button_create(actions);
    ui_reset_base_obj(confirm);
    lv_obj_set_height(confirm, UI_OVERLAY_ACTION_H);
    lv_obj_set_flex_grow(confirm, 1);
    lv_obj_set_style_radius(confirm, 21, 0);
    lv_obj_set_style_bg_opa(confirm, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(confirm, lv_color_hex(UI_C_PRIMARY_TOP), 0);
    lv_obj_set_style_bg_grad_color(confirm, lv_color_hex(UI_C_PRIMARY_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(confirm, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_width(confirm, 10, 0);
    lv_obj_set_style_shadow_color(confirm, lv_color_hex(UI_C_PRIMARY_BOTTOM), 0);
    lv_obj_set_style_shadow_opa(confirm, LV_OPA_30, 0);
    lv_obj_add_event_cb(confirm, ui_time_confirm_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(confirm);
    lv_label_set_text(label, ui_t(UI_STR_CONFIRM));
    lv_obj_set_style_text_font(label, &ui_setting_font_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_BG), 0);
    lv_obj_center(label);
}

/*
 * 主界面元素地图：
 * - brand_label：顶部居中的品牌文字。
 * - B/A 锅遥测和 status_box：直接放在系统状态栏中间。
 * - debug_button：直属 screen，位于设置按钮下方。
 * - settings_button：左上角设置入口。
 * - debug_button：设置按钮右侧的 debug 入口，解锁后显示。
 * - right_group：熟度滚轮区域（数量选择已移至后续弹窗）。
 * - progress_track：运行中的进度灯条。
 * - start_btn：底部整宽的金色开始按钮。
 * - conn_indicator：状态栏时间左侧的主控 UART 在线状态点。
 */
void ui_home_create(ui_data_t *data)
{
    lv_obj_t *debug_button;
    lv_obj_t *brand_label;
    lv_obj_t *settings_button;
    lv_obj_t *screen;
    lv_obj_t *separator;
    lv_obj_t *button;
    lv_obj_t *label;
    const ui_data_state_t *state;

    if(data == NULL) {
        return;
    }

    state = ui_data_get_state(data);
    if(ui_alert_should_show(state)) {
        ui_alert_create(data);
        return;
    }

    ui_stop_countdown_timer();
    ui_stop_blink_timer();
    ui_stop_conn_check_timer();

    ui_egg_preload_images(); /* 整屏图预载到 PSRAM（仅首次，见函数内守卫） */

    lv_memzero(&s_ui, sizeof(s_ui));
    s_ui.active = true;
    s_ui.qty_panel_intent = -1;
    s_ui.reserve_hour = -1; /* 默认未预约（memzero 会置 0=零点，需显式复位） */
    s_ui.reserve_minute = 0;
    s_ui.reserve_qty = 1;
    s_ui.reserve_edit_hour = UI_RESERVE_HOUR_DEFAULT;
    s_ui.reserve_edit_minute = 0;
    s_ui.bg_image_idx = -1;
    s_ui.egg_tile_shown = -1; /* 初次呈现不播放切换动画 */
    s_ui.selector_controls_idx = -1;
    s_ui.selector_adjustable_shown = -1;
    s_ui.start_visual_state = UINT8_MAX;
    s_ui.wifi_connected_shown = -1;
    s_ui.data = data;
    screen = lv_screen_active();
    s_ui.screen = screen;

    ui_screen_reset(screen);

    /* 顶部口味标题不显示；保留透明触控区承接 debug 解锁手势。 */
    brand_label = lv_label_create(screen);
    lv_label_set_text(brand_label, "");
    lv_obj_set_size(brand_label, 120, 24);
    lv_obj_align(brand_label, LV_ALIGN_TOP_MID, 1, 30);
    /* 透明标题区域承接 debug 解锁手势（原取消按钮已移除）。 */
    lv_obj_add_flag(brand_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(brand_label, 12);
    lv_obj_add_event_cb(brand_label, ui_brand_event_cb, LV_EVENT_CLICKED, NULL);

    /* 顶部状态栏：左=设置，右=WiFi+时间，元素在栏内垂直居中对齐。 */
    lv_obj_t *status_bar = lv_obj_create(screen);
    lv_obj_remove_style_all(status_bar);
    lv_obj_set_size(status_bar, LV_PCT(100), 30);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(status_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_border_side(status_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_opa(status_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_CLICKABLE);
    s_ui.status_bar = status_bar;

    /* 左：设置入口。 */
    settings_button = lv_button_create(status_bar);
    ui_style_pressable_button(settings_button, 28, 28, LV_RADIUS_CIRCLE);
    lv_obj_align(settings_button, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_add_event_cb(settings_button, ui_settings_event_cb, LV_EVENT_CLICKED, NULL);
    ui_build_settings_icon(settings_button);

    /* DEBUG 遥测独立放在状态栏下方，避免与常驻状态信息挤在一起。 */
    s_ui.potb_temp_label = lv_label_create(screen);
    lv_label_set_text(s_ui.potb_temp_label, "B:-- D-- --");
    lv_label_set_long_mode(s_ui.potb_temp_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(s_ui.potb_temp_label, UI_POT_METRIC_LABEL_W);
    lv_obj_set_style_text_font(s_ui.potb_temp_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ui.potb_temp_label, lv_color_hex(UI_C_YELLOW), 0);
    lv_obj_set_style_text_align(s_ui.potb_temp_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ui.potb_temp_label, LV_ALIGN_TOP_LEFT,
                 UI_POTB_METRIC_X, UI_DEBUG_METRIC_Y);

    s_ui.pota_temp_label = lv_label_create(screen);
    lv_label_set_text(s_ui.pota_temp_label, "A:-- D-- --");
    lv_label_set_long_mode(s_ui.pota_temp_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(s_ui.pota_temp_label, UI_POT_METRIC_LABEL_W);
    lv_obj_set_style_text_font(s_ui.pota_temp_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ui.pota_temp_label, lv_color_hex(UI_C_YELLOW), 0);
    lv_obj_set_style_text_align(s_ui.pota_temp_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ui.pota_temp_label, LV_ALIGN_TOP_LEFT,
                 UI_POTA_METRIC_X, UI_DEBUG_METRIC_Y);

    /* 制作状态：与锅温遥测同行，位于状态栏下方。 */
    {
        lv_obj_t *status_value;

        s_ui.status_box = lv_obj_create(screen);
        ui_reset_base_obj(s_ui.status_box);
        lv_obj_clear_flag(s_ui.status_box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(s_ui.status_box, UI_STATUS_BOX_W, UI_STATUS_BOX_H);
        lv_obj_set_style_radius(s_ui.status_box, 9, 0);
        lv_obj_set_style_border_width(s_ui.status_box, 0, 0);
        lv_obj_set_style_border_color(s_ui.status_box, lv_color_hex(UI_C_LINE), 0);
        lv_obj_set_style_border_opa(s_ui.status_box, LV_OPA_70, 0);
        lv_obj_set_style_bg_opa(s_ui.status_box, LV_OPA_TRANSP, 0);
        lv_obj_align(s_ui.status_box, LV_ALIGN_TOP_LEFT,
                     UI_STATUS_BOX_X, UI_DEBUG_BUTTON_Y + 2);

        status_value = lv_label_create(s_ui.status_box);
        lv_label_set_text(status_value, "--");
        lv_label_set_long_mode(status_value, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(status_value, UI_STATUS_BOX_W);
        lv_obj_set_style_text_font(status_value, &ui_status_font_12, 0);
        lv_obj_set_style_text_color(status_value, lv_color_hex(UI_C_YELLOW), 0);
        lv_obj_set_style_text_align(status_value, LV_TEXT_ALIGN_CENTER, 0);
        /* 与 A/B 锅遥测共用同一顶部基线，避免缩放后的容器居中使文字偏下。 */
        lv_obj_align(status_value, LV_ALIGN_TOP_MID, 0, 1);
        s_ui.status_value_label = status_value;
    }

    /* 右：实时时间（最右）。 */
    s_ui.clock_label = lv_label_create(status_bar);
    lv_label_set_text(s_ui.clock_label, "--:--");
    lv_label_set_long_mode(s_ui.clock_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(s_ui.clock_label, UI_TOPBAR_CLOCK_W);
    lv_obj_set_style_text_font(s_ui.clock_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.clock_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(s_ui.clock_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_ui.clock_label, LV_ALIGN_RIGHT_MID, -UI_TOPBAR_RIGHT_MARGIN, 0);
    lv_obj_add_flag(s_ui.clock_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_ui.clock_label, UI_TOPBAR_CLOCK_EXT_CLICK);
    lv_obj_add_event_cb(s_ui.clock_label, ui_clock_event_cb, LV_EVENT_CLICKED, NULL);

    /* 主控 UART 心跳状态：在线绿色，离线红色。主界面只保留这一个通信状态。 */
    s_ui.conn_indicator = lv_obj_create(status_bar);
    ui_reset_base_obj(s_ui.conn_indicator);
    lv_obj_clear_flag(s_ui.conn_indicator, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_ui.conn_indicator, UI_TOPBAR_CONN_SIZE, UI_TOPBAR_CONN_SIZE);
    lv_obj_set_style_radius(s_ui.conn_indicator, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_ui.conn_indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_ui.conn_indicator, lv_color_hex(UI_COLOR_CONN_ERR), 0);
    lv_obj_set_style_border_width(s_ui.conn_indicator, 0, 0);
    lv_obj_align_to(s_ui.conn_indicator, s_ui.clock_label,
                    LV_ALIGN_OUT_LEFT_MID, -UI_TOPBAR_CONN_CLOCK_GAP, 0);

    /* DS18B20 仅提供温度，环境信息组不再显示湿度。 */
    s_ui.environment_group = lv_obj_create(status_bar);
    ui_prepare_transparent_obj(s_ui.environment_group);
    lv_obj_set_size(s_ui.environment_group, UI_TOPBAR_ENV_W, 30);
    lv_obj_set_style_pad_all(s_ui.environment_group, 0, 0);
    lv_obj_set_style_pad_column(s_ui.environment_group, 3, 0);
    lv_obj_set_flex_flow(s_ui.environment_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_ui.environment_group, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_ui.environment_group, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align_to(s_ui.environment_group, s_ui.conn_indicator,
                    LV_ALIGN_OUT_LEFT_MID, -UI_TOPBAR_ENV_CONN_GAP, 0);

    {
        lv_obj_t *temperature_icon = lv_obj_create(s_ui.environment_group);
        ui_reset_base_obj(temperature_icon);
        ui_build_temperature_icon(temperature_icon);

        s_ui.temperature_label = lv_label_create(s_ui.environment_group);
        lv_label_set_text(s_ui.temperature_label, "--°C");
        lv_label_set_long_mode(s_ui.temperature_label, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(s_ui.temperature_label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(s_ui.temperature_label, lv_color_hex(0xFFFFFF), 0);
    }

    /* 右：WiFi 状态图标（环境数据显示左侧，点击进入 WiFi 设置）。 */
    {
        lv_obj_t *wifi_btn = lv_button_create(status_bar);
        ui_reset_base_obj(wifi_btn);
        lv_obj_set_size(wifi_btn, 28, 28);
        lv_obj_set_style_bg_opa(wifi_btn, LV_OPA_TRANSP, 0);
        lv_obj_align_to(wifi_btn, s_ui.environment_group,
                        LV_ALIGN_OUT_LEFT_MID, -UI_TOPBAR_ELEMENT_GAP, 0);
        lv_obj_add_event_cb(wifi_btn, ui_wifi_icon_event_cb, LV_EVENT_CLICKED, NULL);

        s_ui.wifi_icon_label = lv_label_create(wifi_btn);
        lv_label_set_text(s_ui.wifi_icon_label, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_font(s_ui.wifi_icon_label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(s_ui.wifi_icon_label, lv_color_hex(UI_C_TEXT_MED), 0);
        lv_obj_center(s_ui.wifi_icon_label);

        /* 未连接样式：在标准 WiFi 图标中央叠加同色短竖线。 */
        static const lv_point_precise_t s_wifi_off_line_pts[] = {{1, 0}, {1, 12}};
        s_ui.wifi_off_line = lv_line_create(wifi_btn);
        lv_line_set_points(s_ui.wifi_off_line, s_wifi_off_line_pts, 2);
        lv_obj_set_style_line_width(s_ui.wifi_off_line, 2, 0);
        lv_obj_set_style_line_color(s_ui.wifi_off_line, lv_color_hex(UI_C_TEXT_MED), 0);
        lv_obj_set_style_line_rounded(s_ui.wifi_off_line, true, 0);
        lv_obj_clear_flag(s_ui.wifi_off_line, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(s_ui.wifi_off_line, LV_ALIGN_CENTER, 0, -4);
        lv_obj_add_flag(s_ui.wifi_off_line, LV_OBJ_FLAG_HIDDEN);

        /* 清洁入口：自绘刷子图标，不依赖额外图标字体。 */
        lv_obj_t *clean_btn = lv_button_create(status_bar);
        ui_reset_base_obj(clean_btn);
        lv_obj_set_size(clean_btn, 28, 28);
        lv_obj_set_style_bg_opa(clean_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(clean_btn, LV_OPA_10, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(clean_btn, lv_color_hex(UI_C_PANEL), LV_STATE_PRESSED);
        lv_obj_align_to(clean_btn, wifi_btn, LV_ALIGN_OUT_LEFT_MID,
                        -UI_TOPBAR_ELEMENT_GAP, 0);
        lv_obj_add_event_cb(clean_btn, ui_clean_mode_event_cb, LV_EVENT_CLICKED, NULL);
        ui_build_clean_icon(clean_btn);

    }
    ui_refresh_topbar();

    /* 主体控制区：整屏大字熟度选择（跟手左右拖拽或左右箭头调节）。 */
    ui_build_doneness_selector(screen);
    /* 整屏图跟手拖拽切换：需要屏幕可接收指针按压事件且不滚动。
     * 回调挂在常驻 screen 上，ui_screen_reset 不会移除事件回调，
     * 每次重建主页前先移除旧回调，避免重复挂载。 */
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_event_cb(screen, ui_doneness_drag_cb);
    lv_obj_add_event_cb(screen, ui_doneness_drag_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(screen, ui_doneness_drag_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(screen, ui_doneness_drag_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(screen, ui_doneness_drag_cb, LV_EVENT_PRESS_LOST, NULL);

    /* debug 入口，完成解锁手势后显示。 */
    debug_button = lv_button_create(screen);
    ui_style_pressable_button(debug_button, UI_DEBUG_BUTTON_W, UI_DEBUG_BUTTON_H, 7);
    lv_obj_set_style_bg_opa(debug_button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(debug_button, lv_color_hex(UI_C_ACCENT), 0);
    lv_obj_set_style_bg_color(debug_button, lv_color_hex(UI_C_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_style_opa(debug_button, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(debug_button, 1, 0);
    lv_obj_set_style_border_color(debug_button, lv_color_hex(UI_C_PRIMARY_TOP), 0);
    lv_obj_set_style_border_opa(debug_button, LV_OPA_COVER, 0);
    lv_obj_set_pos(debug_button, UI_DEBUG_BUTTON_X, UI_DEBUG_BUTTON_Y);
    lv_obj_add_event_cb(debug_button, ui_debug_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(debug_button);
    lv_label_set_text(label, "DEBUG");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_BG), 0);
    lv_obj_set_style_text_letter_space(label, 1, 0);
    lv_obj_center(label);
    s_ui.debug_button = debug_button;
    ui_apply_debug_button_visibility();

    separator = ui_create_separator(screen, 320, 1, UI_C_LINE_SOFT);
    lv_obj_align(separator, LV_ALIGN_BOTTOM_MID, 0, -66);
    /* 设计稿主界面为整屏图，无分隔线；隐藏以免压在描述文字上。 */
    lv_obj_add_flag(separator, LV_OBJ_FLAG_HIDDEN);

    /* 运行进度灯条，待机或完成时隐藏。 */
    s_ui.progress_track = lv_obj_create(screen);
    ui_reset_base_obj(s_ui.progress_track);
    lv_obj_clear_flag(s_ui.progress_track, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_ui.progress_track, 320, 2);
    lv_obj_set_style_bg_opa(s_ui.progress_track, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_ui.progress_track, lv_color_hex(UI_C_LINE), 0);
    lv_obj_align(s_ui.progress_track, LV_ALIGN_BOTTOM_MID, 0, -66);
    lv_obj_add_flag(s_ui.progress_track, LV_OBJ_FLAG_HIDDEN);

    {
        lv_obj_t *dot = lv_obj_create(s_ui.progress_track);
        ui_reset_base_obj(dot);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(dot, 80, 2);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(UI_COLOR_PROGRESS_ON), 0);
        lv_obj_set_style_shadow_width(dot, 8, 0);
        lv_obj_set_style_shadow_color(dot, lv_color_hex(UI_COLOR_PROGRESS_ON), 0);
        lv_obj_set_style_shadow_opa(dot, LV_OPA_50, 0);
        lv_obj_set_pos(dot, -80, 0);

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, dot);
        lv_anim_set_exec_cb(&a, anim_x_cb);
        lv_anim_set_values(&a, -80, 320);
        lv_anim_set_duration(&a, 1400);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, lv_anim_path_linear);
        lv_anim_start(&a);
    }

    /* 底部按钮行：用 flex 布局平分开始/预约两颗按钮，保证不会重叠。 */
    lv_obj_t *btn_row = lv_obj_create(screen);
    ui_reset_base_obj(btn_row);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_size(btn_row, 440, 44);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, 16, 0);

    /* 底部左侧开始按钮：待机金色/制作中深蓝/完成绿色三态（样式参考设计图）。 */
    button = lv_button_create(btn_row);
    ui_reset_base_obj(button);
    lv_obj_set_height(button, 44);
    lv_obj_set_flex_grow(button, 1);
    lv_obj_set_style_radius(button, 22, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_C_PRIMARY_TOP), 0);
    lv_obj_set_style_bg_grad_color(button, lv_color_hex(UI_C_PRIMARY_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(button, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_width(button, 8, 0);
    lv_obj_set_style_shadow_color(button, lv_color_hex(UI_C_PRIMARY_BOTTOM), 0);
    lv_obj_set_style_shadow_opa(button, LV_OPA_40, 0);
    lv_obj_set_style_opa(button, LV_OPA_90, LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, ui_start_event_cb, LV_EVENT_CLICKED, NULL);
    s_ui.start_btn = button;
    label = lv_label_create(button);
    lv_label_set_text(label, ui_t(UI_STR_START_MAKE));
    lv_obj_set_style_text_font(label, &ui_egg_btn_20, 0);
    lv_obj_set_style_text_letter_space(label, 4, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x422006), 0);
    lv_obj_center(label);
    s_ui.start_label = label;

    /* 底部右侧预约按钮：深蓝底 + 金色描边（功能待实现）。 */
    {
        lv_obj_t *reserve = lv_button_create(btn_row);
        ui_reset_base_obj(reserve);
        lv_obj_set_height(reserve, 44);
        lv_obj_set_flex_grow(reserve, 1);
        lv_obj_set_style_radius(reserve, 22, 0);
        lv_obj_set_style_bg_opa(reserve, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(reserve, lv_color_hex(0x1E293B), 0);
        lv_obj_set_style_bg_grad_color(reserve, lv_color_hex(0x0F172A), 0);
        lv_obj_set_style_bg_grad_dir(reserve, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_width(reserve, 1, 0);
        lv_obj_set_style_border_color(reserve, lv_color_hex(UI_C_ACCENT), 0);
        lv_obj_set_style_border_opa(reserve, LV_OPA_50, 0);
        lv_obj_set_style_opa(reserve, LV_OPA_90, LV_STATE_PRESSED);
        lv_obj_add_event_cb(reserve, ui_reserve_event_cb, LV_EVENT_CLICKED, NULL);
        s_ui.reserve_btn = reserve;
        s_ui.reserve_label = lv_label_create(reserve);
        lv_label_set_text(s_ui.reserve_label, u8"预约");
        lv_obj_set_style_text_font(s_ui.reserve_label, &ui_egg_btn_20, 0);
        lv_obj_set_style_text_letter_space(s_ui.reserve_label, 4, 0);
        lv_obj_set_style_text_color(s_ui.reserve_label, lv_color_hex(UI_C_ACCENT), 0);
        lv_obj_center(s_ui.reserve_label);
    }

    /* ---- 通信状态指示器 ---- */
    /* 定时刷新远端心跳和遥测信息。 */
    s_ui.conn_check_timer = lv_timer_create(ui_conn_check_timer_cb, UI_CONN_CHECK_MS, NULL);

    ui_apply_debug_metrics_visibility();
    if(s_ui.status_bar != NULL) {
        lv_obj_move_foreground(s_ui.status_bar);
    }
    lv_obj_move_foreground(debug_button);

    /* 设计稿弹窗：数量选择（内嵌预约时间选择器，默认隐藏，置于最顶层）。 */
    ui_build_qty_panel(screen);

    /* 离线时间设置弹窗（默认隐藏）。 */
    ui_build_time_panel(screen);

    /* 设计稿“制作中”整屏进度浮层（默认隐藏，运行时置顶显示）。 */
    ui_build_cook_overlay(screen);

    /* 首帧立即同步在线颜色，避免等待首个 500 ms 定时周期。 */
    ui_conn_check_timer_cb(NULL);
    ui_refresh();
}

void ui_home_on_data_changed(void)
{
    const ui_data_state_t *state = ui_get_state();

    if(s_cancel_stop_requested && state != NULL &&
       (state->maker_state == UI_MAKER_STOPPING ||
        state->maker_state == UI_MAKER_STOPPED_NEEDS_RECOVERY)) {
        s_cancel_stop_requested = false;
        if(s_ui.active) {
            buzzer_beep_async(50);
            ui_open_cancel_notice();
        }
        return;
    }

    if(s_ui.active) {
        ui_refresh();
    }
}
