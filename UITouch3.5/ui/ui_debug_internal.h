#pragma once

#include "ui_common.h"
#include "ui_data.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UI_DEBUG_UART_TEXT_MAX 32
#define UI_DEBUG_RX_LINE_MAX   160
#define UI_DEBUG_SIDE_MARGIN   8
#define UI_DEBUG_TOP_ROW_Y     6
#define UI_DEBUG_TOP_ROW_GAP   6
#define UI_DEBUG_BACK_BTN_W    58
#define UI_DEBUG_BACK_BTN_H    26
#define UI_DEBUG_UART_BOX_W    117
#define UI_DEBUG_UART_BOX_H    22

typedef enum {
    UI_DEBUG_DISTANCE_TARGET_NONE = 0,
    UI_DEBUG_DISTANCE_TARGET_ENV_TEMP,
    UI_DEBUG_DISTANCE_TARGET_TRAY,
    UI_DEBUG_DISTANCE_TARGET_PAN_A,
} ui_debug_distance_target_t;

typedef enum {
    UI_DEBUG_STATUS_TARGET_NONE = 0,
    UI_DEBUG_STATUS_TARGET_PUSH,
    UI_DEBUG_STATUS_TARGET_POSITION,
    UI_DEBUG_STATUS_TARGET_OIL,
} ui_debug_status_target_t;

typedef struct {
    bool active;
    ui_data_t *data;
    lv_obj_t *tx_value_label;
    lv_obj_t *rx_value_label;
    lv_obj_t *env_temp_value_label;
    lv_obj_t *tray_distance_value_label;
    lv_obj_t *pan_a_distance_value_label;
    lv_obj_t *push_state_label;
    lv_obj_t *position_state_label;
    lv_obj_t *oil_state_label;
    lv_obj_t *heat_a_dw_label;
    lv_obj_t *heat_a_time_label;
    lv_obj_t *heat_b_dw_label;
    lv_obj_t *heat_b_time_label;
    lv_obj_t *heat_a_addr_label;
    lv_obj_t *heat_a_temp_ad_label;
    lv_obj_t *heat_a_temp_value_label;
    lv_obj_t *heat_a_igbt_ad_label;
    lv_obj_t *heat_a_igbt_temp_value_label;
    lv_obj_t *heat_a_voltage_ad_label;
    lv_obj_t *heat_a_current_ad_label;
    lv_obj_t *heat_a_ppg_label;
    lv_obj_t *heat_a_status_label;
    lv_obj_t *heat_b_addr_label;
    lv_obj_t *heat_b_temp_ad_label;
    lv_obj_t *heat_b_temp_value_label;
    lv_obj_t *heat_b_igbt_ad_label;
    lv_obj_t *heat_b_igbt_temp_value_label;
    lv_obj_t *heat_b_voltage_ad_label;
    lv_obj_t *heat_b_current_ad_label;
    lv_obj_t *heat_b_ppg_label;
    lv_obj_t *heat_b_status_label;
    lv_timer_t *serial_timer;
    char last_tx[UI_DEBUG_UART_TEXT_MAX];
    char last_rx[UI_DEBUG_UART_TEXT_MAX];
    char last_env_temp[UI_DEBUG_UART_TEXT_MAX];
    char last_tray_distance[UI_DEBUG_UART_TEXT_MAX];
    char last_pan_a_distance[UI_DEBUG_UART_TEXT_MAX];
    char last_push_state[UI_DEBUG_UART_TEXT_MAX];
    char last_position_state[UI_DEBUG_UART_TEXT_MAX];
    char last_oil_state[UI_DEBUG_UART_TEXT_MAX];
    uint32_t rx_history_cursor;
    uint32_t pending_distance_started_ms;
    uint32_t pending_push_status_started_ms;
    uint32_t pending_position_status_started_ms;
    uint32_t pending_oil_status_started_ms;
    uint32_t heater_debug_last_poll_ms;
    ui_debug_distance_target_t pending_distance_target;
    bool heater_debug_active;
    int heat_a_dw;
    int heat_a_time;
    int heat_b_dw;
    int heat_b_time;
} ui_debug_view_t;

extern ui_debug_view_t s_debug;

void ui_debug_style_button(lv_obj_t *button, lv_coord_t width, lv_coord_t height, lv_coord_t radius);
void ui_debug_style_box(lv_obj_t *box, lv_coord_t width, lv_coord_t height, lv_coord_t radius);
lv_obj_t *ui_debug_create_uart_box(lv_obj_t *parent, const char *tag_text, lv_obj_t **out_value_label);
void ui_debug_stop_serial_timer(void);
void ui_debug_start_serial_timer(void);
