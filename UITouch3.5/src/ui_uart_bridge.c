#include "ui_uart_bridge.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "buzzer.h"
#include "lvgl_port.h"
#include "ui.h"
#include "ui_alert.h"
#include "ui_debug.h"
#include "ui_debug_stress_test.h"
#include "ui_notice.h"
#include "ui_settings.h"
#include "uart_link.h"

#define UI_UART_COMMAND_QUEUE_LEN 8
#define UI_UART_HEARTBEAT_QUEUE_LEN 1
#define UI_UART_MSG_MAX_LEN 384
#define UI_UART_HEARTBEAT_COMMAND_MAX_LEN 64
#define UI_UART_OPERATION_MAX_LEN 64
#define UI_UART_RX_LINE_MAX 1024
#define UI_UART_TEXT_MAX_LEN 1024
#define UI_UART_HISTORY_TEXT_MAX_LEN 384
#define UI_UART_RX_HISTORY_LEN 48
#define UI_UART_TASK_STACK  (6 * 1024)
#define UI_UART_HEARTBEAT_TASK_STACK (4 * 1024)
#define UI_UART_TASK_PRIO   4
#define UI_UART_USER_ENQUEUE_WAIT_MS 20
#define UI_UART_ACK_TIMEOUT_MS 300
#define UI_UART_MAX_ATTEMPTS   3
#define UI_UART_HEARTBEAT_INTERVAL_MS 1000
#define UI_UART_POLL_TIMEOUT_MS 500
#define UI_UART_POLL_MAX_ATTEMPTS 2
#define UI_UART_SETTINGS_SYNC_COMMAND "config.get"
#define UI_UART_HEARTBEAT_COMMAND "state.get"

static const char *TAG = "ui_uart";

typedef enum {
    UI_UART_RESPONSE_COMMAND = 0,
    UI_UART_RESPONSE_SETTINGS_SYNC,
    UI_UART_RESPONSE_HEARTBEAT,
    UI_UART_RESPONSE_STS,
} ui_uart_response_t;

typedef struct {
    char command[UI_UART_MSG_MAX_LEN];
    char operation[UI_UART_OPERATION_MAX_LEN];
    uint32_t request_id;
    ui_uart_response_t response;
    bool store_in_history;
    uint16_t timeout_ms;
    uint8_t max_attempts;
    bool is_need_make_cmd;
} ui_uart_message_t;

typedef struct {
    uint32_t seq;
    char text[UI_UART_HISTORY_TEXT_MAX_LEN];
} ui_uart_rx_history_entry_t;

static QueueHandle_t s_ui_uart_command_queue;
static QueueHandle_t s_ui_uart_heartbeat_queue;
static QueueSetHandle_t s_ui_uart_queue_set;
static TaskHandle_t s_ui_uart_task;
static TaskHandle_t s_ui_uart_heartbeat_task;
static portMUX_TYPE s_ui_uart_ack_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_ui_uart_text_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_ui_uart_heartbeat_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_expected_operation[UI_UART_OPERATION_MAX_LEN];
static char s_expected_action[32];
static char s_rx_line[UI_UART_RX_LINE_MAX];
static char s_last_tx[UI_UART_TEXT_MAX_LEN];
static char s_last_rx[UI_UART_TEXT_MAX_LEN];
static ui_uart_rx_history_entry_t s_rx_history[UI_UART_RX_HISTORY_LEN];
static ui_uart_heartbeat_state_t s_heartbeat_state;
static ui_uart_sts_result_t s_last_sts_result;
static ui_uart_heartbeat_state_t s_pending_heartbeat_state;
static ui_data_t *s_bound_ui_data;
static uint8_t s_pending_need_make_count;
static uint32_t s_rx_history_latest_seq;
static uint32_t s_next_request_id = 1U;
static uint32_t s_expected_request_id;
static size_t s_rx_line_len;
static ui_uart_response_t s_pending_response;
static bool s_response_pending;
static bool s_pending_response_succeeded;
static bool s_has_pending_heartbeat_state;
static bool s_rx_line_dropping;
static bool s_settings_sync_done;
/*
 * 自上次成功 apply 心跳到 UI 后，心跳是否出现过中断。
 * 用途：区分“运行中自然结束”（应奏完成音）与“链路中断后恢复”（不应奏）。
 * MainControl 重启 / 烧录 / 断电再上电时，恢复第一帧会看到 start:1->0,
 * 若不区分会把这种恢复误判为刚刚完成一份而误响蜂鸣。
 */
static bool s_heartbeat_interrupted_since_apply = true; /* 上电初始视为已中断，避免上电误调一次完成音 */

static bool ui_uart_next_token(const char **cursor, char *token, size_t token_size)
{
    const char *start;
    size_t len;

    if(cursor == NULL || *cursor == NULL || token == NULL || token_size == 0U) {
        return false;
    }

    while(**cursor == ' ' || **cursor == '\t') {
        (*cursor)++;
    }
    if(**cursor == '\0') {
        token[0] = '\0';
        return false;
    }

    start = *cursor;
    while(**cursor != '\0' && **cursor != ' ' && **cursor != '\t') {
        (*cursor)++;
    }
    len = (size_t)(*cursor - start);
    if(len >= token_size) {
        return false;
    }
    memcpy(token, start, len);
    token[len] = '\0';
    return true;
}

static bool ui_uart_parse_uint32(const char *text, uint32_t *value)
{
    char *end;
    unsigned long parsed;

    if(text == NULL || text[0] == '\0' || value == NULL) {
        return false;
    }
    for(const char *digit = text; *digit != '\0'; digit++) {
        if(*digit < '0' || *digit > '9') {
            return false;
        }
    }
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if(errno == ERANGE || *end != '\0' || parsed > UINT32_MAX) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static bool ui_uart_parse_int32(const char *text, int32_t *value)
{
    char *end;
    long parsed;
    const char *digit;

    if(text == NULL || text[0] == '\0' || value == NULL) {
        return false;
    }
    digit = text[0] == '-' ? text + 1 : text;
    if(*digit == '\0') {
        return false;
    }
    for(; *digit != '\0'; digit++) {
        if(*digit < '0' || *digit > '9') {
            return false;
        }
    }
    errno = 0;
    parsed = strtol(text, &end, 10);
    if(errno == ERANGE || *end != '\0' || parsed < INT32_MIN || parsed > INT32_MAX) {
        return false;
    }
    *value = (int32_t)parsed;
    return true;
}

static bool ui_uart_get_field(const char *line, const char *key, char *value, size_t value_size)
{
    const char *cursor = line;
    char token[UI_UART_MSG_MAX_LEN];
    size_t key_len;

    if(line == NULL || key == NULL || value == NULL || value_size == 0U) {
        return false;
    }
    key_len = strlen(key);
    while(ui_uart_next_token(&cursor, token, sizeof(token))) {
        if(strncmp(token, key, key_len) == 0 && token[key_len] == '=' && token[key_len + 1U] != '\0') {
            return strlcpy(value, token + key_len + 1U, value_size) < value_size;
        }
    }
    return false;
}

static bool ui_uart_get_int_field(const char *line, const char *key, int32_t *value)
{
    char text[24];
    return ui_uart_get_field(line, key, text, sizeof(text)) && ui_uart_parse_int32(text, value);
}

static bool ui_uart_get_uint_field(const char *line, const char *key, uint32_t *value)
{
    char text[24];
    return ui_uart_get_field(line, key, text, sizeof(text)) && ui_uart_parse_uint32(text, value);
}

static bool ui_uart_parse_response(const char *line,
                                   uint32_t *id,
                                   char *operation,
                                   size_t operation_size,
                                   char *result,
                                   size_t result_size)
{
    const char *cursor = line;
    char token[32];

    if(!ui_uart_next_token(&cursor, token, sizeof(token)) || strcmp(token, "RSP") != 0 ||
       !ui_uart_next_token(&cursor, token, sizeof(token)) || !ui_uart_parse_uint32(token, id) || *id == 0U ||
       !ui_uart_next_token(&cursor, operation, operation_size) ||
       !ui_uart_get_field(line, "result", result, result_size)) {
        return false;
    }
    return strcmp(result, "ok") == 0 || strcmp(result, "accepted") == 0 ||
           strcmp(result, "rejected") == 0 || strcmp(result, "failed") == 0;
}

static bool ui_uart_response_succeeded(const char *result)
{
    return result != NULL && (strcmp(result, "ok") == 0 || strcmp(result, "accepted") == 0);
}

static uint32_t ui_uart_allocate_request_id(void)
{
    uint32_t id = s_next_request_id++;
    if(id == 0U) {
        id = s_next_request_id++;
    }
    if(s_next_request_id == 0U) {
        s_next_request_id = 1U;
    }
    return id;
}

static void ui_uart_set_sts_result(const char *action, ui_uart_sts_result_state_t state)
{
    portENTER_CRITICAL(&s_ui_uart_ack_lock);
    if(action != NULL) {
        strlcpy(s_last_sts_result.action, action, sizeof(s_last_sts_result.action));
    } else {
        s_last_sts_result.action[0] = '\0';
    }
    s_last_sts_result.state = state;
    if(state != UI_UART_STS_RESULT_PENDING) {
        s_last_sts_result.seq++;
    }
    portEXIT_CRITICAL(&s_ui_uart_ack_lock);
}

static bool ui_uart_message_action(const ui_uart_message_t *message, char *action, size_t action_size)
{
    return message != NULL && ui_uart_get_field(message->command, "action", action, action_size);
}

static void ui_uart_cache_settings_from_message(const ui_uart_message_t *message)
{
    char key[32];
    int32_t value;

    if(message == NULL || strcmp(message->operation, "config.set") != 0 ||
       !ui_uart_get_field(message->command, "key", key, sizeof(key)) ||
       !ui_uart_get_int_field(message->command, "value", &value)) {
        return;
    }

    portENTER_CRITICAL(&s_ui_uart_heartbeat_lock);
    if(strcmp(key, "oil") == 0) {
        s_heartbeat_state.remote_oil_level = value;
        s_heartbeat_state.has_remote_prefs = true;
        s_pending_heartbeat_state.remote_oil_level = value;
        s_pending_heartbeat_state.has_remote_prefs = true;
    } else if(strcmp(key, "doneness") == 0) {
        s_heartbeat_state.remote_doneness = value;
        s_heartbeat_state.has_remote_prefs = true;
        s_pending_heartbeat_state.remote_doneness = value;
        s_pending_heartbeat_state.has_remote_prefs = true;
    } else if(strcmp(key, "oil_offset") == 0) {
        s_heartbeat_state.remote_olo = value;
        s_heartbeat_state.has_remote_offsets = true;
        s_pending_heartbeat_state.remote_olo = value;
        s_pending_heartbeat_state.has_remote_offsets = true;
    } else if(strcmp(key, "doneness_offset") == 0) {
        s_heartbeat_state.remote_dno = value;
        s_heartbeat_state.has_remote_offsets = true;
        s_pending_heartbeat_state.remote_dno = value;
        s_pending_heartbeat_state.has_remote_offsets = true;
    } else if(strcmp(key, "max_temp_c") == 0) {
        s_heartbeat_state.remote_temp_adjust = value;
        s_heartbeat_state.has_remote_offsets = true;
        s_pending_heartbeat_state.remote_temp_adjust = value;
        s_pending_heartbeat_state.has_remote_offsets = true;
    } else if(strcmp(key, "power_offset") == 0) {
        s_heartbeat_state.remote_power_offset_first = value;
        s_heartbeat_state.remote_power_offset_second = value;
        s_heartbeat_state.has_remote_offsets = true;
        s_pending_heartbeat_state.remote_power_offset_first = value;
        s_pending_heartbeat_state.remote_power_offset_second = value;
        s_pending_heartbeat_state.has_remote_offsets = true;
    } else if(strcmp(key, "power_offset_first") == 0) {
        s_heartbeat_state.remote_power_offset_first = value;
        s_heartbeat_state.has_remote_offsets = true;
        s_pending_heartbeat_state.remote_power_offset_first = value;
        s_pending_heartbeat_state.has_remote_offsets = true;
    } else if(strcmp(key, "power_offset_second") == 0) {
        s_heartbeat_state.remote_power_offset_second = value;
        s_heartbeat_state.has_remote_offsets = true;
        s_pending_heartbeat_state.remote_power_offset_second = value;
        s_pending_heartbeat_state.has_remote_offsets = true;
    } else if(strcmp(key, "init_dw") == 0) {
        s_heartbeat_state.remote_dw_adjust = value;
        s_heartbeat_state.has_remote_offsets = true;
        s_pending_heartbeat_state.remote_dw_adjust = value;
        s_pending_heartbeat_state.has_remote_offsets = true;
    } else if(strcmp(key, "laser_check") == 0) {
        s_heartbeat_state.remote_laser_check = value != 0;
        s_heartbeat_state.has_remote_offsets = true;
        s_pending_heartbeat_state.remote_laser_check = value != 0;
        s_pending_heartbeat_state.has_remote_offsets = true;
    } else if(strcmp(key, "second_oil") == 0) {
        s_heartbeat_state.remote_second_oil = value != 0;
        s_heartbeat_state.has_remote_offsets = true;
        s_pending_heartbeat_state.remote_second_oil = value != 0;
        s_pending_heartbeat_state.has_remote_offsets = true;
    }
    portEXIT_CRITICAL(&s_ui_uart_heartbeat_lock);
}

static bool ui_uart_prepare_message(ui_uart_message_t *message,
                                    const char *command,
                                    ui_uart_response_t response,
                                    bool store_in_history,
                                    uint16_t timeout_ms,
                                    uint8_t max_attempts)
{
    const char *cursor;

    if(message == NULL || command == NULL) {
        return false;
    }

    if(strlcpy(message->command, command, sizeof(message->command)) >= sizeof(message->command)) {
        ESP_LOGW(TAG, "uart request payload too long");
        return false;
    }
    cursor = message->command;
    if(!ui_uart_next_token(&cursor, message->operation, sizeof(message->operation))) {
        return false;
    }
    message->request_id = 0U;
    message->response = response;
    message->store_in_history = store_in_history;
    message->timeout_ms = timeout_ms;
    message->max_attempts = (max_attempts == 0U) ? 1U : max_attempts;
    message->is_need_make_cmd = false;
    return true;
}

static esp_err_t ui_uart_enqueue_to_queue(QueueHandle_t queue,
                                          const char *queue_name,
                                          const ui_uart_message_t *message,
                                          TickType_t wait_ticks)
{
    if(queue == NULL || queue_name == NULL || message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if(xQueueSendToBack(queue, message, wait_ticks) != pdTRUE) {
        ESP_LOGW(TAG, "%s queue full, drop command: %s", queue_name, message->command);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void ui_uart_store_last_text(char *target, size_t target_size, const char *text)
{
    if(target == NULL || target_size == 0U) {
        return;
    }

    portENTER_CRITICAL(&s_ui_uart_text_lock);
    if(text != NULL) {
        strlcpy(target, text, target_size);
    } else {
        target[0] = '\0';
    }
    portEXIT_CRITICAL(&s_ui_uart_text_lock);
}

static void ui_uart_store_last_tx(const char *text)
{
    ui_uart_store_last_text(s_last_tx, sizeof(s_last_tx), text);
}

static void ui_uart_store_last_rx(const char *text)
{
    ui_uart_store_last_text(s_last_rx, sizeof(s_last_rx), text);
}

static void ui_uart_clear_rx_history(void)
{
    portENTER_CRITICAL(&s_ui_uart_text_lock);
    memset(s_rx_history, 0, sizeof(s_rx_history));
    s_rx_history_latest_seq = 0U;
    portEXIT_CRITICAL(&s_ui_uart_text_lock);
}

static void ui_uart_record_rx_line(const char *text)
{
    uint32_t seq;
    uint32_t slot;

    if(text == NULL || text[0] == '\0') {
        ui_uart_store_last_rx(text);
        return;
    }

    portENTER_CRITICAL(&s_ui_uart_text_lock);
    strlcpy(s_last_rx, text, sizeof(s_last_rx));

    seq = s_rx_history_latest_seq + 1U;
    s_rx_history_latest_seq = seq;
    slot = (seq - 1U) % UI_UART_RX_HISTORY_LEN;
    s_rx_history[slot].seq = seq;
    strlcpy(s_rx_history[slot].text, text, sizeof(s_rx_history[slot].text));
    portEXIT_CRITICAL(&s_ui_uart_text_lock);
}

static void ui_uart_set_heartbeat_online(bool online)
{
    bool changed;
    bool was_online;

    portENTER_CRITICAL(&s_ui_uart_heartbeat_lock);
    changed = (s_heartbeat_state.online != online);
    was_online = s_heartbeat_state.online;
    s_heartbeat_state.online = online;
    portEXIT_CRITICAL(&s_ui_uart_heartbeat_lock);

    /* 真正从在线转为失联时才标记“中断”。 */
    if(changed && !online && was_online) {
        s_heartbeat_interrupted_since_apply = true;
    }

    if(changed) {
        ESP_LOGI(TAG, "peer heartbeat %s", online ? "online" : "offline");
    }
}

static void ui_uart_set_settings_sync_done(bool synced)
{
    portENTER_CRITICAL(&s_ui_uart_heartbeat_lock);
    s_settings_sync_done = synced;
    portEXIT_CRITICAL(&s_ui_uart_heartbeat_lock);
}

static bool ui_uart_consume_pending_heartbeat_state(ui_uart_heartbeat_state_t *state)
{
    bool has_pending;

    if(state == NULL) {
        return false;
    }

    portENTER_CRITICAL(&s_ui_uart_heartbeat_lock);
    has_pending = s_has_pending_heartbeat_state;
    if(has_pending) {
        *state = s_pending_heartbeat_state;
        s_has_pending_heartbeat_state = false;
    }
    portEXIT_CRITICAL(&s_ui_uart_heartbeat_lock);
    return has_pending;
}

static bool ui_uart_parse_state_response(const char *line, ui_uart_heartbeat_state_t *out_state)
{
    int32_t start, error_code, need_make, remaining_s, total_make_s, maker_state;
    int32_t potb_temp, pota_temp, potb_igbt_temp, pota_igbt_temp;
    int32_t potb_dw, pota_dw, potb_remaining_heat_s, pota_remaining_heat_s;
    int32_t debug_bypass, motion_no_heat;
    int32_t reservation_active, reservation_qty, reservation_status, clock_valid;
    uint32_t reservation_epoch;

    if(line == NULL || out_state == NULL ||
       !ui_uart_get_int_field(line, "start", &start) ||
       !ui_uart_get_int_field(line, "err", &error_code) ||
       !ui_uart_get_int_field(line, "need", &need_make) ||
       !ui_uart_get_int_field(line, "remaining_s", &remaining_s) ||
       !ui_uart_get_int_field(line, "total_make_s", &total_make_s) ||
       !ui_uart_get_int_field(line, "maker_state", &maker_state) ||
       !ui_uart_get_int_field(line, "pot_b_temp_c", &potb_temp) ||
       !ui_uart_get_int_field(line, "pot_a_temp_c", &pota_temp) ||
       !ui_uart_get_int_field(line, "pot_b_igbt_c", &potb_igbt_temp) ||
       !ui_uart_get_int_field(line, "pot_a_igbt_c", &pota_igbt_temp) ||
       !ui_uart_get_int_field(line, "pot_b_dw", &potb_dw) ||
       !ui_uart_get_int_field(line, "pot_a_dw", &pota_dw) ||
       !ui_uart_get_int_field(line, "pot_b_heat_remaining_s", &potb_remaining_heat_s) ||
       !ui_uart_get_int_field(line, "pot_a_heat_remaining_s", &pota_remaining_heat_s) ||
       !ui_uart_get_int_field(line, "debug_bypass_mask", &debug_bypass) ||
       !ui_uart_get_int_field(line, "motion_no_heat", &motion_no_heat) ||
       !ui_uart_get_int_field(line, "reservation_active", &reservation_active) ||
       !ui_uart_get_uint_field(line, "reservation_epoch", &reservation_epoch) ||
       !ui_uart_get_int_field(line, "reservation_qty", &reservation_qty) ||
       !ui_uart_get_int_field(line, "reservation_status", &reservation_status) ||
       !ui_uart_get_int_field(line, "clock_valid", &clock_valid)) {
        return false;
    }

    memset(out_state, 0, sizeof(*out_state));
    out_state->online = true;
    out_state->remote_start = start != 0;
    out_state->remote_error_code = error_code;
    out_state->remote_need_make = need_make;
    out_state->remote_remaining_s = remaining_s;
    out_state->has_remote_remaining_s = true;
    out_state->remote_total_make_s = total_make_s;
    out_state->has_remote_total_make_s = true;
    out_state->remote_maker_state = maker_state;
    out_state->has_remote_maker_state = true;
    out_state->remote_potb_temp = potb_temp;
    out_state->remote_pota_temp = pota_temp;
    out_state->has_remote_pot_temps = true;
    out_state->remote_potb_igbt_temp = potb_igbt_temp;
    out_state->remote_pota_igbt_temp = pota_igbt_temp;
    out_state->has_remote_pot_igbt_temps = true;
    out_state->remote_potb_dw = potb_dw;
    out_state->remote_pota_dw = pota_dw;
    out_state->has_remote_pot_dw = true;
    out_state->remote_potb_remaining_heat_s = potb_remaining_heat_s;
    out_state->remote_pota_remaining_heat_s = pota_remaining_heat_s;
    out_state->has_remote_pot_remaining_heat_s = true;
    out_state->remote_debug_bypass_mask = (uint32_t)debug_bypass & 0x3FU;
    out_state->remote_debug_bypass = out_state->remote_debug_bypass_mask != 0U;
    out_state->has_remote_debug_bypass = true;
    out_state->remote_no_heat = motion_no_heat != 0;
    out_state->has_remote_no_heat = true;
    out_state->remote_reservation_active = reservation_active != 0;
    out_state->remote_reservation_epoch = reservation_epoch;
    out_state->remote_reservation_qty = reservation_qty < 0 ? 0U :
                                        (reservation_qty > 255 ? 255U : (uint8_t)reservation_qty);
    out_state->remote_reservation_status = reservation_status < 0 ? 0U :
                                           (reservation_status > 255 ? 255U : (uint8_t)reservation_status);
    out_state->remote_clock_valid = clock_valid != 0;
    out_state->has_remote_reservation = true;
    out_state->last_ok_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    return true;
}

static bool ui_uart_parse_config_response(const char *line, ui_uart_heartbeat_state_t *out_state)
{
    int32_t oil, doneness, oil_offset, doneness_offset, max_temp_c;
    int32_t power_offset, power_offset_first, power_offset_second;
    int32_t init_dw, laser_check, second_oil = 1, debug_bypass, motion_no_heat;

    if(line == NULL || out_state == NULL ||
       !ui_uart_get_int_field(line, "oil", &oil) ||
       !ui_uart_get_int_field(line, "doneness", &doneness) ||
       !ui_uart_get_int_field(line, "oil_offset", &oil_offset) ||
       !ui_uart_get_int_field(line, "doneness_offset", &doneness_offset) ||
       !ui_uart_get_int_field(line, "max_temp_c", &max_temp_c) ||
       !ui_uart_get_int_field(line, "init_dw", &init_dw) ||
       !ui_uart_get_int_field(line, "laser_check", &laser_check) ||
       !ui_uart_get_int_field(line, "debug_bypass_mask", &debug_bypass) ||
       !ui_uart_get_int_field(line, "motion_no_heat", &motion_no_heat)) {
        return false;
    }

    if(!ui_uart_get_int_field(line, "power_offset_first", &power_offset_first) ||
       !ui_uart_get_int_field(line, "power_offset_second", &power_offset_second)) {
        /* Backward compatibility with MainControl firmware exposing one shared value. */
        if(!ui_uart_get_int_field(line, "power_offset", &power_offset)) {
            return false;
        }
        power_offset_first = power_offset;
        power_offset_second = power_offset;
    }
    /* Older MainControl firmware predates this optional setting; keep the
     * feature enabled by default so the UI remains behavior-compatible. */
    if(!ui_uart_get_int_field(line, "second_oil", &second_oil)) {
        second_oil = 1;
    }

    memset(out_state, 0, sizeof(*out_state));
    out_state->online = true;
    out_state->remote_oil_level = oil;
    out_state->remote_doneness = doneness;
    out_state->has_remote_prefs = true;
    out_state->remote_olo = oil_offset;
    out_state->remote_dno = doneness_offset;
    out_state->remote_temp_adjust = max_temp_c;
    out_state->remote_power_offset_first = power_offset_first;
    out_state->remote_power_offset_second = power_offset_second;
    out_state->remote_dw_adjust = init_dw;
    out_state->remote_laser_check = laser_check != 0;
    out_state->remote_second_oil = second_oil != 0;
    out_state->has_remote_offsets = true;
    out_state->remote_debug_bypass_mask = (uint32_t)debug_bypass & 0x3FU;
    out_state->remote_debug_bypass = out_state->remote_debug_bypass_mask != 0U;
    out_state->has_remote_debug_bypass = true;
    out_state->remote_no_heat = motion_no_heat != 0;
    out_state->has_remote_no_heat = true;
    out_state->last_ok_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    return true;
}

static void ui_uart_record_formatted_line(const char *format, ...)
{
    char line[UI_UART_HISTORY_TEXT_MAX_LEN];
    va_list args;

    va_start(args, format);
    (void)vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    ui_uart_record_rx_line(line);
}

static void ui_uart_synthesize_response_line(const char *line, const char *operation)
{
    int32_t value[18];

    if(strcmp(operation, "error_log.get") == 0 && ui_uart_get_int_field(line, "count", &value[0])) {
        ui_uart_record_formatted_line("ELG %ld", (long)value[0]);
        return;
    }
    if(strcmp(operation, "pot.debug.get") != 0) {
        return;
    }

    static const char *keys[18] = {
        "pot_a_addr", "pot_a_temp_ad", "pot_a_temp_c", "pot_a_igbt_ad", "pot_a_igbt_c",
        "pot_a_voltage", "pot_a_current", "pot_a_ppg", "pot_a_state",
        "pot_b_addr", "pot_b_temp_ad", "pot_b_temp_c", "pot_b_igbt_ad", "pot_b_igbt_c",
        "pot_b_voltage", "pot_b_current", "pot_b_ppg", "pot_b_state"
    };
    for(size_t index = 0; index < 18U; index++) {
        if(!ui_uart_get_int_field(line, keys[index], &value[index])) {
            return;
        }
    }
    ui_uart_record_formatted_line(
        "PDBG %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld",
        (long)value[0], (long)value[1], (long)value[2], (long)value[3], (long)value[4],
        (long)value[5], (long)value[6], (long)value[7], (long)value[8], (long)value[9],
        (long)value[10], (long)value[11], (long)value[12], (long)value[13], (long)value[14],
        (long)value[15], (long)value[16], (long)value[17]);
}

static void ui_uart_synthesize_event_line(const char *line)
{
    const char *cursor = line;
    char prefix[8];
    char sequence[16];
    char event[UI_UART_OPERATION_MAX_LEN];
    char name[32];
    int32_t value, seq, code, boot_id, uptime_s;

    if(!ui_uart_next_token(&cursor, prefix, sizeof(prefix)) || strcmp(prefix, "EVT") != 0 ||
       !ui_uart_next_token(&cursor, sequence, sizeof(sequence)) ||
       !ui_uart_next_token(&cursor, event, sizeof(event))) {
        return;
    }
    if((strcmp(event, "debug.measure") == 0 || strcmp(event, "debug.status") == 0) &&
       ui_uart_get_field(line, "name", name, sizeof(name)) &&
       ui_uart_get_int_field(line, "value", &value)) {
        ui_uart_record_formatted_line(strcmp(event, "debug.measure") == 0 ? "MEAS %s %ld" : "STAT %s %ld",
                                      name, (long)value);
        return;
    }
    if(strcmp(event, "error_log.entry") == 0 &&
       ui_uart_get_int_field(line, "seq", &seq) &&
       ui_uart_get_int_field(line, "code", &code) &&
       ui_uart_get_int_field(line, "boot_id", &boot_id) &&
       ui_uart_get_int_field(line, "uptime_s", &uptime_s)) {
        ui_uart_record_formatted_line("ELE %ld %ld %ld %ld",
                                      (long)seq, (long)code, (long)boot_id, (long)uptime_s);
    }
}

static void ui_uart_publish_state_update(const ui_uart_heartbeat_state_t *state)
{
    bool changed_to_online;

    if(state == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_ui_uart_heartbeat_lock);
    changed_to_online = (!s_heartbeat_state.online && state->online);
    s_heartbeat_state = *state;
    s_pending_heartbeat_state = *state;
    s_has_pending_heartbeat_state = true;
    portEXIT_CRITICAL(&s_ui_uart_heartbeat_lock);

    if(changed_to_online) {
        ESP_LOGI(TAG, "peer heartbeat online");
    }
}

static void ui_uart_update_runtime_state(const ui_uart_heartbeat_state_t *state)
{
    ui_uart_heartbeat_state_t merged;

    if(state == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_ui_uart_heartbeat_lock);
    merged = s_heartbeat_state;
    merged.online = state->online;
    merged.remote_start = state->remote_start;
    merged.remote_error_code = state->remote_error_code;
    merged.remote_need_make = state->remote_need_make;
    merged.remote_remaining_s = state->remote_remaining_s;
    merged.has_remote_remaining_s = state->has_remote_remaining_s;
    merged.remote_total_make_s = state->remote_total_make_s;
    merged.has_remote_total_make_s = state->has_remote_total_make_s;
    if(state->has_remote_offsets) {
        merged.remote_olo = state->remote_olo;
        merged.remote_dno = state->remote_dno;
        merged.remote_temp_adjust = state->remote_temp_adjust;
        merged.remote_power_offset_first = state->remote_power_offset_first;
        merged.remote_power_offset_second = state->remote_power_offset_second;
        merged.remote_dw_adjust = state->remote_dw_adjust;
        merged.remote_laser_check = state->remote_laser_check;
        merged.remote_second_oil = state->remote_second_oil;
        merged.has_remote_offsets = true;
    }
    if(state->has_remote_debug_bypass) {
        merged.remote_debug_bypass = state->remote_debug_bypass;
        merged.remote_debug_bypass_mask = state->remote_debug_bypass_mask;
        merged.has_remote_debug_bypass = true;
    }
    if(state->has_remote_no_heat) {
        merged.remote_no_heat = state->remote_no_heat;
        merged.has_remote_no_heat = true;
    }
    if(state->has_remote_prefs) {
        merged.remote_oil_level = state->remote_oil_level;
        merged.remote_doneness = state->remote_doneness;
        merged.has_remote_prefs = true;
    }
    merged.remote_maker_state = state->remote_maker_state;
    merged.has_remote_maker_state = state->has_remote_maker_state;
    merged.remote_potb_temp = state->remote_potb_temp;
    merged.remote_pota_temp = state->remote_pota_temp;
    merged.has_remote_pot_temps = state->has_remote_pot_temps;
    merged.remote_potb_igbt_temp = state->remote_potb_igbt_temp;
    merged.remote_pota_igbt_temp = state->remote_pota_igbt_temp;
    merged.has_remote_pot_igbt_temps = state->has_remote_pot_igbt_temps;
    merged.remote_potb_dw = state->remote_potb_dw;
    merged.remote_pota_dw = state->remote_pota_dw;
    merged.has_remote_pot_dw = state->has_remote_pot_dw;
    merged.remote_potb_remaining_heat_s = state->remote_potb_remaining_heat_s;
    merged.remote_pota_remaining_heat_s = state->remote_pota_remaining_heat_s;
    merged.has_remote_pot_remaining_heat_s = state->has_remote_pot_remaining_heat_s;
    merged.remote_reservation_active = state->remote_reservation_active;
    merged.remote_reservation_epoch = state->remote_reservation_epoch;
    merged.remote_reservation_qty = state->remote_reservation_qty;
    merged.remote_reservation_status = state->remote_reservation_status;
    merged.remote_clock_valid = state->remote_clock_valid;
    merged.has_remote_reservation = state->has_remote_reservation;
    merged.last_ok_ms = state->last_ok_ms;
    portEXIT_CRITICAL(&s_ui_uart_heartbeat_lock);

    ui_uart_publish_state_update(&merged);
}

static void ui_uart_update_settings_state(const ui_uart_heartbeat_state_t *state)
{
    ui_uart_heartbeat_state_t merged;
    bool sync_done;

    if(state == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_ui_uart_heartbeat_lock);
    merged = s_heartbeat_state;
    merged.online = state->online;
    if(state->has_remote_offsets) {
        merged.remote_olo = state->remote_olo;
        merged.remote_dno = state->remote_dno;
        merged.remote_temp_adjust = state->remote_temp_adjust;
        merged.remote_power_offset_first = state->remote_power_offset_first;
        merged.remote_power_offset_second = state->remote_power_offset_second;
        merged.remote_dw_adjust = state->remote_dw_adjust;
        merged.remote_laser_check = state->remote_laser_check;
        merged.remote_second_oil = state->remote_second_oil;
        merged.has_remote_offsets = true;
    }
    if(state->has_remote_debug_bypass) {
        merged.remote_debug_bypass = state->remote_debug_bypass;
        merged.remote_debug_bypass_mask = state->remote_debug_bypass_mask;
        merged.has_remote_debug_bypass = true;
    }
    if(state->has_remote_no_heat) {
        merged.remote_no_heat = state->remote_no_heat;
        merged.has_remote_no_heat = true;
    }
    if(state->has_remote_prefs) {
        merged.remote_oil_level = state->remote_oil_level;
        merged.remote_doneness = state->remote_doneness;
        merged.has_remote_prefs = true;
    }
    merged.last_ok_ms = state->last_ok_ms;
    sync_done = state->has_remote_offsets && state->has_remote_prefs;
    s_settings_sync_done = sync_done;
    portEXIT_CRITICAL(&s_ui_uart_heartbeat_lock);

    ui_uart_publish_state_update(&merged);
}

static bool ui_uart_has_synced_settings(void)
{
    bool has_settings;

    portENTER_CRITICAL(&s_ui_uart_heartbeat_lock);
    has_settings = s_settings_sync_done;
    portEXIT_CRITICAL(&s_ui_uart_heartbeat_lock);

    return has_settings;
}

static bool ui_uart_sync_preferences_to_bound_state(const ui_uart_heartbeat_state_t *state)
{
    bool changed = false;

    if(state == NULL || s_bound_ui_data == NULL || !state->has_remote_prefs ||
       !ui_data_state_is_idle(&s_bound_ui_data->state)) {
        return false;
    }

    if(state->remote_oil_level >= 0 && state->remote_oil_level < (int32_t)UI_OIL_COUNT &&
       s_bound_ui_data->state.oil_mode != (ui_oil_mode_t)state->remote_oil_level) {
        s_bound_ui_data->state.oil_mode = (ui_oil_mode_t)state->remote_oil_level;
        changed = true;
    }
    if(state->remote_doneness >= 0 && state->remote_doneness < (int32_t)UI_DON_COUNT &&
       s_bound_ui_data->state.doneness != (ui_doneness_t)state->remote_doneness) {
        s_bound_ui_data->state.doneness = (ui_doneness_t)state->remote_doneness;
        changed = true;
    }

    return changed;
}

static void ui_uart_apply_heartbeat_to_ui(const ui_uart_heartbeat_state_t *state)
{
    bool changed;
    bool was_running;
    bool is_running;
    uint8_t remote_need_make;
    int32_t remote_remaining_s;
    int32_t remote_total_make_s;
    uint8_t pending_need_make;
    const ui_data_state_t *current_state;

    if(state == NULL || s_bound_ui_data == NULL) {
        return;
    }

    current_state = ui_data_get_state(s_bound_ui_data);
    was_running = current_state != NULL &&
                  (current_state->start ||
                   (current_state->maker_state >= UI_MAKER_PRE_HEATING &&
                    current_state->maker_state <= UI_MAKER_MAKING));

    remote_need_make = state->remote_need_make < 0 ? 0U :
                       (state->remote_need_make > 255 ? 255U : (uint8_t)state->remote_need_make);
    remote_remaining_s = state->has_remote_remaining_s ? state->remote_remaining_s : -1;
    remote_total_make_s = state->has_remote_total_make_s ? state->remote_total_make_s : -1;

    portENTER_CRITICAL(&s_ui_uart_ack_lock);
    pending_need_make = s_pending_need_make_count;
    portEXIT_CRITICAL(&s_ui_uart_ack_lock);

    if(!lvgl_port_lock(100)) {
        ESP_LOGW(TAG, "lvgl lock timeout while applying heartbeat");
        return;
    }

    if(pending_need_make > 0U) {
        /* ADD/SUB 命令尚未被 MainControl 确认，用本地值替换远端旧值，避免数字闪动 */
        const ui_data_state_t *cur = ui_data_get_state(s_bound_ui_data);
        if(cur != NULL) {
            remote_need_make = cur->needMake;
        }
    }

    changed = ui_data_apply_remote_state(s_bound_ui_data,
                                         state->remote_start,
                                         state->remote_error_code,
                                         remote_need_make,
                                         remote_remaining_s,
                                         remote_total_make_s,
                                         state->has_remote_maker_state ?
                                         state->remote_maker_state : -1);

    changed = ui_uart_sync_preferences_to_bound_state(state) || changed;

    current_state = ui_data_get_state(s_bound_ui_data);
    is_running = current_state != NULL &&
                 (current_state->start ||
                  (current_state->maker_state >= UI_MAKER_PRE_HEATING &&
                   current_state->maker_state <= UI_MAKER_MAKING));

    if(was_running && !is_running && current_state != NULL &&
       current_state->maker_state != UI_MAKER_STOPPING &&
       current_state->maker_state != UI_MAKER_STOPPED_NEEDS_RECOVERY &&
       !ui_data_state_has_alert(current_state) &&
       !s_heartbeat_interrupted_since_apply) {
        /*
         * 只有本次心跳与上次 apply 是连续在线的（中间未中断）才能认为
         * 是一次真实的“运行 -> 完成”转换。链路恢复后第一帧即使状态从
         * running -> idle 也不奏完成音，避免重启/烧录 MainControl 时误响。
         */
        buzzer_complete_async();
    }
    /* 本次成功 apply 后重置中断标志，后续若仍在线才能再次正常奏完成音 */
    s_heartbeat_interrupted_since_apply = false;

    if(changed) {
        ui_on_data_changed();
        ui_debug_on_data_changed();
        ui_debug_stress_test_on_data_changed();
        ui_settings_on_data_changed();
        ui_alert_on_data_changed();
        ui_notice_on_data_changed();
    }
    ui_on_debug_state_changed();

    lvgl_port_unlock();
}

static void ui_uart_apply_remote_preferences(const ui_uart_heartbeat_state_t *state)
{
    bool changed = false;

    if(state == NULL) {
        return;
    }

    if(!lvgl_port_lock(100)) {
        ESP_LOGW(TAG, "lvgl lock timeout while applying remote preferences");
        return;
    }

    changed = ui_uart_sync_preferences_to_bound_state(state);

    if(changed) {
        ui_on_data_changed();
        ui_debug_on_data_changed();
        ui_debug_stress_test_on_data_changed();
        ui_alert_on_data_changed();
        ui_notice_on_data_changed();
    }
    ui_on_debug_state_changed();
    ui_settings_on_remote_settings_synced();
    ui_settings_on_data_changed();

    lvgl_port_unlock();
}

static esp_err_t ui_uart_enqueue_message_ex(const char *command,
                                            ui_uart_response_t response,
                                            bool store_in_history,
                                            uint16_t timeout_ms,
                                            uint8_t max_attempts)
{
    ui_uart_message_t message;

    if(command == NULL || s_ui_uart_command_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if(!ui_uart_prepare_message(&message, command, response, store_in_history, timeout_ms, max_attempts)) {
        return ESP_ERR_INVALID_ARG;
    }

    return ui_uart_enqueue_to_queue(s_ui_uart_command_queue,
                                    "ui-uart command",
                                    &message,
                                    pdMS_TO_TICKS(UI_UART_USER_ENQUEUE_WAIT_MS));
}

static bool ui_uart_format_request(char *request, size_t request_size, const char *format, ...)
{
    va_list args;
    int len;

    if(request == NULL || request_size == 0U || format == NULL) {
        return false;
    }
    va_start(args, format);
    len = vsnprintf(request, request_size, format, args);
    va_end(args);
    return len >= 0 && (size_t)len < request_size;
}

static esp_err_t ui_uart_enqueue_poll_message(const char *command, ui_uart_response_t response)
{
    ui_uart_message_t message;

    if(command == NULL || s_ui_uart_heartbeat_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if(!ui_uart_prepare_message(&message,
                                command,
                                response,
                                true,
                                UI_UART_POLL_TIMEOUT_MS,
                                UI_UART_POLL_MAX_ATTEMPTS)) {
        return ESP_ERR_INVALID_ARG;
    }

    return ui_uart_enqueue_to_queue(s_ui_uart_heartbeat_queue,
                                    "ui-uart heartbeat",
                                    &message,
                                    0);
}

static esp_err_t ui_uart_enqueue_settings_sync_message(void)
{
    return ui_uart_enqueue_poll_message(UI_UART_SETTINGS_SYNC_COMMAND,
                                        UI_UART_RESPONSE_SETTINGS_SYNC);
}

static const char *ui_uart_config_key(const char *legacy_key, bool sun)
{
    if(strcmp(legacy_key, "OIL") == 0) return "oil";
    if(strcmp(legacy_key, "DON") == 0) return "doneness";
    if(strcmp(legacy_key, "OLO") == 0) return "oil_offset";
    if(strcmp(legacy_key, "DNO") == 0) return "doneness_offset";
    if(strcmp(legacy_key, "TMO") == 0) return "max_temp_c";
    if(strcmp(legacy_key, "PWO") == 0) return "power_offset";
    if(strcmp(legacy_key, "PWO1") == 0) return "power_offset_first";
    if(strcmp(legacy_key, "PWO2") == 0) return "power_offset_second";
    if(!sun && strcmp(legacy_key, "DWO") == 0) return "init_dw";
    if(!sun && strcmp(legacy_key, "LSR") == 0) return "laser_check";
    if(!sun && strcmp(legacy_key, "OIL2") == 0) return "second_oil";
    return NULL;
}

static bool ui_uart_translate_legacy_command(const char *command, char *request, size_t request_size)
{
    const char *cursor = command;
    const char *config_key;
    char token[16];
    char arg1[64];
    char arg2[64];
    char arg3[64];
    char extra[2];
    bool has_arg1;
    bool has_arg2;
    bool has_arg3;

    if(command == NULL || request == NULL || request_size == 0U ||
       !ui_uart_next_token(&cursor, token, sizeof(token))) {
        return false;
    }
    has_arg1 = ui_uart_next_token(&cursor, arg1, sizeof(arg1));
    has_arg2 = ui_uart_next_token(&cursor, arg2, sizeof(arg2));
    has_arg3 = ui_uart_next_token(&cursor, arg3, sizeof(arg3));
    if(ui_uart_next_token(&cursor, extra, sizeof(extra))) {
        return false;
    }

    config_key = ui_uart_config_key(token, false);
    if(config_key != NULL && has_arg1 && !has_arg2) {
        return ui_uart_format_request(request, request_size, "config.set key=%s value=%s", config_key, arg1);
    }
    if(strcmp(token, "DBG") == 0 && has_arg1) {
        if(strcmp(arg1, "GET") == 0 && !has_arg2) {
            return ui_uart_format_request(request, request_size, "debug.get");
        }
        if(has_arg2 && !has_arg3) {
            const char *key = strcmp(arg1, "BYPASS") == 0 ? "bypass" :
                              strcmp(arg1, "MASK") == 0 ? "mask" :
                              strcmp(arg1, "NOHEAT") == 0 ? "no_heat" :
                              strcmp(arg1, "ALL") == 0 ? "all" : NULL;
            return key != NULL && ui_uart_format_request(request, request_size,
                                                          "debug.set key=%s value=%s", key, arg2);
        }
        return false;
    }
    if(strcmp(token, "RST") == 0 && !has_arg1) {
        return ui_uart_format_request(request, request_size, "system.restart");
    }
    if(strcmp(token, "HET") == 0 && has_arg1 && !has_arg2) {
        const char *operation = strcmp(arg1, "0") == 0 ? "config.get" :
                                strcmp(arg1, "1") == 0 ? "state.get" :
                                strcmp(arg1, "2") == 0 ? "pot.debug.get" : NULL;
        return operation != NULL && ui_uart_format_request(request, request_size, "%s", operation);
    }
    if(strcmp(token, "ELG") == 0) {
        if(!has_arg1) return ui_uart_format_request(request, request_size, "error_log.get");
        if(strcmp(arg1, "C") == 0 && !has_arg2) {
            return ui_uart_format_request(request, request_size, "error_log.clear");
        }
        return false;
    }
    if(strcmp(token, "TST") == 0 && has_arg1) {
        if(!has_arg2) {
            return ui_uart_format_request(request, request_size, "debug.run action=%s", arg1);
        }
        if(!has_arg3) {
            return ui_uart_format_request(request, request_size, "debug.run action=%s arg1=%s", arg1, arg2);
        }
        return ui_uart_format_request(request, request_size,
                                      "debug.run action=%s arg1=%s arg2=%s", arg1, arg2, arg3);
    }
    if(strcmp(token, "STA") == 0 && has_arg1 && !has_arg2) {
        if(strcmp(arg1, "1") == 0) return ui_uart_format_request(request, request_size, "maker.start");
        if(strcmp(arg1, "0") == 0) return ui_uart_format_request(request, request_size, "maker.stop");
        return false;
    }
    if(strcmp(token, "REC") == 0 && has_arg1 && !has_arg2) {
        return ui_uart_format_request(request, request_size, "maker.recover");
    }
    if(strcmp(token, "CLR") == 0 && (!has_arg1 || !has_arg2)) {
        return ui_uart_format_request(request, request_size, "error.clear");
    }
    if((strcmp(token, "ADD") == 0 || strcmp(token, "SUB") == 0) && has_arg1 && !has_arg2) {
        return ui_uart_format_request(request, request_size, "%s value=%s",
                                      strcmp(token, "ADD") == 0 ? "quantity.add" : "quantity.sub", arg1);
    }
    if(strcmp(token, "SUN") == 0 && has_arg1) {
        if(strcmp(arg1, "GET") == 0 && !has_arg2) return ui_uart_format_request(request, request_size, "sun.state.get");
        if(strcmp(arg1, "CFG") == 0 && !has_arg2) return ui_uart_format_request(request, request_size, "sun.config.get");
        if(strcmp(arg1, "CLR") == 0 && !has_arg2) return ui_uart_format_request(request, request_size, "sun.clear");
        if(strcmp(arg1, "QTY") == 0 && has_arg2 && !has_arg3) {
            return ui_uart_format_request(request, request_size, "sun.quantity.set value=%s", arg2);
        }
        if(strcmp(arg1, "STA") == 0 && has_arg2 && !has_arg3) {
            if(strcmp(arg2, "1") == 0) return ui_uart_format_request(request, request_size, "sun.start");
            if(strcmp(arg2, "0") == 0) return ui_uart_format_request(request, request_size, "sun.stop");
            return false;
        }
        config_key = ui_uart_config_key(arg1, true);
        if(config_key != NULL && has_arg2 && !has_arg3) {
            return ui_uart_format_request(request, request_size,
                                          "sun.config.set key=%s value=%s", config_key, arg2);
        }
    }
    return false;
}


static esp_err_t ui_uart_enqueue_operation(const char *request, bool require_ack)
{
    return ui_uart_enqueue_message_ex(request,
                                      UI_UART_RESPONSE_COMMAND,
                                      true,
                                      UI_UART_ACK_TIMEOUT_MS,
                                      require_ack ? UI_UART_MAX_ATTEMPTS : 1U);
}

static esp_err_t ui_uart_enqueue_sts_message(const char *action, uint16_t timeout_ms)
{
    char command[UI_UART_MSG_MAX_LEN];
    int len;
    esp_err_t err;

    if(action == NULL || action[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    len = snprintf(command, sizeof(command), "debug.stress action=%s", action);
    if(len < 0 || (size_t)len >= sizeof(command)) {
        return ESP_ERR_INVALID_ARG;
    }

    err = ui_uart_enqueue_message_ex(command,
                                     UI_UART_RESPONSE_STS,
                                     true,
                                     timeout_ms,
                                     1);
    if(err == ESP_OK) {
        ui_uart_set_sts_result(action, UI_UART_STS_RESULT_PENDING);
    }

    return err;
}

static void ui_uart_need_make_done(void)
{
    portENTER_CRITICAL(&s_ui_uart_ack_lock);
    if(s_pending_need_make_count > 0U) {
        s_pending_need_make_count--;
    }
    portEXIT_CRITICAL(&s_ui_uart_ack_lock);
}

static void ui_uart_enqueue_value_command(const char *prefix, int32_t value, bool is_need_make_cmd)
{
    char command[UI_UART_MSG_MAX_LEN];
    ui_uart_message_t message;
    esp_err_t err;

    if(prefix == NULL || s_ui_uart_command_queue == NULL) {
        return;
    }

    if(!ui_uart_format_request(command, sizeof(command), "%s value=%ld", prefix, (long)value)) {
        return;
    }

    (void)ui_uart_prepare_message(&message, command, UI_UART_RESPONSE_COMMAND, true,
                                  UI_UART_ACK_TIMEOUT_MS, UI_UART_MAX_ATTEMPTS);
    message.is_need_make_cmd = is_need_make_cmd;

    if(is_need_make_cmd) {
        portENTER_CRITICAL(&s_ui_uart_ack_lock);
        s_pending_need_make_count++;
        portEXIT_CRITICAL(&s_ui_uart_ack_lock);
    }

    err = ui_uart_enqueue_to_queue(s_ui_uart_command_queue, "ui-uart command", &message,
                                   pdMS_TO_TICKS(UI_UART_USER_ENQUEUE_WAIT_MS));
    if(err != ESP_OK && is_need_make_cmd) {
        ui_uart_need_make_done();
    }
}

static void ui_uart_finish_message(const ui_uart_message_t *message)
{
    if(message != NULL && message->is_need_make_cmd) {
        ui_uart_need_make_done();
    }
}

static bool ui_uart_dequeue_message(ui_uart_message_t *message)
{
    if(message == NULL) {
        return false;
    }

    if(s_ui_uart_command_queue != NULL &&
       xQueueReceive(s_ui_uart_command_queue, message, 0) == pdTRUE) {
        return true;
    }

    if(s_ui_uart_heartbeat_queue != NULL &&
       xQueueReceive(s_ui_uart_heartbeat_queue, message, 0) == pdTRUE) {
        return true;
    }

    return false;
}

static void ui_uart_set_expected_response(const ui_uart_message_t *message)
{
    portENTER_CRITICAL(&s_ui_uart_ack_lock);
    if(message != NULL) {
        s_pending_response = message->response;
        s_expected_request_id = message->request_id;
        strlcpy(s_expected_operation, message->operation, sizeof(s_expected_operation));
        s_response_pending = true;
        s_pending_response_succeeded = false;
        if(message->response == UI_UART_RESPONSE_STS) {
            (void)ui_uart_message_action(message, s_expected_action, sizeof(s_expected_action));
        } else {
            s_expected_action[0] = '\0';
        }
    } else {
        s_expected_request_id = 0U;
        s_expected_operation[0] = '\0';
        s_expected_action[0] = '\0';
        s_response_pending = false;
        s_pending_response_succeeded = false;
    }
    portEXIT_CRITICAL(&s_ui_uart_ack_lock);
}

static bool ui_uart_pending_response_succeeded(void)
{
    bool succeeded;
    portENTER_CRITICAL(&s_ui_uart_ack_lock);
    succeeded = s_pending_response_succeeded;
    portEXIT_CRITICAL(&s_ui_uart_ack_lock);
    return succeeded;
}

static bool ui_uart_can_schedule_heartbeat(void)
{
    bool waiting;
    bool has_user_commands;

    portENTER_CRITICAL(&s_ui_uart_ack_lock);
    waiting = s_response_pending;
    portEXIT_CRITICAL(&s_ui_uart_ack_lock);
    has_user_commands = (s_ui_uart_command_queue != NULL &&
                         uxQueueMessagesWaiting(s_ui_uart_command_queue) > 0U);
    return !waiting &&
           !has_user_commands &&
           s_ui_uart_heartbeat_queue != NULL &&
           uxQueueMessagesWaiting(s_ui_uart_heartbeat_queue) == 0U;
}

static bool ui_uart_try_match_expected_response(const char *line)
{
    ui_uart_response_t pending_response;
    uint32_t expected_id;
    uint32_t response_id;
    char expected_operation[UI_UART_OPERATION_MAX_LEN];
    char expected_action[32];
    char response_operation[UI_UART_OPERATION_MAX_LEN];
    char result[16];
    bool pending;
    bool succeeded;

    if(line == NULL) {
        return false;
    }

    if(!ui_uart_parse_response(line, &response_id, response_operation, sizeof(response_operation),
                               result, sizeof(result))) {
        return false;
    }

    portENTER_CRITICAL(&s_ui_uart_ack_lock);
    pending = s_response_pending;
    pending_response = s_pending_response;
    expected_id = s_expected_request_id;
    strlcpy(expected_operation, s_expected_operation, sizeof(expected_operation));
    strlcpy(expected_action, s_expected_action, sizeof(expected_action));
    portEXIT_CRITICAL(&s_ui_uart_ack_lock);

    if(!pending || response_id != expected_id || strcmp(response_operation, expected_operation) != 0) {
        return false;
    }
    succeeded = ui_uart_response_succeeded(result);
    ui_uart_record_rx_line(line);
    ui_uart_synthesize_response_line(line, response_operation);

    if(pending_response == UI_UART_RESPONSE_SETTINGS_SYNC) {
        ui_uart_heartbeat_state_t state;

        if(succeeded && ui_uart_parse_config_response(line, &state)) {
            ESP_LOGI(TAG, "sync rx settings: %s", line);
            ui_uart_update_settings_state(&state);
        } else if(succeeded) {
            ESP_LOGW(TAG, "invalid config.get response: %s", line);
            succeeded = false;
        }
    } else if(pending_response == UI_UART_RESPONSE_HEARTBEAT) {
        ui_uart_heartbeat_state_t state;

        if(succeeded && ui_uart_parse_state_response(line, &state)) {
            ESP_LOGI(TAG, "poll rx state: %s", line);
            ui_uart_update_runtime_state(&state);
        } else if(succeeded) {
            ESP_LOGW(TAG, "invalid state.get response: %s", line);
            succeeded = false;
        }
    } else if(pending_response == UI_UART_RESPONSE_STS) {
        ui_uart_set_sts_result(expected_action,
                               succeeded ? UI_UART_STS_RESULT_SUCCESS : UI_UART_STS_RESULT_FAILED);
    }

    portENTER_CRITICAL(&s_ui_uart_ack_lock);
    s_pending_response_succeeded = succeeded;
    portEXIT_CRITICAL(&s_ui_uart_ack_lock);
    if(s_ui_uart_task != NULL) {
        xTaskNotifyGive(s_ui_uart_task);
    }
    return true;
}

static void ui_uart_handle_rx_line(char *line)
{
    size_t start = 0;
    size_t end;

    if(line == NULL) {
        return;
    }

    end = strlen(line);
    while(start < end && (line[start] == ' ' || line[start] == '\t')) {
        start++;
    }
    while(end > start && (line[end - 1] == ' ' || line[end - 1] == '\t')) {
        end--;
    }

    if(start > 0 || end < strlen(line)) {
        memmove(line, &line[start], end - start);
        line[end - start] = '\0';
    }

    if(line[0] == '\0') {
        return;
    }

    if(ui_uart_try_match_expected_response(line)) {
        return;
    }

    ui_uart_record_rx_line(line);
    ui_uart_synthesize_event_line(line);
    ESP_LOGI(TAG, "uart rx: %s", line);
}

static void ui_uart_rx_callback(const uint8_t *data, size_t len, void *user_data)
{
    (void)user_data;

    if(data == NULL || len == 0U) {
        return;
    }

    for(size_t i = 0; i < len; i++) {
        char ch = (char)data[i];

        if(ch == '\r' || ch == '\n') {
            if(s_rx_line_dropping) {
                s_rx_line_dropping = false;
                s_rx_line_len = 0U;
                continue;
            }

            if(s_rx_line_len > 0U) {
                s_rx_line[s_rx_line_len] = '\0';
                ui_uart_handle_rx_line(s_rx_line);
                s_rx_line_len = 0U;
            }
            continue;
        }

        if(s_rx_line_dropping) {
            continue;
        }

        if(s_rx_line_len + 1U >= sizeof(s_rx_line)) {
            ESP_LOGW(TAG, "uart rx line too long, drop");
            s_rx_line_dropping = true;
            s_rx_line_len = 0U;
            continue;
        }

        s_rx_line[s_rx_line_len++] = ch;
    }
}

static void ui_uart_task(void *arg)
{
    ui_uart_message_t message;
    char frame[UI_UART_TEXT_MAX_LEN];
    QueueSetMemberHandle_t ready_member;
    esp_err_t err;
    bool responded;
    bool response_succeeded;

    (void)arg;

    while(1) {
        ready_member = xQueueSelectFromSet(s_ui_uart_queue_set, portMAX_DELAY);
        if(ready_member == NULL) {
            continue;
        }

        if(!ui_uart_dequeue_message(&message)) {
            continue;
        }

        message.request_id = ui_uart_allocate_request_id();
        if(!ui_uart_format_request(frame, sizeof(frame), "REQ %lu %s",
                                   (unsigned long)message.request_id, message.command)) {
            ESP_LOGW(TAG, "uart request frame too long: %s", message.operation);
            ui_uart_finish_message(&message);
            continue;
        }

        responded = false;
        response_succeeded = false;

        for(uint8_t attempt = 1; attempt <= message.max_attempts; attempt++) {
            (void)ulTaskNotifyTake(pdTRUE, 0);
            ui_uart_set_expected_response(&message);

            if(message.store_in_history) {
                ui_uart_store_last_tx(frame);
            }

            if(message.response == UI_UART_RESPONSE_HEARTBEAT) {
                ESP_LOGI(TAG, "poll tx: %s", message.command);
            } else if(message.response == UI_UART_RESPONSE_SETTINGS_SYNC) {
                ESP_LOGI(TAG, "sync tx settings: %s", message.command);
            } else if(message.response == UI_UART_RESPONSE_STS) {
                ESP_LOGI(TAG, "sts tx: %s", message.command);
            } else if(attempt == 1U) {
                ESP_LOGI(TAG, "cmd tx: %s", message.command);
            } else {
                ESP_LOGI(TAG, "cmd tx retry %u/%u: %s",
                         (unsigned)attempt, (unsigned)message.max_attempts, message.command);
            }

            err = uart_link_writef("%s\r\n", frame);
            if(err != ESP_OK) {
                ESP_LOGW(TAG, "uart send failed: %s", esp_err_to_name(err));
                ui_uart_set_expected_response(NULL);
                continue;
            }

            if(ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(message.timeout_ms)) > 0U) {
                responded = true;
                response_succeeded = ui_uart_pending_response_succeeded();
                if(response_succeeded) {
                    ui_uart_cache_settings_from_message(&message);
                } else {
                    ESP_LOGW(TAG, "command rejected or failed: %s", message.command);
                }
                break;
            }

            ui_uart_set_expected_response(NULL);
            if(message.response == UI_UART_RESPONSE_HEARTBEAT) {
                ESP_LOGW(TAG, "poll timeout: %s", message.command);
                ui_uart_set_settings_sync_done(false);
                ui_uart_set_heartbeat_online(false);
            } else if(message.response == UI_UART_RESPONSE_SETTINGS_SYNC) {
                ESP_LOGW(TAG, "settings sync timeout: %s", message.command);
                ui_uart_set_settings_sync_done(false);
                ui_uart_set_heartbeat_online(false);
            } else if(message.response == UI_UART_RESPONSE_STS) {
                char action[32];

                if(ui_uart_message_action(&message, action, sizeof(action))) {
                    ui_uart_set_sts_result(action, UI_UART_STS_RESULT_TIMEOUT);
                }
                ESP_LOGW(TAG, "sts timeout: %s", message.command);
            } else {
                ESP_LOGW(TAG, "ack timeout for '%s' (%u/%u)",
                         message.command,
                         (unsigned)attempt,
                         (unsigned)message.max_attempts);
            }
        }

        ui_uart_set_expected_response(NULL);
        if(responded && response_succeeded && message.response == UI_UART_RESPONSE_HEARTBEAT) {
            ui_uart_heartbeat_state_t heartbeat_state;

            if(ui_uart_consume_pending_heartbeat_state(&heartbeat_state)) {
                ui_uart_apply_heartbeat_to_ui(&heartbeat_state);
            }
        }
        if(responded && response_succeeded && message.response == UI_UART_RESPONSE_SETTINGS_SYNC) {
            ui_uart_heartbeat_state_t heartbeat_state;

            if(ui_uart_consume_pending_heartbeat_state(&heartbeat_state)) {
                ui_uart_apply_remote_preferences(&heartbeat_state);
            }
        }
        if((!responded || !response_succeeded) && message.response == UI_UART_RESPONSE_HEARTBEAT) {
            if(s_bound_ui_data != NULL && lvgl_port_lock(100)) {
                const ui_data_state_t *state = ui_data_get_state(s_bound_ui_data);
                bool changed = state != NULL && state->start &&
                               ui_data_set_remote_time_authoritative(s_bound_ui_data, true);
                if(changed) {
                    ui_on_data_changed();
                    ui_debug_on_data_changed();
                    ui_debug_stress_test_on_data_changed();
                    ui_settings_on_data_changed();
                    ui_alert_on_data_changed();
                    ui_notice_on_data_changed();
                }
                lvgl_port_unlock();
            }
            ui_uart_set_heartbeat_online(false);
        }
        if(!responded && message.response != UI_UART_RESPONSE_HEARTBEAT) {
            ESP_LOGW(TAG, "command not acknowledged: %s", message.command);
        }
        ui_uart_finish_message(&message);
    }
}

static void ui_uart_heartbeat_task(void *arg)
{
    char heartbeat_command[UI_UART_HEARTBEAT_COMMAND_MAX_LEN];
    (void)arg;

    while(1) {
        if(ui_uart_can_schedule_heartbeat()) {
            if(ui_uart_has_synced_settings()) {
                time_t now = time(NULL);
                if(now >= (time_t)1704067200) {
                    (void)ui_uart_format_request(heartbeat_command, sizeof(heartbeat_command),
                                                 "state.get epoch=%lu", (unsigned long)now);
                } else {
                    strlcpy(heartbeat_command, UI_UART_HEARTBEAT_COMMAND,
                            sizeof(heartbeat_command));
                }
                (void)ui_uart_enqueue_poll_message(heartbeat_command,
                                                  UI_UART_RESPONSE_HEARTBEAT);
            } else {
                (void)ui_uart_enqueue_poll_message(UI_UART_SETTINGS_SYNC_COMMAND,
                                                  UI_UART_RESPONSE_SETTINGS_SYNC);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(UI_UART_HEARTBEAT_INTERVAL_MS));
    }
}

esp_err_t ui_uart_bridge_init(void)
{
    BaseType_t task_ok;
    BaseType_t heartbeat_task_ok;

    if(s_ui_uart_command_queue != NULL && s_ui_uart_heartbeat_queue != NULL) {
        return ESP_OK;
    }

    s_rx_line_len = 0U;
    s_pending_need_make_count = 0U;
    s_rx_line_dropping = false;
    s_settings_sync_done = false;
    s_next_request_id = 1U;
    memset(&s_last_sts_result, 0, sizeof(s_last_sts_result));
    s_last_sts_result.state = UI_UART_STS_RESULT_IDLE;
    memset(&s_heartbeat_state, 0, sizeof(s_heartbeat_state));
    ui_uart_set_expected_response(NULL);
    ui_uart_store_last_tx(NULL);
    ui_uart_store_last_rx(NULL);
    ui_uart_clear_rx_history();
    uart_link_set_rx_callback(ui_uart_rx_callback, NULL);

    s_ui_uart_queue_set = xQueueCreateSet(UI_UART_COMMAND_QUEUE_LEN + UI_UART_HEARTBEAT_QUEUE_LEN);
    if(s_ui_uart_queue_set == NULL) {
        ESP_LOGE(TAG, "ui uart queue set create failed");
        uart_link_set_rx_callback(NULL, NULL);
        return ESP_ERR_NO_MEM;
    }

    s_ui_uart_command_queue = xQueueCreate(UI_UART_COMMAND_QUEUE_LEN, sizeof(ui_uart_message_t));
    if(s_ui_uart_command_queue == NULL) {
        vQueueDelete(s_ui_uart_queue_set);
        s_ui_uart_queue_set = NULL;
        ESP_LOGE(TAG, "ui uart command queue create failed");
        uart_link_set_rx_callback(NULL, NULL);
        return ESP_ERR_NO_MEM;
    }

    s_ui_uart_heartbeat_queue = xQueueCreate(UI_UART_HEARTBEAT_QUEUE_LEN, sizeof(ui_uart_message_t));
    if(s_ui_uart_heartbeat_queue == NULL) {
        vQueueDelete(s_ui_uart_command_queue);
        s_ui_uart_command_queue = NULL;
        vQueueDelete(s_ui_uart_queue_set);
        s_ui_uart_queue_set = NULL;
        ESP_LOGE(TAG, "ui uart heartbeat queue create failed");
        uart_link_set_rx_callback(NULL, NULL);
        return ESP_ERR_NO_MEM;
    }

    if(xQueueAddToSet(s_ui_uart_command_queue, s_ui_uart_queue_set) != pdPASS ||
       xQueueAddToSet(s_ui_uart_heartbeat_queue, s_ui_uart_queue_set) != pdPASS) {
        vQueueDelete(s_ui_uart_heartbeat_queue);
        s_ui_uart_heartbeat_queue = NULL;
        vQueueDelete(s_ui_uart_command_queue);
        s_ui_uart_command_queue = NULL;
        vQueueDelete(s_ui_uart_queue_set);
        s_ui_uart_queue_set = NULL;
        uart_link_set_rx_callback(NULL, NULL);
        ESP_LOGE(TAG, "ui uart queue set bind failed");
        return ESP_ERR_NO_MEM;
    }

    task_ok = xTaskCreate(ui_uart_task,
                          "ui_uart",
                          UI_UART_TASK_STACK,
                          NULL,
                          UI_UART_TASK_PRIO,
                          &s_ui_uart_task);
    if(task_ok != pdPASS) {
        vQueueDelete(s_ui_uart_heartbeat_queue);
        s_ui_uart_heartbeat_queue = NULL;
        vQueueDelete(s_ui_uart_command_queue);
        s_ui_uart_command_queue = NULL;
        vQueueDelete(s_ui_uart_queue_set);
        s_ui_uart_queue_set = NULL;
        s_ui_uart_task = NULL;
        uart_link_set_rx_callback(NULL, NULL);
        ESP_LOGE(TAG, "ui uart task create failed");
        return ESP_ERR_NO_MEM;
    }

    heartbeat_task_ok = xTaskCreate(ui_uart_heartbeat_task,
                                    "ui_uart_hb",
                                    UI_UART_HEARTBEAT_TASK_STACK,
                                    NULL,
                                    UI_UART_TASK_PRIO,
                                    &s_ui_uart_heartbeat_task);
    if(heartbeat_task_ok != pdPASS) {
        vTaskDelete(s_ui_uart_task);
        s_ui_uart_task = NULL;
        vQueueDelete(s_ui_uart_heartbeat_queue);
        s_ui_uart_heartbeat_queue = NULL;
        vQueueDelete(s_ui_uart_command_queue);
        s_ui_uart_command_queue = NULL;
        vQueueDelete(s_ui_uart_queue_set);
        s_ui_uart_queue_set = NULL;
        uart_link_set_rx_callback(NULL, NULL);
        ESP_LOGE(TAG, "ui uart heartbeat task create failed");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void ui_uart_bridge_bind_ui_data(ui_data_t *data)
{
    s_bound_ui_data = data;
}

esp_err_t ui_uart_bridge_request_settings_sync(void)
{
    return ui_uart_enqueue_settings_sync_message();
}

esp_err_t ui_uart_bridge_send_raw_command(const char *command, bool require_ack)
{
    char request[UI_UART_MSG_MAX_LEN];

    if(!ui_uart_translate_legacy_command(command, request, sizeof(request))) {
        ESP_LOGW(TAG, "unsupported legacy uart command: %s", command != NULL ? command : "(null)");
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ui_uart_enqueue_operation(request, require_ack);
}

esp_err_t ui_uart_bridge_clear_need_make(uint8_t count)
{
    char command[UI_UART_MSG_MAX_LEN];

    if(count == 0U) {
        return ESP_OK;
    }

    if(!ui_uart_format_request(command, sizeof(command), "quantity.sub value=%u", (unsigned)count)) {
        return ESP_ERR_INVALID_ARG;
    }

    return ui_uart_enqueue_operation(command, true);
}

esp_err_t ui_uart_bridge_set_reservation(uint32_t epoch,
                                         uint8_t quantity,
                                         ui_oil_mode_t oil_mode,
                                         ui_doneness_t doneness)
{
    char command[UI_UART_MSG_MAX_LEN];

    if(epoch < 1704067200UL || quantity < 1U ||
       quantity > ui_data_max_make_quantity(doneness) ||
       oil_mode < UI_OIL_LIGHT || oil_mode >= UI_OIL_COUNT ||
       doneness < UI_DON_WELL || doneness >= UI_DON_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if(!ui_uart_format_request(command, sizeof(command),
                               "reservation.set epoch=%lu qty=%u oil=%u doneness=%u",
                               (unsigned long)epoch, (unsigned)quantity,
                               (unsigned)oil_mode, (unsigned)doneness)) {
        return ESP_ERR_INVALID_ARG;
    }
    return ui_uart_enqueue_operation(command, true);
}

esp_err_t ui_uart_bridge_cancel_reservation(void)
{
    return ui_uart_enqueue_operation("reservation.cancel", true);
}
esp_err_t ui_uart_bridge_send_sts_command(const char *action, uint16_t timeout_ms)
{
    return ui_uart_enqueue_sts_message(action, timeout_ms);
}

void ui_uart_bridge_clear_last_rx(void)
{
    ui_uart_store_last_rx(NULL);
}

void ui_uart_bridge_get_last_messages(char *tx, size_t tx_size, char *rx, size_t rx_size)
{
    portENTER_CRITICAL(&s_ui_uart_text_lock);
    if(tx != NULL && tx_size > 0U) {
        strlcpy(tx, s_last_tx, tx_size);
    }
    if(rx != NULL && rx_size > 0U) {
        strlcpy(rx, s_last_rx, rx_size);
    }
    portEXIT_CRITICAL(&s_ui_uart_text_lock);
}

void ui_uart_bridge_get_sts_result(ui_uart_sts_result_t *result)
{
    if(result == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_ui_uart_ack_lock);
    *result = s_last_sts_result;
    portEXIT_CRITICAL(&s_ui_uart_ack_lock);
}

uint32_t ui_uart_bridge_get_rx_history_cursor(void)
{
    uint32_t cursor;

    portENTER_CRITICAL(&s_ui_uart_text_lock);
    cursor = s_rx_history_latest_seq;
    portEXIT_CRITICAL(&s_ui_uart_text_lock);

    return cursor;
}

bool ui_uart_bridge_get_next_rx_line(uint32_t *cursor, char *rx, size_t rx_size)
{
    uint32_t latest_seq;
    uint32_t next_seq;
    uint32_t oldest_seq;
    uint32_t slot;
    bool has_line = false;

    if(cursor == NULL || rx == NULL || rx_size == 0U) {
        return false;
    }

    rx[0] = '\0';

    portENTER_CRITICAL(&s_ui_uart_text_lock);
    latest_seq = s_rx_history_latest_seq;
    if(*cursor < latest_seq) {
        oldest_seq = (latest_seq >= UI_UART_RX_HISTORY_LEN) ?
                     (latest_seq - UI_UART_RX_HISTORY_LEN + 1U) :
                     1U;
        next_seq = *cursor + 1U;
        if(next_seq < oldest_seq) {
            next_seq = oldest_seq;
        }

        slot = (next_seq - 1U) % UI_UART_RX_HISTORY_LEN;
        if(s_rx_history[slot].seq == next_seq) {
            strlcpy(rx, s_rx_history[slot].text, rx_size);
            *cursor = next_seq;
            has_line = true;
        } else {
            *cursor = latest_seq;
        }
    }
    portEXIT_CRITICAL(&s_ui_uart_text_lock);

    return has_line;
}

void ui_uart_bridge_get_heartbeat_state(ui_uart_heartbeat_state_t *state)
{
    if(state == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_ui_uart_heartbeat_lock);
    *state = s_heartbeat_state;
    portEXIT_CRITICAL(&s_ui_uart_heartbeat_lock);
}

void ui_uart_bridge_handle_event(ui_data_event_t event, const ui_data_state_t *state, void *user_data)
{
    (void)user_data;

    if(state == NULL) {
        return;
    }

    switch(event) {
    case UI_DATA_EVENT_OIL_CHANGED:
    {
        char request[UI_UART_MSG_MAX_LEN];
        if(ui_uart_format_request(request, sizeof(request), "config.set key=oil value=%ld",
                                  (long)state->oil_mode)) {
            (void)ui_uart_enqueue_operation(request, true);
        }
        if(state->doneness == UI_DON_SUNNY &&
           ui_uart_format_request(request, sizeof(request),
                                  "sun.config.set key=oil value=%ld",
                                  (long)state->oil_mode)) {
            (void)ui_uart_enqueue_operation(request, true);
        }
        break;
    }

    case UI_DATA_EVENT_DONENESS_CHANGED:
    {
        char request[UI_UART_MSG_MAX_LEN];
        if(ui_uart_format_request(request, sizeof(request), "config.set key=doneness value=%ld",
                                  (long)state->doneness)) {
            (void)ui_uart_enqueue_operation(request, true);
        }
        break;
    }

    case UI_DATA_EVENT_NEED_MAKE_SET:
        ui_uart_enqueue_value_command(state->doneness == UI_DON_SUNNY ?
                                      "sun.quantity.set" : "quantity.set",
                                      state->needMake, true);
        break;

    case UI_DATA_EVENT_NEED_MAKE_INCREASED:
        if(state->doneness == UI_DON_SUNNY) {
            ui_uart_enqueue_value_command("sun.quantity.set", state->needMake, true);
        } else {
            ui_uart_enqueue_value_command("quantity.add", 1, true);
        }
        break;

    case UI_DATA_EVENT_NEED_MAKE_DECREASED:
        if(state->doneness == UI_DON_SUNNY) {
            ui_uart_enqueue_value_command("sun.quantity.set", state->needMake, true);
        } else {
            ui_uart_enqueue_value_command("quantity.sub", 1, true);
        }
        break;

    case UI_DATA_EVENT_START:
        if(state->doneness == UI_DON_SUNNY) {
            char request[UI_UART_MSG_MAX_LEN];
            if(ui_uart_format_request(request, sizeof(request),
                                      "sun.config.set key=oil value=%ld",
                                      (long)state->oil_mode)) {
                (void)ui_uart_enqueue_operation(request, true);
            }
            ui_uart_enqueue_value_command("sun.quantity.set", state->needMake, true);
            (void)ui_uart_enqueue_operation("sun.start", true);
        } else {
            (void)ui_uart_enqueue_operation("maker.start", true);
        }
        break;

    case UI_DATA_EVENT_CANCEL:
        (void)ui_uart_enqueue_operation(state->doneness == UI_DON_SUNNY ?
                                        "sun.stop" : "maker.stop", true);
        break;

    case UI_DATA_EVENT_ERROR_CLEARED:
        (void)ui_uart_enqueue_operation(state->doneness == UI_DON_SUNNY ?
                                        "sun.clear" : "error.clear", true);
        break;

    case UI_DATA_EVENT_FINISHED:
        buzzer_complete_async();
        break;

    case UI_DATA_EVENT_PROGRESS:
    case UI_DATA_EVENT_RESET:
        break;
    }
}
