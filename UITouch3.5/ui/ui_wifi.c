#include "ui_wifi.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "buzzer.h"
#include "ui_alert.h"
#include "ui_common.h"
#include "ui_i18n.h"
#include "ui_settings.h"
#include "wifi_manager.h"

#define UI_WIFI_HEADER_H 36
#define UI_WIFI_REFRESH_MS 250U
#define UI_WIFI_LIST_X 16
#define UI_WIFI_LIST_Y 66
#define UI_WIFI_LIST_W 288
#define UI_WIFI_LIST_H 150
#define UI_WIFI_ROW_H 38

typedef enum {
    UI_WIFI_PAGE_LIST = 0,
    UI_WIFI_PAGE_PASSWORD,
    UI_WIFI_PAGE_DETAIL,
} ui_wifi_page_t;

typedef struct {
    bool active;
    bool pending_scan;
    ui_wifi_page_t page;
    ui_data_t *data;
    lv_timer_t *timer;
    lv_obj_t *status_label;
    lv_obj_t *enable_switch;
    lv_obj_t *list;
    lv_obj_t *password_textarea;
    lv_obj_t *password_error_label;
    lv_obj_t *detail_status_label;
    lv_obj_t *detail_ip_label;
    lv_obj_t *detail_signal_label;
    lv_obj_t *detail_security_label;
    lv_obj_t *forget_button;
    lv_obj_t *confirm_overlay;
    wifi_manager_ap_t aps[WIFI_MANAGER_MAX_APS];
    size_t ap_count;
    wifi_manager_ap_t selected_ap;
    uint32_t last_revision;
} ui_wifi_view_t;

static ui_wifi_view_t s_wifi;

static void ui_wifi_show_list(bool request_scan);
static void ui_wifi_show_password(const wifi_manager_ap_t *ap);
static void ui_wifi_show_detail(const wifi_manager_ap_t *ap);

static void ui_wifi_style_label(lv_obj_t *label,
                                uint32_t color,
                                lv_text_align_t align)
{
    lv_obj_set_style_text_font(label, &ui_setting_font_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_text_align(label, align, 0);
}

static void ui_wifi_style_button(lv_obj_t *button,
                                 uint32_t color,
                                 lv_coord_t radius)
{
    ui_reset_base_obj(button);
    lv_obj_set_style_radius(button, radius, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(UI_C_LINE), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_C_LINE), LV_STATE_PRESSED);
}

static lv_obj_t *ui_wifi_create_header_button(lv_obj_t *screen,
                                               const char *text,
                                               lv_align_t align,
                                               lv_coord_t x,
                                               lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(screen);
    ui_reset_base_obj(button);
    lv_obj_set_size(button, 72, 28);
    lv_obj_align(button, align, x, 4);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_10, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_C_PANEL), LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    ui_wifi_style_label(label, UI_C_TEXT_MED, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(label);
    return button;
}

static void ui_wifi_create_title(lv_obj_t *screen, const char *title)
{
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, title);
    ui_wifi_style_label(label, UI_C_TEXT_STRONG, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_font(label, &egg_ui_font_16, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);
}

static void ui_wifi_deactivate(void)
{
    s_wifi.active = false;
    if(s_wifi.timer != NULL) {
        lv_timer_delete(s_wifi.timer);
        s_wifi.timer = NULL;
    }
    if(s_wifi.password_textarea != NULL) {
        lv_textarea_set_text(s_wifi.password_textarea, "");
        s_wifi.password_textarea = NULL;
    }
    /*
     * 确认弹窗是 overlay, 静态变量持有的指针必须随页面一起失效,
     * 否则下次进入 detail 再点"忘记网络"会被 if(confirm_overlay) 拦截而无法弹出。
     * overlay 对象本身会随 ui_screen_reset 销毁, 这里只清指针。
     */
    s_wifi.confirm_overlay = NULL;
    memset(s_wifi.selected_ap.ssid, 0, sizeof(s_wifi.selected_ap.ssid));
}

static bool ui_wifi_ap_is_secure(const wifi_manager_ap_t *ap)
{
    return ap->authmode != WIFI_AUTH_OPEN;
}

static const char *ui_wifi_security_text(wifi_auth_mode_t authmode)
{
    switch(authmode) {
    case WIFI_AUTH_OPEN:
        return "OPEN";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2/WPA3";
    default:
        return "SEC";
    }
}

static const char *ui_wifi_error_text(wifi_manager_error_t error)
{
    switch(error) {
    case WIFI_MANAGER_ERROR_AUTH:
        return ui_t(UI_STR_WIFI_WRONG_PASSWORD);
    case WIFI_MANAGER_ERROR_NOT_FOUND:
        return ui_t(UI_STR_WIFI_NOT_FOUND);
    case WIFI_MANAGER_ERROR_WEAK_SIGNAL:
        return ui_t(UI_STR_WIFI_WEAK_SIGNAL);
    case WIFI_MANAGER_ERROR_DHCP:
        return ui_t(UI_STR_WIFI_DHCP_FAILED);
    case WIFI_MANAGER_ERROR_TIMEOUT:
    case WIFI_MANAGER_ERROR_INTERNAL:
        return ui_t(UI_STR_WIFI_CONNECT_FAILED);
    default:
        return "";
    }
}

static const char *ui_wifi_status_text(const wifi_manager_snapshot_t *snapshot)
{
    if(!snapshot->enabled) {
        return ui_t(UI_STR_WIFI_OFF);
    }

    switch(snapshot->state) {
    case WIFI_MANAGER_STATE_SCANNING:
        return ui_t(UI_STR_WIFI_SCANNING);
    case WIFI_MANAGER_STATE_CONNECTING:
    case WIFI_MANAGER_STATE_WAITING_IP:
        return ui_t(UI_STR_WIFI_CONNECTING);
    case WIFI_MANAGER_STATE_RETRY_WAIT:
        return snapshot->error == WIFI_MANAGER_ERROR_NONE
                   ? ui_t(UI_STR_WIFI_CONNECTING)
                   : ui_wifi_error_text(snapshot->error);
    case WIFI_MANAGER_STATE_CONNECTED:
        return ui_t(UI_STR_WIFI_CONNECTED);
    case WIFI_MANAGER_STATE_FAILED:
        return ui_wifi_error_text(snapshot->error);
    default:
        return snapshot->has_saved_network ? ui_t(UI_STR_WIFI_DISCONNECTED)
                                           : ui_t(UI_STR_WIFI_NOT_CONFIGURED);
    }
}

static void ui_wifi_back_to_setting_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    buzzer_beep_async(50);
    ui_data_t *data = s_wifi.data;
    ui_wifi_deactivate();
    ui_settings_create(data);
}

static void ui_wifi_back_to_list_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    buzzer_beep_async(50);
    if(s_wifi.password_textarea != NULL) {
        lv_textarea_set_text(s_wifi.password_textarea, "");
        s_wifi.password_textarea = NULL;
    }
    ui_wifi_show_list(false);
}

static void ui_wifi_update_list_status(const wifi_manager_snapshot_t *snapshot)
{
    if(s_wifi.status_label != NULL) {
        lv_label_set_text(s_wifi.status_label, ui_wifi_status_text(snapshot));
    }

    if(s_wifi.enable_switch != NULL) {
        if(snapshot->enabled) {
            lv_obj_add_state(s_wifi.enable_switch, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(s_wifi.enable_switch, LV_STATE_CHECKED);
        }
    }
}

static void ui_wifi_ap_row_cb(lv_event_t *event)
{
    const wifi_manager_ap_t *ap =
        (const wifi_manager_ap_t *)lv_event_get_user_data(event);
    wifi_manager_snapshot_t snapshot;

    if(ap == NULL) {
        return;
    }

    buzzer_beep_async(50);
    wifi_manager_get_snapshot(&snapshot);
    if((snapshot.connected && strcmp(snapshot.ssid, ap->ssid) == 0) ||
       (snapshot.has_saved_network &&
        strcmp(snapshot.saved_ssid, ap->ssid) == 0)) {
        ui_wifi_show_detail(ap);
    } else if(ui_wifi_ap_is_secure(ap)) {
        ui_wifi_show_password(ap);
    } else {
        s_wifi.selected_ap = *ap;
        if(wifi_manager_connect(ap->ssid, "", true) == ESP_OK) {
            ui_wifi_show_detail(ap);
        }
    }
}

static void ui_wifi_rebuild_rows(const wifi_manager_snapshot_t *snapshot)
{
    const lv_coord_t row_stride = ui_scale_y(UI_WIFI_ROW_H);

    if(s_wifi.list == NULL) {
        return;
    }

    lv_obj_clean(s_wifi.list);
    s_wifi.ap_count = wifi_manager_get_scan_results(s_wifi.aps,
                                                    WIFI_MANAGER_MAX_APS);

    if(snapshot->has_saved_network && snapshot->saved_ssid[0] != '\0') {
        bool saved_is_present = false;
        for(size_t i = 0U; i < s_wifi.ap_count; i++) {
            if(strcmp(s_wifi.aps[i].ssid, snapshot->saved_ssid) == 0) {
                saved_is_present = true;
                break;
            }
        }
        if(!saved_is_present) {
            const size_t move_count =
                s_wifi.ap_count < WIFI_MANAGER_MAX_APS
                    ? s_wifi.ap_count
                    : WIFI_MANAGER_MAX_APS - 1U;
            if(move_count > 0U) {
                memmove(&s_wifi.aps[1],
                        &s_wifi.aps[0],
                        move_count * sizeof(s_wifi.aps[0]));
            }
            memset(&s_wifi.aps[0], 0, sizeof(s_wifi.aps[0]));
            strlcpy(s_wifi.aps[0].ssid,
                    snapshot->saved_ssid,
                    sizeof(s_wifi.aps[0].ssid));
            s_wifi.aps[0].rssi = -127;
            s_wifi.aps[0].authmode = WIFI_AUTH_MAX;
            if(s_wifi.ap_count < WIFI_MANAGER_MAX_APS) {
                s_wifi.ap_count++;
            }
        }
    }

    if(snapshot->state == WIFI_MANAGER_STATE_SCANNING &&
       s_wifi.ap_count == 0U) {
        lv_obj_t *label = lv_label_create(s_wifi.list);
        lv_label_set_text(label, ui_t(UI_STR_WIFI_SCANNING));
        ui_wifi_style_label(label, UI_C_TEXT_MED, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(label, UI_WIFI_LIST_W);
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 28);
        return;
    }

    if(s_wifi.ap_count == 0U) {
        lv_obj_t *label = lv_label_create(s_wifi.list);
        lv_label_set_text(label, ui_t(UI_STR_WIFI_NO_NETWORKS));
        ui_wifi_style_label(label, UI_C_TEXT_DIM, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(label, UI_WIFI_LIST_W);
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 28);
        return;
    }

    for(size_t i = 0U; i < s_wifi.ap_count; i++) {
        const bool current = snapshot->connected &&
                             strcmp(snapshot->ssid, s_wifi.aps[i].ssid) == 0;
        const bool saved = snapshot->has_saved_network &&
                           strcmp(snapshot->saved_ssid,
                                  s_wifi.aps[i].ssid) == 0;
        lv_obj_t *button = lv_button_create(s_wifi.list);
        ui_reset_base_obj(button);
        lv_obj_set_size(button, UI_WIFI_LIST_W, UI_WIFI_ROW_H);
        /*
         * Scroll content can extend beyond the 240 px design canvas.  Passing
         * the accumulated Y value through the screen-coordinate adapter makes
         * scaling stop once it exceeds 240, which moves later rows backwards
         * and causes overlap.  Scale the stride once and position rows in
         * native LVGL coordinates instead.
         */
        lv_obj_set_x(button, 0);
        lv_obj_set_y(button, (lv_coord_t)i * row_stride);
        lv_obj_set_style_bg_opa(button, current ? LV_OPA_20 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(button,
                                  lv_color_hex(current ? UI_C_YELLOW : UI_C_PANEL),
                                  0);
        lv_obj_set_style_bg_opa(button, LV_OPA_20, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(button, lv_color_hex(UI_C_TEXT_MED),
                                  LV_STATE_PRESSED);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_border_side(button, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_add_event_cb(button,
                            ui_wifi_ap_row_cb,
                            LV_EVENT_CLICKED,
                            &s_wifi.aps[i]);

        lv_obj_t *ssid_label = lv_label_create(button);
        lv_label_set_text_fmt(ssid_label,
                              "%s%s",
                              current ? "* " : (saved ? "+ " : ""),
                              s_wifi.aps[i].ssid);
        ui_wifi_style_label(ssid_label,
                            current ? UI_C_TEXT_STRONG : UI_C_TEXT_MED,
                            LV_TEXT_ALIGN_LEFT);
        lv_obj_set_style_text_font(ssid_label, &ui_wifi_ssid_font_12, 0);
        lv_obj_set_width(ssid_label, 158);
        lv_label_set_long_mode(ssid_label, LV_LABEL_LONG_DOT);
        lv_obj_align(ssid_label, LV_ALIGN_LEFT_MID, 10, 0);

        lv_obj_t *info_label = lv_label_create(button);
        if(s_wifi.aps[i].rssi <= -127) {
            lv_label_set_text(info_label, "-- dBm  SEC");
        } else {
            lv_label_set_text_fmt(info_label,
                                  "%d dBm%s",
                                  (int)s_wifi.aps[i].rssi,
                                  ui_wifi_ap_is_secure(&s_wifi.aps[i])
                                      ? "  SEC"
                                      : "");
        }
        ui_wifi_style_label(info_label, UI_C_TEXT_DIM, LV_TEXT_ALIGN_RIGHT);
        lv_obj_set_width(info_label, 102);
        lv_obj_align(info_label, LV_ALIGN_RIGHT_MID, -8, 0);
    }
}

static void ui_wifi_refresh_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    buzzer_beep_async(50);
    esp_err_t err = wifi_manager_request_scan();
    if(err == ESP_OK) {
        s_wifi.pending_scan = false;
    } else if(err == ESP_ERR_INVALID_STATE) {
        s_wifi.pending_scan = true;
    }
}

static void ui_wifi_enable_cb(lv_event_t *event)
{
    lv_obj_t *switch_obj = lv_event_get_target(event);
    const bool enabled = lv_obj_has_state(switch_obj, LV_STATE_CHECKED);

    buzzer_beep_async(50);
    esp_err_t err = wifi_manager_set_enabled(enabled);
    if(err != ESP_OK) {
        if(enabled) {
            lv_obj_remove_state(switch_obj, LV_STATE_CHECKED);
        } else {
            lv_obj_add_state(switch_obj, LV_STATE_CHECKED);
        }
        return;
    }
    s_wifi.pending_scan = enabled;
}

static void ui_wifi_show_list(bool request_scan)
{
    wifi_manager_snapshot_t snapshot;
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *label;

    s_wifi.page = UI_WIFI_PAGE_LIST;
    s_wifi.status_label = NULL;
    s_wifi.enable_switch = NULL;
    s_wifi.list = NULL;
    s_wifi.password_textarea = NULL;
    s_wifi.password_error_label = NULL;
    s_wifi.detail_status_label = NULL;
    s_wifi.forget_button = NULL;
    s_wifi.confirm_overlay = NULL;
    ui_screen_reset(screen);

    (void)ui_wifi_create_header_button(screen,
                                       ui_t(UI_STR_BACK),
                                       LV_ALIGN_TOP_LEFT,
                                       4,
                                       ui_wifi_back_to_setting_cb);
    ui_wifi_create_title(screen, ui_t(UI_STR_WIFI));
    (void)ui_wifi_create_header_button(screen,
                                       ui_t(UI_STR_WIFI_REFRESH),
                                       LV_ALIGN_TOP_RIGHT,
                                       -4,
                                       ui_wifi_refresh_cb);

    s_wifi.status_label = lv_label_create(screen);
    ui_wifi_style_label(s_wifi.status_label, UI_C_TEXT_MED, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_width(s_wifi.status_label, 210);
    lv_obj_align(s_wifi.status_label, LV_ALIGN_TOP_LEFT, 18, 44);

    s_wifi.enable_switch = lv_switch_create(screen);
    lv_obj_set_size(s_wifi.enable_switch, 42, 22);
    lv_obj_align(s_wifi.enable_switch, LV_ALIGN_TOP_RIGHT, -18, 38);
    lv_obj_set_style_bg_color(s_wifi.enable_switch,
                              lv_color_hex(UI_C_LINE),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_wifi.enable_switch,
                              lv_color_hex(UI_C_YELLOW),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(s_wifi.enable_switch,
                              lv_color_hex(UI_C_TEXT_STRONG),
                              LV_PART_KNOB);

    s_wifi.list = lv_obj_create(screen);
    ui_prepare_transparent_obj(s_wifi.list);
    lv_obj_set_size(s_wifi.list, UI_WIFI_LIST_W, UI_WIFI_LIST_H);
    lv_obj_set_pos(s_wifi.list, UI_WIFI_LIST_X, UI_WIFI_LIST_Y);
    lv_obj_add_flag(s_wifi.list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_wifi.list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_wifi.list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_all(s_wifi.list, 0, 0);

    label = lv_label_create(screen);
    lv_label_set_text(label, ui_t(UI_STR_WIFI_ONLY_24G));
    ui_wifi_style_label(label, UI_C_TEXT_DIM, LV_TEXT_ALIGN_CENTER);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -6);

    wifi_manager_get_snapshot(&snapshot);
    ui_wifi_update_list_status(&snapshot);
    ui_wifi_rebuild_rows(&snapshot);
    lv_obj_add_event_cb(s_wifi.enable_switch,
                        ui_wifi_enable_cb,
                        LV_EVENT_VALUE_CHANGED,
                        NULL);

    if(request_scan && snapshot.enabled) {
        esp_err_t err = wifi_manager_request_scan();
        s_wifi.pending_scan = err == ESP_ERR_INVALID_STATE;
    }
}

static void ui_wifi_password_submit(void)
{
    if(s_wifi.password_textarea == NULL) {
        return;
    }

    const char *password = lv_textarea_get_text(s_wifi.password_textarea);
    const size_t password_length = strlen(password);
    if(ui_wifi_ap_is_secure(&s_wifi.selected_ap) &&
       (password_length < 8U ||
        password_length > WIFI_MANAGER_PASSWORD_MAX_LEN)) {
        if(s_wifi.password_error_label != NULL) {
            lv_label_set_text(s_wifi.password_error_label,
                              ui_t(UI_STR_WIFI_PASSWORD_LENGTH));
        }
        return;
    }

    esp_err_t err = wifi_manager_connect(s_wifi.selected_ap.ssid,
                                         password,
                                         true);
    if(err != ESP_OK) {
        /*
         * 同步失败(空 SSID / 状态异常等): 不前进到 detail 页,
         * 否则用户会看到"按了没反应"。在密码页显示错误, 保留输入方便修改。
         */
        if(s_wifi.password_error_label != NULL) {
            lv_label_set_text(s_wifi.password_error_label,
                              ui_t(UI_STR_WIFI_CONNECT_FAILED));
        }
        return;
    }

    lv_textarea_set_text(s_wifi.password_textarea, "");
    s_wifi.password_textarea = NULL;
    ui_wifi_show_detail(&s_wifi.selected_ap);
}

static void ui_wifi_keyboard_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if(code == LV_EVENT_READY) {
        buzzer_beep_async(50);
        ui_wifi_password_submit();
    } else if(code == LV_EVENT_CANCEL) {
        ui_wifi_back_to_list_cb(event);
    }
}

static void ui_wifi_show_password(const wifi_manager_ap_t *ap)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *label;
    lv_obj_t *keyboard;

    s_wifi.page = UI_WIFI_PAGE_PASSWORD;
    s_wifi.selected_ap = *ap;
    s_wifi.status_label = NULL;
    s_wifi.enable_switch = NULL;
    s_wifi.list = NULL;
    s_wifi.detail_status_label = NULL;
    s_wifi.confirm_overlay = NULL;
    ui_screen_reset(screen);

    (void)ui_wifi_create_header_button(screen,
                                       ui_t(UI_STR_BACK),
                                       LV_ALIGN_TOP_LEFT,
                                       4,
                                       ui_wifi_back_to_list_cb);
    ui_wifi_create_title(screen, ui_t(UI_STR_WIFI_PASSWORD));

    /* SSID 名称。统一用 ui_setting_font_12(已覆盖 Wi-Fi 所有中文),
     * 避免 egg_ui_font_16 缺字导致中文 SSID 或提示乱码。 */
    label = lv_label_create(screen);
    lv_label_set_text(label, s_wifi.selected_ap.ssid);
    ui_wifi_style_label(label, UI_C_TEXT_STRONG, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_width(label, 288);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 16, 38);

    /* 密码输入框。按需求改为明文, 不再提供掩码/显示切换。 */
    s_wifi.password_textarea = lv_textarea_create(screen);
    lv_textarea_set_one_line(s_wifi.password_textarea, true);
    lv_textarea_set_password_mode(s_wifi.password_textarea, false);
    lv_textarea_set_max_length(s_wifi.password_textarea,
                               WIFI_MANAGER_PASSWORD_MAX_LEN);
    /* placeholder 必须用 ASCII, 因为 textarea 字体是 montserrat_14(无中文),
     * 用中文"密码"会乱码。这里用纯英文 placeholder。 */
    lv_textarea_set_placeholder_text(s_wifi.password_textarea, "Password");
    lv_obj_set_size(s_wifi.password_textarea, 288, 32);
    lv_obj_align(s_wifi.password_textarea, LV_ALIGN_TOP_MID, 0, 56);
    /* 密码本就是 ASCII, 用 montserrat_14 即可, 不依赖中文字库。 */
    lv_obj_set_style_text_font(s_wifi.password_textarea, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_wifi.password_textarea,
                                lv_color_hex(UI_C_TEXT_STRONG),
                                0);
    lv_obj_set_style_bg_color(s_wifi.password_textarea,
                              lv_color_hex(UI_C_PANEL),
                              0);
    lv_obj_set_style_border_color(s_wifi.password_textarea,
                                  lv_color_hex(UI_C_LINE),
                                  0);
    lv_obj_set_style_border_width(s_wifi.password_textarea, 1, 0);

    /* 错误提示行(初始空)。 */
    s_wifi.password_error_label = lv_label_create(screen);
    lv_label_set_text(s_wifi.password_error_label, "");
    ui_wifi_style_label(s_wifi.password_error_label,
                        UI_C_RED,
                        LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_wifi.password_error_label, 288);
    lv_obj_align(s_wifi.password_error_label, LV_ALIGN_TOP_MID, 0, 92);

    /* 键盘放大: 高度 132(原 112), 占据 y=108~240 全部剩余空间,
     * 字体也用 14 号提升可触性。 */
    keyboard = lv_keyboard_create(screen);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(keyboard, s_wifi.password_textarea);
    lv_obj_set_size(keyboard, 320, 132);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_14,
                               LV_PART_ITEMS);
    /* 增大按键内边距, 让字符更易点中。 */
    lv_obj_set_style_pad_all(keyboard, 2, LV_PART_ITEMS);
    lv_obj_set_style_pad_gap(keyboard, 1, LV_PART_ITEMS);
    lv_obj_add_event_cb(keyboard,
                        ui_wifi_keyboard_cb,
                        LV_EVENT_ALL,
                        NULL);
}

static void ui_wifi_detail_back_cb(lv_event_t *event)
{
    ui_wifi_back_to_list_cb(event);
}

static void ui_wifi_detail_reconnect_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    buzzer_beep_async(50);
    (void)wifi_manager_reconnect();
}

static void ui_wifi_detail_disconnect_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    buzzer_beep_async(50);
    (void)wifi_manager_disconnect();
}

static void ui_wifi_forget_cancel_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    buzzer_beep_async(50);
    if(s_wifi.confirm_overlay != NULL) {
        lv_obj_delete(s_wifi.confirm_overlay);
        s_wifi.confirm_overlay = NULL;
    }
}

static void ui_wifi_forget_confirm_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    buzzer_beep_async(50);
    (void)wifi_manager_forget_network();
    s_wifi.confirm_overlay = NULL;
    ui_wifi_show_list(true);
}

static lv_obj_t *ui_wifi_create_dialog_button(lv_obj_t *parent,
                                               const char *text,
                                               lv_coord_t x,
                                               lv_event_cb_t callback,
                                               bool primary)
{
    lv_obj_t *button = lv_button_create(parent);
    ui_dialog_style_button(button, UI_DIALOG_DOUBLE_BUTTON_W, primary);
    lv_obj_align(button, LV_ALIGN_BOTTOM_MID, x, -UI_DIALOG_BUTTON_BOTTOM);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    ui_dialog_style_button_label(label, primary);
    return button;
}

static void ui_wifi_detail_forget_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    wifi_manager_snapshot_t snapshot;

    buzzer_beep_async(50);
    wifi_manager_get_snapshot(&snapshot);
    if(!snapshot.has_saved_network ||
       strcmp(snapshot.saved_ssid, s_wifi.selected_ap.ssid) != 0) {
        return;
    }
    if(s_wifi.confirm_overlay != NULL) {
        return;
    }

    lv_obj_t *screen = lv_screen_active();
    s_wifi.confirm_overlay = lv_obj_create(screen);
    ui_dialog_style_overlay(s_wifi.confirm_overlay);
    lv_obj_add_flag(s_wifi.confirm_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(s_wifi.confirm_overlay);

    lv_obj_t *card = lv_obj_create(s_wifi.confirm_overlay);
    ui_dialog_style_card(card);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, ui_t(UI_STR_NOTICE));
    ui_dialog_style_title(title);

    lv_obj_t *message_panel = lv_obj_create(card);
    ui_dialog_style_message_panel(message_panel);

    lv_obj_t *message = lv_label_create(message_panel);
    lv_label_set_text(message, ui_t(UI_STR_WIFI_FORGET_CONFIRM));
    ui_dialog_style_message(message);

    (void)ui_wifi_create_dialog_button(card,
                                       ui_t(UI_STR_CANCEL),
                                       -46,
                                       ui_wifi_forget_cancel_cb,
                                       false);
    (void)ui_wifi_create_dialog_button(card,
                                       ui_t(UI_STR_WIFI_FORGET),
                                       46,
                                       ui_wifi_forget_confirm_cb,
                                       true);
}

static lv_obj_t *ui_wifi_create_action_button(lv_obj_t *screen,
                                               const char *text,
                                               lv_coord_t x,
                                               uint32_t color,
                                               lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(screen);
    ui_wifi_style_button(button, UI_C_PANEL, 8);
    lv_obj_set_size(button, 92, 34);
    lv_obj_align(button, LV_ALIGN_BOTTOM_LEFT, x, -12);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    ui_wifi_style_label(label, color, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(label);
    return button;
}

static void ui_wifi_update_detail(const wifi_manager_snapshot_t *snapshot)
{
    if(s_wifi.detail_status_label == NULL) {
        return;
    }

    const bool selected_is_current =
        strcmp(snapshot->ssid, s_wifi.selected_ap.ssid) == 0;
    const char *status = selected_is_current
                             ? ui_wifi_status_text(snapshot)
                             : ui_t(UI_STR_WIFI_DISCONNECTED);
    lv_label_set_text(s_wifi.detail_status_label, status);
    lv_obj_set_style_text_color(s_wifi.detail_status_label,
                                lv_color_hex(snapshot->connected &&
                                                     selected_is_current
                                                 ? UI_C_YELLOW
                                                 : UI_C_TEXT_MED),
                                0);

    lv_label_set_text_fmt(s_wifi.detail_ip_label,
                          "%s: %s",
                          ui_t(UI_STR_WIFI_IP_ADDRESS),
                          snapshot->connected && selected_is_current
                              ? snapshot->ip
                              : "--");
    if(snapshot->connected && selected_is_current) {
        lv_label_set_text_fmt(s_wifi.detail_signal_label,
                              "%s: %d dBm",
                              ui_t(UI_STR_WIFI_SIGNAL),
                              (int)snapshot->rssi);
    } else {
        lv_label_set_text_fmt(s_wifi.detail_signal_label,
                              "%s: --",
                              ui_t(UI_STR_WIFI_SIGNAL));
    }
    lv_label_set_text_fmt(s_wifi.detail_security_label,
                          "%s: %s",
                          ui_t(UI_STR_WIFI_SECURITY),
                          ui_wifi_security_text(s_wifi.selected_ap.authmode));

    if(s_wifi.forget_button != NULL) {
        if(snapshot->has_saved_network &&
           strcmp(snapshot->saved_ssid, s_wifi.selected_ap.ssid) == 0) {
            lv_obj_clear_flag(s_wifi.forget_button, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_wifi.forget_button, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void ui_wifi_show_detail(const wifi_manager_ap_t *ap)
{
    wifi_manager_snapshot_t snapshot;
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *label;

    s_wifi.page = UI_WIFI_PAGE_DETAIL;
    s_wifi.selected_ap = *ap;
    s_wifi.status_label = NULL;
    s_wifi.enable_switch = NULL;
    s_wifi.list = NULL;
    s_wifi.password_textarea = NULL;
    s_wifi.confirm_overlay = NULL;
    ui_screen_reset(screen);

    (void)ui_wifi_create_header_button(screen,
                                       ui_t(UI_STR_BACK),
                                       LV_ALIGN_TOP_LEFT,
                                       4,
                                       ui_wifi_detail_back_cb);
    ui_wifi_create_title(screen, ui_t(UI_STR_WIFI));

    label = lv_label_create(screen);
    lv_label_set_text(label, s_wifi.selected_ap.ssid);
    ui_wifi_style_label(label, UI_C_TEXT_STRONG, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_font(label, &egg_ui_font_16, 0);
    lv_obj_set_width(label, 288);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 43);

    s_wifi.detail_status_label = lv_label_create(screen);
    ui_wifi_style_label(s_wifi.detail_status_label,
                        UI_C_TEXT_MED,
                        LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_wifi.detail_status_label, 288);
    lv_obj_align(s_wifi.detail_status_label, LV_ALIGN_TOP_MID, 0, 68);

    s_wifi.detail_ip_label = lv_label_create(screen);
    ui_wifi_style_label(s_wifi.detail_ip_label,
                        UI_C_TEXT_MED,
                        LV_TEXT_ALIGN_LEFT);
    lv_obj_align(s_wifi.detail_ip_label, LV_ALIGN_TOP_LEFT, 32, 96);

    s_wifi.detail_signal_label = lv_label_create(screen);
    ui_wifi_style_label(s_wifi.detail_signal_label,
                        UI_C_TEXT_MED,
                        LV_TEXT_ALIGN_LEFT);
    lv_obj_align(s_wifi.detail_signal_label, LV_ALIGN_TOP_LEFT, 32, 119);

    s_wifi.detail_security_label = lv_label_create(screen);
    ui_wifi_style_label(s_wifi.detail_security_label,
                        UI_C_TEXT_MED,
                        LV_TEXT_ALIGN_LEFT);
    lv_obj_align(s_wifi.detail_security_label, LV_ALIGN_TOP_LEFT, 32, 142);

    (void)ui_wifi_create_action_button(screen,
                                       ui_t(UI_STR_WIFI_RECONNECT),
                                       10,
                                       UI_C_TEXT_STRONG,
                                       ui_wifi_detail_reconnect_cb);
    (void)ui_wifi_create_action_button(screen,
                                       ui_t(UI_STR_WIFI_DISCONNECT),
                                       114,
                                       UI_C_TEXT_MED,
                                       ui_wifi_detail_disconnect_cb);
    s_wifi.forget_button =
        ui_wifi_create_action_button(screen,
                                     ui_t(UI_STR_WIFI_FORGET),
                                     218,
                                     UI_C_RED,
                                     ui_wifi_detail_forget_cb);

    wifi_manager_get_snapshot(&snapshot);
    ui_wifi_update_detail(&snapshot);
}

static void ui_wifi_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    wifi_manager_snapshot_t snapshot;

    if(!s_wifi.active) {
        return;
    }

    if(s_wifi.data != NULL &&
       ui_alert_should_show(ui_data_get_state(s_wifi.data))) {
        ui_data_t *data = s_wifi.data;
        ui_wifi_deactivate();
        ui_alert_create(data);
        return;
    }

    wifi_manager_get_snapshot(&snapshot);

    if(s_wifi.pending_scan && snapshot.enabled && snapshot.driver_started &&
       snapshot.state != WIFI_MANAGER_STATE_CONNECTING &&
       snapshot.state != WIFI_MANAGER_STATE_WAITING_IP &&
       snapshot.state != WIFI_MANAGER_STATE_SCANNING) {
        if(wifi_manager_request_scan() == ESP_OK) {
            s_wifi.pending_scan = false;
        }
    }

    if(snapshot.revision == s_wifi.last_revision) {
        return;
    }
    s_wifi.last_revision = snapshot.revision;

    if(s_wifi.page == UI_WIFI_PAGE_LIST) {
        ui_wifi_update_list_status(&snapshot);
        ui_wifi_rebuild_rows(&snapshot);
    } else if(s_wifi.page == UI_WIFI_PAGE_DETAIL) {
        ui_wifi_update_detail(&snapshot);
    }
}

void ui_wifi_create(ui_data_t *data)
{
    if(data == NULL) {
        return;
    }

    ui_wifi_deactivate();
    memset(&s_wifi, 0, sizeof(s_wifi));
    s_wifi.active = true;
    s_wifi.data = data;
    s_wifi.last_revision = UINT32_MAX;
    ui_wifi_show_list(true);
    s_wifi.timer = lv_timer_create(ui_wifi_timer_cb, UI_WIFI_REFRESH_MS, NULL);
}
