#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "ui_data.h"

typedef struct {
    bool online;
    bool remote_start;
    int32_t remote_error_code;
    int32_t remote_need_make;
    int32_t remote_remaining_s;
    bool has_remote_remaining_s;
    int32_t remote_total_make_s;
    bool has_remote_total_make_s;
    int32_t remote_olo;
    int32_t remote_dno;
    int32_t remote_temp_adjust;
    int32_t remote_power_offset_first;
    int32_t remote_power_offset_second;
    int32_t remote_dw_adjust;
    bool remote_laser_check;
    bool remote_second_oil;
    bool remote_debug_bypass;
    uint32_t remote_debug_bypass_mask;
    bool has_remote_debug_bypass;
    bool remote_no_heat;
    bool has_remote_no_heat;
    bool has_remote_offsets;
    int32_t remote_oil_level;
    int32_t remote_doneness;
    bool has_remote_prefs;
    int32_t remote_maker_state;
    bool has_remote_maker_state;
    int32_t remote_potb_temp;
    int32_t remote_pota_temp;
    bool has_remote_pot_temps;
    int32_t remote_potb_igbt_temp;
    int32_t remote_pota_igbt_temp;
    bool has_remote_pot_igbt_temps;
    int32_t remote_potb_dw;
    int32_t remote_pota_dw;
    bool has_remote_pot_dw;
    int32_t remote_potb_remaining_heat_s;
    int32_t remote_pota_remaining_heat_s;
    bool has_remote_pot_remaining_heat_s;
    bool remote_reservation_active;
    uint32_t remote_reservation_epoch;
    uint8_t remote_reservation_qty;
    uint8_t remote_reservation_status;
    bool remote_clock_valid;
    bool has_remote_reservation;
    uint32_t last_ok_ms;
} ui_uart_heartbeat_state_t;

typedef enum {
    UI_UART_STS_RESULT_IDLE = 0,
    UI_UART_STS_RESULT_PENDING,
    UI_UART_STS_RESULT_SUCCESS,
    UI_UART_STS_RESULT_FAILED,
    UI_UART_STS_RESULT_TIMEOUT,
} ui_uart_sts_result_state_t;

typedef struct {
    uint32_t seq;
    char action[32];
    ui_uart_sts_result_state_t state;
} ui_uart_sts_result_t;

esp_err_t ui_uart_bridge_init(void);
void ui_uart_bridge_bind_ui_data(ui_data_t *data);
void ui_uart_bridge_handle_event(ui_data_event_t event, const ui_data_state_t *state, void *user_data);
esp_err_t ui_uart_bridge_request_settings_sync(void);
esp_err_t ui_uart_bridge_send_raw_command(const char *command, bool require_ack);
esp_err_t ui_uart_bridge_clear_need_make(uint8_t count);
esp_err_t ui_uart_bridge_set_reservation(uint32_t epoch,
                                         uint8_t quantity,
                                         ui_oil_mode_t oil_mode,
                                         ui_doneness_t doneness);
esp_err_t ui_uart_bridge_cancel_reservation(void);
esp_err_t ui_uart_bridge_send_sts_command(const char *action, uint16_t timeout_ms);
void ui_uart_bridge_clear_last_rx(void);
void ui_uart_bridge_get_last_messages(char *tx, size_t tx_size, char *rx, size_t rx_size);
uint32_t ui_uart_bridge_get_rx_history_cursor(void);
bool ui_uart_bridge_get_next_rx_line(uint32_t *cursor, char *rx, size_t rx_size);
void ui_uart_bridge_get_heartbeat_state(ui_uart_heartbeat_state_t *state);
void ui_uart_bridge_get_sts_result(ui_uart_sts_result_t *result);
