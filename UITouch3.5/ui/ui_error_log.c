#include "ui_error_log.h"

#include "buzzer.h"
#include "ui_alert.h"
#include "ui_common.h"
#include "ui_debug.h"
#include "ui_debug_internal.h"
#include "ui_i18n.h"
#include "ui_uart_bridge.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 最多展示的历史错误条数，与主控 ErrorLogDataManager::MAX_ENTRIES 保持一致。 */
#define UI_ERROR_LOG_CAPACITY 20
#define UI_ERROR_LOG_POLL_MS  120

typedef struct {
    uint32_t seq;
    uint32_t code;
    uint32_t bootId;
    uint32_t uptimeSec;
} ui_error_log_entry_t;

/* 从主控拉取的记录，按接收顺序(主控已按时间倒序发送)存放。 */
static ui_error_log_entry_t s_entries[UI_ERROR_LOG_CAPACITY];
static uint32_t s_count;          /* 当前已填充条数 */

static ui_data_t *s_data;
static lv_obj_t *s_list;
static lv_timer_t *s_poll_timer;
static uint32_t s_rx_cursor;

static void ui_error_log_render(void);

static void ui_error_log_request(void)
{
    s_count = 0U;
    /* 从当前 RX 位置开始接收应答，避免解析到历史行。 */
    s_rx_cursor = ui_uart_bridge_get_rx_history_cursor();
    (void)ui_uart_bridge_send_raw_command("ELG", false);
}

/* 解析主控回传的 ELG/ELE 行，成功消费返回 true。 */
static bool ui_error_log_handle_rx_line(const char *rx)
{
    char prefix[8];
    unsigned int count;
    unsigned int seq, code, boot, uptime;
    int n;

    if(rx == NULL || rx[0] == '\0') {
        return false;
    }

    /* 头帧: ELG <count> */
    n = sscanf(rx, " %7s %u", prefix, &count);
    if(n == 2 && strcmp(prefix, "ELG") == 0) {
        (void)count;
        s_count = 0U;
        ui_error_log_render();
        return true;
    }

    /* 条目: ELE <seq> <code> <bootId> <uptimeSec> */
    n = sscanf(rx, " %7s %u %u %u %u", prefix, &seq, &code, &boot, &uptime);
    if(n == 5 && strcmp(prefix, "ELE") == 0) {
        if(s_count < UI_ERROR_LOG_CAPACITY) {
            s_entries[s_count].seq = seq;
            s_entries[s_count].code = code;
            s_entries[s_count].bootId = boot;
            s_entries[s_count].uptimeSec = uptime;
            s_count++;
            ui_error_log_render();
        }
        return true;
    }

    return false;
}

static void ui_error_log_poll_cb(lv_timer_t *timer)
{
    char rx[UI_DEBUG_RX_LINE_MAX];

    LV_UNUSED(timer);

    while(ui_uart_bridge_get_next_rx_line(&s_rx_cursor, rx, sizeof(rx))) {
        (void)ui_error_log_handle_rx_line(rx);
    }
}

static void ui_error_log_render(void)
{
    if(s_list == NULL) {
        return;
    }

    lv_obj_clean(s_list);

    if(s_count == 0U) {
        lv_obj_t *empty = lv_label_create(s_list);
        lv_label_set_text(empty, ui_t(UI_STR_ERROR_LOG_EMPTY));
        lv_obj_set_style_text_font(empty, &ui_alert_font_16, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(UI_C_TEXT_DIM), 0);
        return;
    }

    /* s_entries 已按时间倒序排列(最近一条在最前)。 */
    for(uint32_t i = 0; i < s_count; i++) {
        const ui_error_log_entry_t *entry = &s_entries[i];
        char text[96];

        snprintf(text, sizeof(text), "%02u. E%02u %s  [%us]",
                 (unsigned)entry->seq, (unsigned)entry->code,
                 ui_alert_error_headline((ui_error_code_t)entry->code),
                 (unsigned)entry->uptimeSec);

        lv_obj_t *row = lv_label_create(s_list);
        lv_label_set_text(row, text);
        lv_label_set_long_mode(row, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_style_text_font(row, &ui_alert_font_16, 0);
        lv_obj_set_style_text_color(row, lv_color_hex(UI_C_TEXT_STRONG), 0);
    }
}

static void ui_error_log_stop(void)
{
    if(s_poll_timer != NULL) {
        lv_timer_delete(s_poll_timer);
        s_poll_timer = NULL;
    }
    s_list = NULL;
}

static void ui_error_log_back_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    ui_error_log_stop();
    if(s_data != NULL) {
        ui_debug_create(s_data);
    }
}

static void ui_error_log_clear_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    buzzer_beep_async(50);
    /* 通知主控清空历史，再重新拉取以刷新展示。 */
    (void)ui_uart_bridge_send_raw_command("ELG C", false);
    ui_error_log_request();
}

void ui_error_log_create(ui_data_t *data)
{
    lv_obj_t *screen;
    lv_obj_t *button;
    lv_obj_t *label;
    lv_obj_t *title;

    if(data != NULL) {
        s_data = data;
    }

    /* 离开 debug 页时停止其串口轮询，避免回调访问已释放的控件。 */
    ui_debug_stop_serial_timer();
    ui_error_log_stop();

    screen = lv_screen_active();
    ui_screen_reset(screen);

    /* 返回按钮 */
    button = lv_button_create(screen);
    ui_reset_base_obj(button);
    lv_obj_set_size(button, 58, 26);
    lv_obj_set_style_radius(button, 13, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_C_PANEL), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_align(button, LV_ALIGN_TOP_LEFT, 8, 6);
    lv_obj_add_event_cb(button, ui_error_log_back_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(button);
    lv_label_set_text(label, ui_t(UI_STR_BACK));
    lv_obj_set_style_text_font(label, &ui_alert_font_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_center(label);

    /* 标题 */
    title = lv_label_create(screen);
    lv_label_set_text(title, ui_t(UI_STR_ERROR_LOG_TITLE));
    lv_obj_set_style_text_font(title, &ui_alert_font_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* 清空按钮 */
    button = lv_button_create(screen);
    ui_reset_base_obj(button);
    lv_obj_set_size(button, 58, 26);
    lv_obj_set_style_radius(button, 13, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_C_PANEL), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_align(button, LV_ALIGN_TOP_RIGHT, -8, 6);
    lv_obj_add_event_cb(button, ui_error_log_clear_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(button);
    lv_label_set_text(label, ui_t(UI_STR_ERROR_LOG_CLEAR));
    lv_obj_set_style_text_font(label, &ui_alert_font_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_C_TEXT_STRONG), 0);
    lv_obj_center(label);

    /* 日志列表 */
    s_list = lv_obj_create(screen);
    ui_prepare_transparent_obj(s_list);
    lv_obj_set_size(s_list, 304, 194);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_pad_all(s_list, 4, 0);
    lv_obj_set_style_pad_row(s_list, 4, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_clear_flag(s_list, LV_OBJ_FLAG_SCROLL_ELASTIC);

    ui_error_log_render();

    /* 发起查询并启动轮询接收应答。 */
    ui_error_log_request();
    s_poll_timer = lv_timer_create(ui_error_log_poll_cb, UI_ERROR_LOG_POLL_MS, NULL);
}
