#include "ui_bypass_switch.h"

#include "buzzer.h"
#include "ui.h"
#include "ui_alert.h"
#include "ui_common.h"
#include "ui_debug.h"
#include "ui_debug_internal.h"
#include "ui_uart_bridge.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define UI_BYPASS_SENSOR_COUNT     6U
#define UI_BYPASS_ITEM_COUNT       8U
#define UI_BYPASS_ALL_MASK         0x3FU
#define UI_BYPASS_COMMAND_TIMEOUT  2000U
#define UI_BYPASS_REFRESH_MS       120U
#define UI_BYPASS_COLUMN_COUNT     2U
#define UI_BYPASS_CARD_W           144
#define UI_BYPASS_CARD_H           38
#define UI_BYPASS_COLUMN_GAP       8
#define UI_BYPASS_ROW_GAP          4
#define UI_BYPASS_GRID_TOP         52

typedef enum {
    UI_BYPASS_KIND_NO_HEAT = 0,
    UI_BYPASS_KIND_SENSOR,
    UI_BYPASS_KIND_ALL,
} ui_bypass_item_kind_t;

typedef struct {
    const char *label;
    uint8_t bit;
    ui_bypass_item_kind_t kind;
} ui_bypass_item_t;

typedef struct {
    bool active;
    bool command_pending;
    uint8_t pending_mask;
    bool pending_no_heat;
    ui_bypass_item_kind_t pending_kind;
    uint32_t command_started_ms;
    ui_data_t *data;
    lv_obj_t *switches[UI_BYPASS_ITEM_COUNT];
    lv_obj_t *status_label;
    lv_timer_t *timer;
} ui_bypass_view_t;

static const ui_bypass_item_t k_bypass_items[UI_BYPASS_ITEM_COUNT] = {
    {"全部启用", 0U, UI_BYPASS_KIND_ALL},
    {"禁止加热", 0U, UI_BYPASS_KIND_NO_HEAT},
    {"跳过测距\n自检", 1U << 0U, UI_BYPASS_KIND_SENSOR},
    {"跳过给蛋\n前检测", 1U << 1U, UI_BYPASS_KIND_SENSOR},
    {"跳过给蛋\n后检测", 1U << 2U, UI_BYPASS_KIND_SENSOR},
    {"跳过蛋壳\n检测", 1U << 3U, UI_BYPASS_KIND_SENSOR},
    {"跳过油路\n反馈", 1U << 4U, UI_BYPASS_KIND_SENSOR},
    {"跳过锅具\n检测", 1U << 5U, UI_BYPASS_KIND_SENSOR},
};

static ui_bypass_view_t s_bypass;

static void ui_bypass_stop_timer(void)
{
    if(s_bypass.timer != NULL) {
        lv_timer_delete(s_bypass.timer);
        s_bypass.timer = NULL;
    }
}

static unsigned ui_bypass_count_enabled(uint8_t mask)
{
    unsigned count = 0U;

    for(uint8_t bit = 0U; bit < UI_BYPASS_SENSOR_COUNT; bit++) {
        if((mask & (1U << bit)) != 0U) {
            count++;
        }
    }
    return count;
}

static void ui_bypass_refresh(void)
{
    ui_uart_heartbeat_state_t heartbeat;
    uint8_t remote_mask;
    uint8_t display_mask;
    bool remote_no_heat;
    bool display_no_heat;
    bool editable;
    char status[48];

    if(!s_bypass.active) {
        return;
    }

    ui_uart_bridge_get_heartbeat_state(&heartbeat);
    remote_mask = heartbeat.has_remote_debug_bypass
                      ? (uint8_t)(heartbeat.remote_debug_bypass_mask & UI_BYPASS_ALL_MASK)
                      : 0U;
    remote_no_heat = heartbeat.has_remote_no_heat && heartbeat.remote_no_heat;

    if(s_bypass.command_pending) {
        bool confirmed;
        if(s_bypass.pending_kind == UI_BYPASS_KIND_ALL) {
            confirmed = heartbeat.has_remote_debug_bypass &&
                        heartbeat.has_remote_no_heat &&
                        remote_mask == s_bypass.pending_mask &&
                        heartbeat.remote_no_heat == s_bypass.pending_no_heat;
        } else if(s_bypass.pending_kind == UI_BYPASS_KIND_NO_HEAT) {
            confirmed = heartbeat.has_remote_no_heat &&
                        heartbeat.remote_no_heat == s_bypass.pending_no_heat;
        } else {
            confirmed = heartbeat.has_remote_debug_bypass &&
                        remote_mask == s_bypass.pending_mask;
        }
        if(confirmed) {
            s_bypass.command_pending = false;
        } else if(lv_tick_elaps(s_bypass.command_started_ms) >= UI_BYPASS_COMMAND_TIMEOUT) {
            s_bypass.command_pending = false;
            buzzer_beep_async(120);
        }
    }

    display_mask = (s_bypass.command_pending &&
                    (s_bypass.pending_kind == UI_BYPASS_KIND_SENSOR ||
                     s_bypass.pending_kind == UI_BYPASS_KIND_ALL))
                       ? s_bypass.pending_mask
                       : remote_mask;
    display_no_heat = (s_bypass.command_pending &&
                       (s_bypass.pending_kind == UI_BYPASS_KIND_NO_HEAT ||
                        s_bypass.pending_kind == UI_BYPASS_KIND_ALL))
                          ? s_bypass.pending_no_heat
                          : remote_no_heat;
    editable = heartbeat.online &&
               !heartbeat.remote_start &&
               heartbeat.remote_error_code == 0 &&
               heartbeat.has_remote_debug_bypass &&
               heartbeat.has_remote_no_heat &&
               !s_bypass.command_pending;

    for(uint32_t i = 0U; i < UI_BYPASS_ITEM_COUNT; i++) {
        lv_obj_t *switch_obj = s_bypass.switches[i];
        if(switch_obj == NULL) {
            continue;
        }

        bool checked;
        if(k_bypass_items[i].kind == UI_BYPASS_KIND_ALL) {
            checked = display_no_heat && display_mask == UI_BYPASS_ALL_MASK;
        } else if(k_bypass_items[i].kind == UI_BYPASS_KIND_NO_HEAT) {
            checked = display_no_heat;
        } else {
            checked = (display_mask & k_bypass_items[i].bit) != 0U;
        }
        if(checked) {
            lv_obj_add_state(switch_obj, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(switch_obj, LV_STATE_CHECKED);
        }

        if(editable) {
            lv_obj_remove_state(switch_obj, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(switch_obj, LV_STATE_DISABLED);
        }
    }

    if(s_bypass.status_label == NULL) {
        return;
    }

    if(!heartbeat.online) {
        lv_label_set_text(s_bypass.status_label, "主控离线");
    } else if(heartbeat.remote_start) {
        lv_label_set_text(s_bypass.status_label, "运行中不可修改");
    } else if(heartbeat.remote_error_code != 0) {
        lv_label_set_text(s_bypass.status_label, "故障中不可修改");
    } else if(!heartbeat.has_remote_debug_bypass || !heartbeat.has_remote_no_heat) {
        lv_label_set_text(s_bypass.status_label, "等待调试状态");
    } else if(s_bypass.command_pending) {
        lv_label_set_text(s_bypass.status_label, "修改中");
    } else if(display_mask == 0U && !display_no_heat) {
        lv_label_set_text(s_bypass.status_label, "黄色为启用 当前全关");
    } else {
        (void)snprintf(status, sizeof(status), "黄色为启用 已开启 %u 项",
                       ui_bypass_count_enabled(display_mask) +
                           (display_no_heat ? 1U : 0U));
        lv_label_set_text(s_bypass.status_label, status);
    }
}

static void ui_bypass_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    ui_bypass_refresh();
}

static void ui_bypass_back_event_cb(lv_event_t *event)
{
    LV_UNUSED(event);

    buzzer_beep_async(50);
    s_bypass.active = false;
    ui_bypass_stop_timer();
    ui_debug_create(s_bypass.data);
}

static void ui_bypass_switch_event_cb(lv_event_t *event)
{
    const ui_bypass_item_t *item =
        (const ui_bypass_item_t *)lv_event_get_user_data(event);
    lv_obj_t *switch_obj = lv_event_get_target(event);
    ui_uart_heartbeat_state_t heartbeat;
    uint8_t current_mask;
    uint8_t next_mask;
    bool enabled;
    char command[24];

    if(item == NULL || switch_obj == NULL) {
        return;
    }

    ui_uart_bridge_get_heartbeat_state(&heartbeat);
    if(!heartbeat.online ||
       heartbeat.remote_start ||
       heartbeat.remote_error_code != 0 ||
       !heartbeat.has_remote_debug_bypass ||
       !heartbeat.has_remote_no_heat ||
       s_bypass.command_pending) {
        buzzer_beep_async(120);
        ui_bypass_refresh();
        return;
    }

    current_mask = (uint8_t)(heartbeat.remote_debug_bypass_mask & UI_BYPASS_ALL_MASK);
    enabled = lv_obj_has_state(switch_obj, LV_STATE_CHECKED);
    next_mask = current_mask;
    if(item->kind == UI_BYPASS_KIND_ALL) {
        next_mask = enabled ? UI_BYPASS_ALL_MASK : 0U;
        (void)snprintf(command, sizeof(command), "DBG ALL %u", enabled ? 1U : 0U);
    } else if(item->kind == UI_BYPASS_KIND_NO_HEAT) {
        (void)snprintf(command, sizeof(command), "DBG NOHEAT %u", enabled ? 1U : 0U);
    } else {
        next_mask = enabled ? (uint8_t)(current_mask | item->bit)
                            : (uint8_t)(current_mask & (uint8_t)~item->bit);
        (void)snprintf(command, sizeof(command), "DBG MASK %u", (unsigned)next_mask);
    }
    if(ui_uart_bridge_send_raw_command(command, true) != ESP_OK) {
        buzzer_beep_async(120);
        ui_bypass_refresh();
        return;
    }

    s_bypass.command_pending = true;
    s_bypass.pending_mask = next_mask;
    s_bypass.pending_no_heat = enabled;
    s_bypass.pending_kind = item->kind;
    s_bypass.command_started_ms = lv_tick_get();
    buzzer_beep_async(50);
    ui_bypass_refresh();
}

static void ui_bypass_create_row(lv_obj_t *screen, uint32_t index)
{
    lv_obj_t *row;
    lv_obj_t *label;
    lv_obj_t *switch_obj;
    const uint32_t column = index % UI_BYPASS_COLUMN_COUNT;
    const uint32_t grid_row = index / UI_BYPASS_COLUMN_COUNT;
    const lv_coord_t x = column == 0U
                             ? -(UI_BYPASS_CARD_W + UI_BYPASS_COLUMN_GAP) / 2
                             : (UI_BYPASS_CARD_W + UI_BYPASS_COLUMN_GAP) / 2;
    const lv_coord_t y =
        UI_BYPASS_GRID_TOP +
        (lv_coord_t)grid_row * (UI_BYPASS_CARD_H + UI_BYPASS_ROW_GAP);

    row = lv_obj_create(screen);
    ui_reset_base_obj(row);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(row, UI_BYPASS_CARD_W, UI_BYPASS_CARD_H);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_C_PANEL), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(UI_C_LINE), 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, x, y);

    label = lv_label_create(row);
    lv_label_set_text(label, k_bypass_items[index].label);
    lv_obj_set_style_text_font(label, &ui_sensor_debug_font_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_set_style_text_line_space(label, -2, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 8, 0);

    switch_obj = lv_switch_create(row);
    lv_obj_set_size(switch_obj, 42, 22);
    lv_obj_set_style_bg_color(switch_obj, lv_color_hex(UI_C_LINE),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(switch_obj, lv_color_hex(UI_C_YELLOW),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(switch_obj, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(switch_obj, lv_color_hex(UI_C_TEXT_STRONG),
                              LV_PART_KNOB);
    lv_obj_align(switch_obj, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_add_event_cb(switch_obj,
                        ui_bypass_switch_event_cb,
                        LV_EVENT_VALUE_CHANGED,
                        (void *)&k_bypass_items[index]);
    s_bypass.switches[index] = switch_obj;
}

void ui_bypass_switch_create(ui_data_t *data)
{
    const ui_data_state_t *state;
    lv_obj_t *screen;
    lv_obj_t *button;
    lv_obj_t *label;

    if(data == NULL) {
        return;
    }

    state = ui_data_get_state(data);
    if(ui_alert_should_show(state)) {
        ui_alert_create(data);
        return;
    }

    ui_debug_stop_serial_timer();
    ui_bypass_stop_timer();
    lv_memzero(&s_bypass, sizeof(s_bypass));
    s_bypass.active = true;
    s_bypass.data = data;
    s_debug.active = false;

    screen = lv_screen_active();
    ui_screen_reset(screen);

    button = lv_button_create(screen);
    ui_debug_style_button(button, UI_DEBUG_BACK_BTN_W, UI_DEBUG_BACK_BTN_H, 13);
    lv_obj_align(button, LV_ALIGN_TOP_LEFT, UI_DEBUG_SIDE_MARGIN, UI_DEBUG_TOP_ROW_Y);
    lv_obj_add_event_cb(button, ui_bypass_back_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(button);
    lv_label_set_text(label, "BACK");
    lv_obj_set_style_text_font(label, &egg_ui_font_12, 0);
    lv_obj_set_style_text_letter_space(label, 1, 0);
    lv_obj_center(label);

    label = lv_label_create(screen);
    lv_label_set_text(label, "调试开关");
    lv_obj_set_style_text_font(label, &ui_sensor_debug_font_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_RED), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 9);

    s_bypass.status_label = lv_label_create(screen);
    lv_label_set_text(s_bypass.status_label, "等待调试状态");
    lv_obj_set_style_text_font(s_bypass.status_label, &ui_sensor_debug_font_16, 0);
    lv_obj_set_style_text_color(s_bypass.status_label, lv_color_hex(UI_C_TEXT_MED), 0);
    lv_obj_align(s_bypass.status_label, LV_ALIGN_TOP_MID, 0, 32);

    for(uint32_t i = 0U; i < UI_BYPASS_ITEM_COUNT; i++) {
        ui_bypass_create_row(screen, i);
    }

    ui_bypass_refresh();
    s_bypass.timer = lv_timer_create(ui_bypass_timer_cb, UI_BYPASS_REFRESH_MS, NULL);
}
