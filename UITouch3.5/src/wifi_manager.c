#include "wifi_manager.h"
#include "gpio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#define WIFI_NVS_NAMESPACE "wifi_cfg"
#define WIFI_NVS_KEY_VERSION "ver"
#define WIFI_NVS_KEY_ENABLED "enabled"
#define WIFI_NVS_KEY_SSID "ssid"
#define WIFI_NVS_KEY_PASSWORD "pass"
#define WIFI_NVS_VERSION 1U

#define WIFI_EVENT_QUEUE_LENGTH 12U
#define WIFI_WORKER_STACK_SIZE 6144U
#define WIFI_WORKER_PRIORITY 4U
/*
 * 30s 同时覆盖关联+认证(~3-5s)与 DHCP(~数秒, 慢速路由器可达 ~20s)。
 * 设为 20s 会把慢但最终能成功的 DHCP 误判为失败, 触发不必要的退避重连。
 * GOT_IP 到达时会主动 stop 该定时器, 所以 30s 仅作兜底。
 */
#define WIFI_CONNECT_TIMEOUT_US (30LL * 1000LL * 1000LL)

static const char *TAG = "wifi_mgr";

static void configure_radio_power(void)
{
    esp_err_t err = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "could not enable Wi-Fi power save: %s",
                 esp_err_to_name(err));
    }

    err = esp_wifi_set_max_tx_power(WIFI_MAX_TX_POWER_QDBM);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "could not limit Wi-Fi TX power: %s",
                 esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Wi-Fi TX power limited to %.2f dBm",
                 (double)WIFI_MAX_TX_POWER_QDBM / 4.0);
    }
}

/* 首次拿到 IP 后启动 SNTP 校时（东八区），仅初始化一次。 */
static bool s_sntp_started;

static void wifi_manager_configure_timezone(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();
}

static void wifi_manager_start_sntp(void)
{
    if(s_sntp_started) {
        return;
    }
    s_sntp_started = true;

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "pool.ntp.org");
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP time sync started");
}

typedef enum {
    MANAGER_EVENT_STA_START = 0,
    MANAGER_EVENT_STA_CONNECTED,
    MANAGER_EVENT_STA_DISCONNECTED,
    MANAGER_EVENT_SCAN_DONE,
    MANAGER_EVENT_GOT_IP,
    MANAGER_EVENT_RECONNECT_TIMER,
    MANAGER_EVENT_CONNECT_TIMEOUT,
} manager_event_id_t;

typedef struct {
    manager_event_id_t id;
    union {
        uint16_t disconnect_reason;
        esp_ip4_addr_t ip;
    } data;
} manager_event_t;

typedef struct {
    wifi_manager_snapshot_t snapshot;
    wifi_manager_ap_t aps[WIFI_MANAGER_MAX_APS];
    size_t ap_count;
    wifi_manager_state_t state_before_scan;
    bool desired_connected;
    bool ignore_next_disconnect;
    bool pending_save;
    bool has_pending_credentials;
    char saved_ssid[WIFI_MANAGER_SSID_MAX_LEN + 1U];
    char saved_password[WIFI_MANAGER_PASSWORD_MAX_LEN + 1U];
    char pending_ssid[WIFI_MANAGER_SSID_MAX_LEN + 1U];
    char pending_password[WIFI_MANAGER_PASSWORD_MAX_LEN + 1U];
} wifi_manager_context_t;

static wifi_manager_context_t s_ctx;
static SemaphoreHandle_t s_mutex;
static QueueHandle_t s_event_queue;
static TaskHandle_t s_worker_task;
static esp_timer_handle_t s_reconnect_timer;
static esp_timer_handle_t s_connect_timeout_timer;
static esp_event_handler_instance_t s_wifi_event_instance;
static esp_event_handler_instance_t s_ip_event_instance;

static bool wifi_lock(TickType_t timeout)
{
    return s_mutex != NULL && xSemaphoreTake(s_mutex, timeout) == pdTRUE;
}

static void wifi_unlock(void)
{
    xSemaphoreGive(s_mutex);
}

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if(dst == NULL || dst_size == 0U) {
        return;
    }

    if(src == NULL) {
        dst[0] = '\0';
        return;
    }

    strlcpy(dst, src, dst_size);
}

static void set_state_locked(wifi_manager_state_t state, wifi_manager_error_t error)
{
    if(s_ctx.snapshot.state != state || s_ctx.snapshot.error != error) {
        s_ctx.snapshot.state = state;
        s_ctx.snapshot.error = error;
        s_ctx.snapshot.revision++;
    }
}

static bool password_is_valid(const char *password)
{
    const size_t length = password != NULL ? strlen(password) : 0U;
    return length == 0U || (length >= 8U && length <= WIFI_MANAGER_PASSWORD_MAX_LEN);
}

static bool ssid_is_valid(const char *ssid)
{
    const size_t length = ssid != NULL ? strlen(ssid) : 0U;
    return length > 0U && length <= WIFI_MANAGER_SSID_MAX_LEN;
}

static esp_err_t nvs_store_enabled(bool enabled)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if(err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(handle, WIFI_NVS_KEY_VERSION, WIFI_NVS_VERSION);
    if(err == ESP_OK) {
        err = nvs_set_u8(handle, WIFI_NVS_KEY_ENABLED, enabled ? 1U : 0U);
    }
    if(err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t nvs_store_credentials(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if(err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(handle, WIFI_NVS_KEY_VERSION, WIFI_NVS_VERSION);
    if(err == ESP_OK) {
        err = nvs_set_str(handle, WIFI_NVS_KEY_SSID, ssid);
    }
    if(err == ESP_OK) {
        err = nvs_set_str(handle, WIFI_NVS_KEY_PASSWORD, password);
    }
    if(err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t nvs_clear_credentials(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if(err != ESP_OK) {
        return err;
    }

    esp_err_t erase_err = nvs_erase_key(handle, WIFI_NVS_KEY_SSID);
    if(erase_err != ESP_OK && erase_err != ESP_ERR_NVS_NOT_FOUND) {
        err = erase_err;
    }
    erase_err = nvs_erase_key(handle, WIFI_NVS_KEY_PASSWORD);
    if(err == ESP_OK && erase_err != ESP_OK && erase_err != ESP_ERR_NVS_NOT_FOUND) {
        err = erase_err;
    }
    if(err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static void nvs_load_configuration(bool *enabled,
                                   char *ssid,
                                   size_t ssid_size,
                                   char *password,
                                   size_t password_size)
{
    nvs_handle_t handle;
    uint8_t value = 1U;
    uint8_t version = 0U;

    *enabled = true;
    ssid[0] = '\0';
    password[0] = '\0';

    if(nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }

    if(nvs_get_u8(handle, WIFI_NVS_KEY_VERSION, &version) == ESP_OK &&
       version == WIFI_NVS_VERSION) {
        if(nvs_get_u8(handle, WIFI_NVS_KEY_ENABLED, &value) == ESP_OK) {
            *enabled = value != 0U;
        }

        size_t length = ssid_size;
        if(nvs_get_str(handle, WIFI_NVS_KEY_SSID, ssid, &length) != ESP_OK) {
            ssid[0] = '\0';
        }
        length = password_size;
        if(nvs_get_str(handle, WIFI_NVS_KEY_PASSWORD, password, &length) != ESP_OK) {
            password[0] = '\0';
        }
    }

    nvs_close(handle);
}

static esp_err_t apply_credentials(const char *ssid, const char *password)
{
    wifi_config_t config = {0};
    esp_err_t err;
    const size_t ssid_length = strlen(ssid);
    memcpy(config.sta.ssid,
           ssid,
           ssid_length < sizeof(config.sta.ssid)
               ? ssid_length
               : sizeof(config.sta.ssid));
    copy_string((char *)config.sta.password, sizeof(config.sta.password), password);
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    err = esp_wifi_set_config(WIFI_IF_STA, &config);
    memset(&config, 0, sizeof(config));
    return err;
}

static wifi_manager_error_t classify_disconnect_reason(uint16_t reason)
{
    switch(reason) {
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return WIFI_MANAGER_ERROR_AUTH;

    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return WIFI_MANAGER_ERROR_NOT_FOUND;

    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return WIFI_MANAGER_ERROR_WEAK_SIGNAL;

    default:
        return WIFI_MANAGER_ERROR_INTERNAL;
    }
}

static int ap_compare(const void *lhs, const void *rhs)
{
    const wifi_manager_ap_t *a = (const wifi_manager_ap_t *)lhs;
    const wifi_manager_ap_t *b = (const wifi_manager_ap_t *)rhs;
    return (int)b->rssi - (int)a->rssi;
}

static void post_manager_event(const manager_event_t *event)
{
    if(s_event_queue == NULL ||
       xQueueSend(s_event_queue, event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full, dropping event %d", (int)event->id);
    }
}

static void reconnect_timer_callback(void *arg)
{
    (void)arg;
    const manager_event_t event = {.id = MANAGER_EVENT_RECONNECT_TIMER};
    post_manager_event(&event);
}

static void connect_timeout_callback(void *arg)
{
    (void)arg;
    const manager_event_t event = {.id = MANAGER_EVENT_CONNECT_TIMEOUT};
    post_manager_event(&event);
}

static void stop_connect_timeout(void)
{
    if(s_connect_timeout_timer != NULL) {
        (void)esp_timer_stop(s_connect_timeout_timer);
    }
}

static void start_connect_timeout(void)
{
    if(s_connect_timeout_timer == NULL) {
        return;
    }

    (void)esp_timer_stop(s_connect_timeout_timer);
    esp_err_t err = esp_timer_start_once(s_connect_timeout_timer,
                                         WIFI_CONNECT_TIMEOUT_US);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "could not start connect timeout: %s", esp_err_to_name(err));
    }
}

static uint64_t reconnect_delay_us(uint8_t retry_count)
{
    static const uint8_t delays_s[] = {2U, 4U, 8U, 16U, 30U};
    if(retry_count < sizeof(delays_s)) {
        return (uint64_t)delays_s[retry_count] * 1000ULL * 1000ULL;
    }
    return 60ULL * 1000ULL * 1000ULL;
}

static void schedule_reconnect(wifi_manager_error_t error)
{
    uint8_t retry_count;

    if(!wifi_lock(portMAX_DELAY)) {
        return;
    }
    if(!s_ctx.snapshot.enabled || !s_ctx.desired_connected) {
        set_state_locked(s_ctx.snapshot.enabled ? WIFI_MANAGER_STATE_IDLE
                                                : WIFI_MANAGER_STATE_DISABLED,
                         error);
        wifi_unlock();
        return;
    }

    retry_count = s_ctx.snapshot.retry_count;
    if(s_ctx.snapshot.retry_count < UINT8_MAX) {
        s_ctx.snapshot.retry_count++;
    }
    set_state_locked(WIFI_MANAGER_STATE_RETRY_WAIT, error);
    wifi_unlock();

    if(s_reconnect_timer != NULL) {
        (void)esp_timer_stop(s_reconnect_timer);
        esp_err_t err = esp_timer_start_once(s_reconnect_timer,
                                             reconnect_delay_us(retry_count));
        if(err != ESP_OK) {
            ESP_LOGW(TAG, "could not schedule reconnect: %s", esp_err_to_name(err));
        }
    }
}

static esp_err_t connect_now(void)
{
    esp_err_t err;
    bool stop_scan = false;

    if(!wifi_lock(portMAX_DELAY)) {
        return ESP_ERR_TIMEOUT;
    }
    if(!s_ctx.snapshot.enabled || !s_ctx.snapshot.driver_started ||
       !s_ctx.desired_connected) {
        wifi_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    if(s_ctx.snapshot.connected) {
        wifi_unlock();
        return ESP_OK;
    }
    stop_scan = s_ctx.snapshot.state == WIFI_MANAGER_STATE_SCANNING;
    set_state_locked(WIFI_MANAGER_STATE_CONNECTING, WIFI_MANAGER_ERROR_NONE);
    wifi_unlock();

    if(stop_scan) {
        (void)esp_wifi_scan_stop();
    }
    err = esp_wifi_connect();
    if(err == ESP_OK) {
        start_connect_timeout();
    } else {
        schedule_reconnect(WIFI_MANAGER_ERROR_INTERNAL);
    }
    return err;
}

static void handle_scan_done(void)
{
    uint16_t available = 0U;
    uint16_t requested = WIFI_MANAGER_MAX_APS;
    wifi_ap_record_t records[WIFI_MANAGER_MAX_APS];
    wifi_manager_ap_t deduplicated[WIFI_MANAGER_MAX_APS];
    size_t result_count = 0U;

    esp_err_t err = esp_wifi_scan_get_ap_num(&available);
    if(err == ESP_OK && available > 0U) {
        if(requested > available) {
            requested = available;
        }
        memset(records, 0, sizeof(records));
        err = esp_wifi_scan_get_ap_records(&requested, records);
    }

    if(err == ESP_OK) {
        for(uint16_t i = 0U; i < requested; i++) {
            const char *ssid = (const char *)records[i].ssid;
            if(ssid[0] == '\0') {
                continue;
            }

            size_t duplicate = result_count;
            for(size_t j = 0U; j < result_count; j++) {
                if(strcmp(deduplicated[j].ssid, ssid) == 0) {
                    duplicate = j;
                    break;
                }
            }

            if(duplicate < result_count) {
                if(records[i].rssi > deduplicated[duplicate].rssi) {
                    deduplicated[duplicate].rssi = records[i].rssi;
                    deduplicated[duplicate].authmode = records[i].authmode;
                    deduplicated[duplicate].channel = records[i].primary;
                }
            } else if(result_count < WIFI_MANAGER_MAX_APS) {
                copy_string(deduplicated[result_count].ssid,
                            sizeof(deduplicated[result_count].ssid),
                            ssid);
                deduplicated[result_count].rssi = records[i].rssi;
                deduplicated[result_count].authmode = records[i].authmode;
                deduplicated[result_count].channel = records[i].primary;
                result_count++;
            }
        }
        qsort(deduplicated, result_count, sizeof(deduplicated[0]), ap_compare);
    } else {
        /* This also releases any scan buffer retained by the driver. */
        uint16_t release_count = 0U;
        (void)esp_wifi_scan_get_ap_records(&release_count, NULL);
        ESP_LOGW(TAG, "scan result failed: %s", esp_err_to_name(err));
    }

    if(!wifi_lock(portMAX_DELAY)) {
        return;
    }
    if(err == ESP_OK) {
        memcpy(s_ctx.aps, deduplicated, result_count * sizeof(deduplicated[0]));
        s_ctx.ap_count = result_count;
    }
    if(s_ctx.snapshot.state == WIFI_MANAGER_STATE_SCANNING) {
        if(!s_ctx.snapshot.enabled) {
            set_state_locked(WIFI_MANAGER_STATE_DISABLED,
                             WIFI_MANAGER_ERROR_NONE);
        } else if(s_ctx.snapshot.connected) {
            set_state_locked(WIFI_MANAGER_STATE_CONNECTED, WIFI_MANAGER_ERROR_NONE);
        } else if(err == ESP_OK) {
            set_state_locked(WIFI_MANAGER_STATE_IDLE, WIFI_MANAGER_ERROR_NONE);
        } else {
            set_state_locked(WIFI_MANAGER_STATE_FAILED, WIFI_MANAGER_ERROR_INTERNAL);
        }
    }
    s_ctx.snapshot.revision++;
    wifi_unlock();
}

static void handle_got_ip(const esp_ip4_addr_t *ip)
{
    wifi_ap_record_t ap_info;
    char save_ssid[WIFI_MANAGER_SSID_MAX_LEN + 1U] = {0};
    char save_password[WIFI_MANAGER_PASSWORD_MAX_LEN + 1U] = {0};
    bool save_credentials = false;

    stop_connect_timeout();
    (void)esp_timer_stop(s_reconnect_timer);

    memset(&ap_info, 0, sizeof(ap_info));
    (void)esp_wifi_sta_get_ap_info(&ap_info);

    if(!wifi_lock(portMAX_DELAY)) {
        return;
    }
    s_ctx.snapshot.connected = true;
    s_ctx.snapshot.retry_count = 0U;
    s_ctx.snapshot.disconnect_reason = 0U;
    s_ctx.snapshot.rssi = ap_info.rssi;
    snprintf(s_ctx.snapshot.ip, sizeof(s_ctx.snapshot.ip), IPSTR, IP2STR(ip));
    if(ap_info.ssid[0] != '\0') {
        copy_string(s_ctx.snapshot.ssid,
                    sizeof(s_ctx.snapshot.ssid),
                    (const char *)ap_info.ssid);
    } else if(s_ctx.has_pending_credentials) {
        copy_string(s_ctx.snapshot.ssid,
                    sizeof(s_ctx.snapshot.ssid),
                    s_ctx.pending_ssid);
    }
    set_state_locked(WIFI_MANAGER_STATE_CONNECTED, WIFI_MANAGER_ERROR_NONE);

    if(s_ctx.pending_save && s_ctx.has_pending_credentials) {
        copy_string(save_ssid, sizeof(save_ssid), s_ctx.pending_ssid);
        copy_string(save_password, sizeof(save_password), s_ctx.pending_password);
        save_credentials = true;
    }
    wifi_unlock();

    if(save_credentials) {
        esp_err_t err = nvs_store_credentials(save_ssid, save_password);
        if(err == ESP_OK) {
            if(wifi_lock(portMAX_DELAY)) {
                copy_string(s_ctx.saved_ssid, sizeof(s_ctx.saved_ssid), save_ssid);
                copy_string(s_ctx.saved_password, sizeof(s_ctx.saved_password), save_password);
                copy_string(s_ctx.snapshot.saved_ssid,
                            sizeof(s_ctx.snapshot.saved_ssid),
                            save_ssid);
                s_ctx.snapshot.has_saved_network = true;
                s_ctx.snapshot.revision++;
                wifi_unlock();
            }
        } else {
            ESP_LOGE(TAG, "could not persist Wi-Fi credentials: %s",
                     esp_err_to_name(err));
        }
    }
    memset(save_password, 0, sizeof(save_password));

    if(wifi_lock(portMAX_DELAY)) {
        s_ctx.pending_save = false;
        s_ctx.has_pending_credentials = false;
        memset(s_ctx.pending_password, 0, sizeof(s_ctx.pending_password));
        wifi_unlock();
    }

    ESP_LOGI(TAG, "connected, IP=%s", s_ctx.snapshot.ip);
    wifi_manager_start_sntp();
}

static void handle_disconnected(uint16_t reason)
{
    wifi_manager_error_t error = classify_disconnect_reason(reason);
    bool ignore;
    bool enabled;
    bool desired;

    stop_connect_timeout();

    if(!wifi_lock(portMAX_DELAY)) {
        return;
    }
    ignore = s_ctx.ignore_next_disconnect;
    s_ctx.ignore_next_disconnect = false;
    enabled = s_ctx.snapshot.enabled;
    desired = s_ctx.desired_connected;
    s_ctx.snapshot.connected = false;
    s_ctx.snapshot.ip[0] = '\0';
    s_ctx.snapshot.rssi = -127;
    s_ctx.snapshot.disconnect_reason = reason;
    s_ctx.snapshot.revision++;
    wifi_unlock();

    if(ignore) {
        return;
    }
    if(!enabled) {
        if(wifi_lock(portMAX_DELAY)) {
            set_state_locked(WIFI_MANAGER_STATE_DISABLED, WIFI_MANAGER_ERROR_NONE);
            wifi_unlock();
        }
        return;
    }
    if(!desired) {
        if(wifi_lock(portMAX_DELAY)) {
            set_state_locked(WIFI_MANAGER_STATE_IDLE, WIFI_MANAGER_ERROR_NONE);
            wifi_unlock();
        }
        return;
    }

    /* 保存网络即使遇到 AUTH_FAIL/握手超时也继续退避重试。
     * 路由器刚启动、信道切换或短时干扰同样可能上报认证类原因；
     * 这里若清除 desired_connected，设备会永久停在失败态直到重启。 */
    ESP_LOGW(TAG, "disconnected: reason=%u error=%d, scheduling reconnect",
             (unsigned)reason, (int)error);
    schedule_reconnect(error);
}

static void wifi_worker(void *arg)
{
    (void)arg;
    manager_event_t event;

    while(true) {
        if(xQueueReceive(s_event_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch(event.id) {
        case MANAGER_EVENT_STA_START:
            if(wifi_lock(portMAX_DELAY)) {
                s_ctx.snapshot.driver_started = true;
                s_ctx.snapshot.revision++;
                wifi_unlock();
            }
            (void)connect_now();
            break;

        case MANAGER_EVENT_STA_CONNECTED:
            if(wifi_lock(portMAX_DELAY)) {
                set_state_locked(WIFI_MANAGER_STATE_WAITING_IP,
                                 WIFI_MANAGER_ERROR_NONE);
                wifi_unlock();
            }
            break;

        case MANAGER_EVENT_STA_DISCONNECTED:
            handle_disconnected(event.data.disconnect_reason);
            break;

        case MANAGER_EVENT_SCAN_DONE:
            handle_scan_done();
            break;

        case MANAGER_EVENT_GOT_IP:
            handle_got_ip(&event.data.ip);
            break;

        case MANAGER_EVENT_RECONNECT_TIMER:
            (void)connect_now();
            break;

        case MANAGER_EVENT_CONNECT_TIMEOUT: {
            wifi_manager_error_t error = WIFI_MANAGER_ERROR_TIMEOUT;
            esp_err_t disconnect_err;
            bool timeout_is_current = false;
            if(wifi_lock(portMAX_DELAY)) {
                if(s_ctx.snapshot.state == WIFI_MANAGER_STATE_WAITING_IP) {
                    error = WIFI_MANAGER_ERROR_DHCP;
                }
                timeout_is_current =
                    s_ctx.snapshot.state == WIFI_MANAGER_STATE_CONNECTING ||
                    s_ctx.snapshot.state == WIFI_MANAGER_STATE_WAITING_IP;
                if(timeout_is_current) {
                    s_ctx.ignore_next_disconnect = true;
                }
                wifi_unlock();
            }
            if(!timeout_is_current) {
                break;
            }
            disconnect_err = esp_wifi_disconnect();
            if(disconnect_err != ESP_OK && wifi_lock(portMAX_DELAY)) {
                s_ctx.ignore_next_disconnect = false;
                wifi_unlock();
            }
            schedule_reconnect(error);
            break;
        }
        }
    }
}

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;
    manager_event_t event;
    memset(&event, 0, sizeof(event));

    if(event_base == WIFI_EVENT) {
        switch(event_id) {
        case WIFI_EVENT_STA_START:
            event.id = MANAGER_EVENT_STA_START;
            break;
        case WIFI_EVENT_STA_CONNECTED:
            event.id = MANAGER_EVENT_STA_CONNECTED;
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            event.id = MANAGER_EVENT_STA_DISCONNECTED;
            event.data.disconnect_reason =
                ((wifi_event_sta_disconnected_t *)event_data)->reason;
            break;
        case WIFI_EVENT_SCAN_DONE:
            event.id = MANAGER_EVENT_SCAN_DONE;
            break;
        default:
            return;
        }
    } else if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        event.id = MANAGER_EVENT_GOT_IP;
        event.data.ip = ((ip_event_got_ip_t *)event_data)->ip_info.ip;
    } else {
        return;
    }

    post_manager_event(&event);
}

esp_err_t wifi_manager_init(void)
{
    esp_err_t err;
    bool enabled;

    if(s_ctx.snapshot.initialized) {
        return ESP_OK;
    }

    wifi_manager_configure_timezone();

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.snapshot.rssi = -127;
    s_ctx.snapshot.state = WIFI_MANAGER_STATE_DISABLED;

    s_mutex = xSemaphoreCreateMutex();
    s_event_queue = xQueueCreate(WIFI_EVENT_QUEUE_LENGTH, sizeof(manager_event_t));
    if(s_mutex == NULL || s_event_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    nvs_load_configuration(&enabled,
                           s_ctx.saved_ssid,
                           sizeof(s_ctx.saved_ssid),
                           s_ctx.saved_password,
                           sizeof(s_ctx.saved_password));
    s_ctx.snapshot.has_saved_network = s_ctx.saved_ssid[0] != '\0';
    if(s_ctx.snapshot.has_saved_network && !enabled) {
        enabled = true;
        err = nvs_store_enabled(true);
        if(err != ESP_OK) {
            ESP_LOGW(TAG, "could not persist boot auto-connect: %s",
                     esp_err_to_name(err));
        }
    }
#if WIFI_DIAG_FORCE_OFF
    enabled = false;
    ESP_LOGW(TAG, "Wi-Fi radio force-disabled for display-jitter A/B test");
#endif
    s_ctx.snapshot.enabled = enabled;
    s_ctx.desired_connected = s_ctx.snapshot.has_saved_network;
    copy_string(s_ctx.snapshot.ssid,
                sizeof(s_ctx.snapshot.ssid),
                s_ctx.saved_ssid);
    copy_string(s_ctx.snapshot.saved_ssid,
                sizeof(s_ctx.snapshot.saved_ssid),
                s_ctx.saved_ssid);

    err = esp_netif_init();
    if(err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if(err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    if(esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&config);
    if(err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if(err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if(err != ESP_OK) {
        return err;
    }

    err = esp_event_handler_instance_register(WIFI_EVENT,
                                              ESP_EVENT_ANY_ID,
                                              wifi_event_handler,
                                              NULL,
                                              &s_wifi_event_instance);
    if(err != ESP_OK) {
        return err;
    }
    err = esp_event_handler_instance_register(IP_EVENT,
                                              IP_EVENT_STA_GOT_IP,
                                              wifi_event_handler,
                                              NULL,
                                              &s_ip_event_instance);
    if(err != ESP_OK) {
        return err;
    }

    const esp_timer_create_args_t reconnect_args = {
        .callback = reconnect_timer_callback,
        .name = "wifi_retry",
    };
    const esp_timer_create_args_t timeout_args = {
        .callback = connect_timeout_callback,
        .name = "wifi_timeout",
    };
    err = esp_timer_create(&reconnect_args, &s_reconnect_timer);
    if(err != ESP_OK) {
        return err;
    }
    err = esp_timer_create(&timeout_args, &s_connect_timeout_timer);
    if(err != ESP_OK) {
        return err;
    }

    if(xTaskCreate(wifi_worker,
                   "wifi_manager",
                   WIFI_WORKER_STACK_SIZE,
                   NULL,
                   WIFI_WORKER_PRIORITY,
                   &s_worker_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    if(s_ctx.snapshot.has_saved_network) {
        err = apply_credentials(s_ctx.saved_ssid, s_ctx.saved_password);
        if(err != ESP_OK) {
            return err;
        }
    }

    s_ctx.snapshot.initialized = true;
    s_ctx.snapshot.state = enabled ? WIFI_MANAGER_STATE_IDLE
                                   : WIFI_MANAGER_STATE_DISABLED;
    s_ctx.snapshot.revision++;

    if(enabled) {
        err = esp_wifi_start();
        if(err != ESP_OK) {
            return err;
        }
        configure_radio_power();
    }

    ESP_LOGI(TAG, "initialized: enabled=%d saved_network=%d",
             enabled,
             s_ctx.snapshot.has_saved_network);
    return ESP_OK;
}

esp_err_t wifi_manager_set_enabled(bool enabled)
{
    bool was_started;
    esp_err_t err;

    if(!s_ctx.snapshot.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    err = nvs_store_enabled(enabled);
    if(err != ESP_OK) {
        return err;
    }

    if(!wifi_lock(portMAX_DELAY)) {
        return ESP_ERR_TIMEOUT;
    }
    was_started = s_ctx.snapshot.driver_started;
    s_ctx.snapshot.enabled = enabled;
    s_ctx.desired_connected = enabled && s_ctx.snapshot.has_saved_network;
    s_ctx.snapshot.retry_count = 0U;
    s_ctx.snapshot.error = WIFI_MANAGER_ERROR_NONE;
    s_ctx.snapshot.revision++;
    if(!enabled) {
        s_ctx.snapshot.connected = false;
        s_ctx.snapshot.ip[0] = '\0';
        set_state_locked(WIFI_MANAGER_STATE_DISABLED, WIFI_MANAGER_ERROR_NONE);
    } else {
        set_state_locked(WIFI_MANAGER_STATE_IDLE, WIFI_MANAGER_ERROR_NONE);
    }
    wifi_unlock();

    (void)esp_timer_stop(s_reconnect_timer);
    stop_connect_timeout();

    if(enabled) {
        if(s_ctx.snapshot.has_saved_network) {
            err = apply_credentials(s_ctx.saved_ssid, s_ctx.saved_password);
            if(err != ESP_OK) {
                return err;
            }
        }
        if(!was_started) {
            err = esp_wifi_start();
            if(err == ESP_OK) {
                configure_radio_power();
            }
            return err;
        }
        if(s_ctx.snapshot.has_saved_network) {
            return connect_now();
        }
        return ESP_OK;
    }

    if(was_started) {
        err = esp_wifi_stop();
        if(wifi_lock(portMAX_DELAY)) {
            s_ctx.snapshot.driver_started = false;
            s_ctx.snapshot.revision++;
            wifi_unlock();
        }
        return err;
    }
    return ESP_OK;
}

esp_err_t wifi_manager_request_scan(void)
{
    wifi_manager_state_t previous_state;

    if(!s_ctx.snapshot.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if(!wifi_lock(portMAX_DELAY)) {
        return ESP_ERR_TIMEOUT;
    }
    if(!s_ctx.snapshot.enabled || !s_ctx.snapshot.driver_started ||
       s_ctx.snapshot.state == WIFI_MANAGER_STATE_SCANNING ||
       s_ctx.snapshot.state == WIFI_MANAGER_STATE_CONNECTING ||
       s_ctx.snapshot.state == WIFI_MANAGER_STATE_WAITING_IP) {
        wifi_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    previous_state = s_ctx.snapshot.state;
    s_ctx.state_before_scan = previous_state;
    set_state_locked(WIFI_MANAGER_STATE_SCANNING, WIFI_MANAGER_ERROR_NONE);
    wifi_unlock();

    esp_err_t err = esp_wifi_scan_start(NULL, false);
    if(err != ESP_OK && wifi_lock(portMAX_DELAY)) {
        set_state_locked(previous_state, WIFI_MANAGER_ERROR_INTERNAL);
        wifi_unlock();
    }
    return err;
}

size_t wifi_manager_get_scan_results(wifi_manager_ap_t *items, size_t capacity)
{
    size_t count;
    if(items == NULL || capacity == 0U || !wifi_lock(portMAX_DELAY)) {
        return 0U;
    }

    count = s_ctx.ap_count < capacity ? s_ctx.ap_count : capacity;
    memcpy(items, s_ctx.aps, count * sizeof(items[0]));
    wifi_unlock();
    return count;
}

esp_err_t wifi_manager_connect(const char *ssid,
                               const char *password,
                               bool save_after_success)
{
    bool should_disconnect;
    esp_err_t err;

    if(!s_ctx.snapshot.initialized || !ssid_is_valid(ssid) ||
       !password_is_valid(password)) {
        return ESP_ERR_INVALID_ARG;
    }

    if(!s_ctx.snapshot.enabled) {
        err = wifi_manager_set_enabled(true);
        if(err != ESP_OK) {
            return err;
        }
    }

    if(!wifi_lock(portMAX_DELAY)) {
        return ESP_ERR_TIMEOUT;
    }
    should_disconnect = s_ctx.snapshot.connected ||
                        s_ctx.snapshot.state == WIFI_MANAGER_STATE_CONNECTING ||
                        s_ctx.snapshot.state == WIFI_MANAGER_STATE_WAITING_IP;
    s_ctx.desired_connected = true;
    s_ctx.pending_save = save_after_success;
    s_ctx.has_pending_credentials = true;
    s_ctx.snapshot.retry_count = 0U;
    s_ctx.snapshot.disconnect_reason = 0U;
    copy_string(s_ctx.pending_ssid, sizeof(s_ctx.pending_ssid), ssid);
    copy_string(s_ctx.pending_password, sizeof(s_ctx.pending_password),
                password != NULL ? password : "");
    copy_string(s_ctx.snapshot.ssid, sizeof(s_ctx.snapshot.ssid), ssid);
    set_state_locked(WIFI_MANAGER_STATE_CONNECTING, WIFI_MANAGER_ERROR_NONE);
    wifi_unlock();

    (void)esp_timer_stop(s_reconnect_timer);
    stop_connect_timeout();
    (void)esp_wifi_scan_stop();

    if(should_disconnect) {
        if(wifi_lock(portMAX_DELAY)) {
            s_ctx.ignore_next_disconnect = true;
            wifi_unlock();
        }
        err = esp_wifi_disconnect();
        if(err != ESP_OK && wifi_lock(portMAX_DELAY)) {
            s_ctx.ignore_next_disconnect = false;
            wifi_unlock();
        }
    }

    err = apply_credentials(ssid, password != NULL ? password : "");
    if(err != ESP_OK) {
        if(wifi_lock(portMAX_DELAY)) {
            set_state_locked(WIFI_MANAGER_STATE_FAILED,
                             WIFI_MANAGER_ERROR_INTERNAL);
            wifi_unlock();
        }
        return err;
    }

    return connect_now();
}

esp_err_t wifi_manager_disconnect(void)
{
    bool connected;

    if(!s_ctx.snapshot.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if(!wifi_lock(portMAX_DELAY)) {
        return ESP_ERR_TIMEOUT;
    }
    connected = s_ctx.snapshot.connected ||
                s_ctx.snapshot.state == WIFI_MANAGER_STATE_CONNECTING ||
                s_ctx.snapshot.state == WIFI_MANAGER_STATE_WAITING_IP;
    s_ctx.desired_connected = false;
    s_ctx.pending_save = false;
    s_ctx.has_pending_credentials = false;
    s_ctx.snapshot.connected = false;
    s_ctx.snapshot.ip[0] = '\0';
    memset(s_ctx.pending_password, 0, sizeof(s_ctx.pending_password));
    set_state_locked(WIFI_MANAGER_STATE_IDLE, WIFI_MANAGER_ERROR_NONE);
    if(connected) {
        s_ctx.ignore_next_disconnect = true;
    }
    wifi_unlock();

    (void)esp_timer_stop(s_reconnect_timer);
    stop_connect_timeout();
    if(!connected) {
        return ESP_OK;
    }

    esp_err_t err = esp_wifi_disconnect();
    if(err != ESP_OK && wifi_lock(portMAX_DELAY)) {
        s_ctx.ignore_next_disconnect = false;
        wifi_unlock();
    }
    return err == ESP_ERR_WIFI_NOT_CONNECT ? ESP_OK : err;
}

esp_err_t wifi_manager_reconnect(void)
{
    char ssid[WIFI_MANAGER_SSID_MAX_LEN + 1U];
    char password[WIFI_MANAGER_PASSWORD_MAX_LEN + 1U];
    bool save_after_success = false;

    if(!s_ctx.snapshot.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if(!wifi_lock(portMAX_DELAY)) {
        return ESP_ERR_TIMEOUT;
    }
    if(s_ctx.has_pending_credentials) {
        copy_string(ssid, sizeof(ssid), s_ctx.pending_ssid);
        copy_string(password, sizeof(password), s_ctx.pending_password);
        save_after_success = s_ctx.pending_save;
    } else {
        copy_string(ssid, sizeof(ssid), s_ctx.saved_ssid);
        copy_string(password, sizeof(password), s_ctx.saved_password);
    }
    wifi_unlock();

    if(ssid[0] == '\0') {
        memset(password, 0, sizeof(password));
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t err = wifi_manager_connect(ssid, password, save_after_success);
    memset(password, 0, sizeof(password));
    return err;
}

esp_err_t wifi_manager_forget_network(void)
{
    esp_err_t err = wifi_manager_disconnect();
    if(err != ESP_OK) {
        return err;
    }

    err = nvs_clear_credentials();
    if(err != ESP_OK) {
        return err;
    }

    if(wifi_lock(portMAX_DELAY)) {
        s_ctx.saved_ssid[0] = '\0';
        memset(s_ctx.saved_password, 0, sizeof(s_ctx.saved_password));
        s_ctx.snapshot.has_saved_network = false;
        s_ctx.snapshot.ssid[0] = '\0';
        s_ctx.snapshot.saved_ssid[0] = '\0';
        s_ctx.snapshot.revision++;
        wifi_unlock();
    }
    return ESP_OK;
}

void wifi_manager_get_snapshot(wifi_manager_snapshot_t *snapshot)
{
    if(snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->rssi = -127;
    if(wifi_lock(pdMS_TO_TICKS(100U))) {
        *snapshot = s_ctx.snapshot;
        wifi_unlock();
    }
}

wifi_manager_state_t wifi_manager_get_state(void)
{
    wifi_manager_snapshot_t snapshot;
    wifi_manager_get_snapshot(&snapshot);
    return snapshot.state;
}

bool wifi_manager_is_connected(void)
{
    wifi_manager_snapshot_t snapshot;
    wifi_manager_get_snapshot(&snapshot);
    return snapshot.connected;
}

esp_err_t wifi_manager_get_ip(char *buf, size_t len)
{
    if(buf == NULL || len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_manager_snapshot_t snapshot;
    wifi_manager_get_snapshot(&snapshot);
    copy_string(buf, len, snapshot.ip);
    return ESP_OK;
}

esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password)
{
    if(ssid == NULL || ssid[0] == '\0') {
        return wifi_manager_forget_network();
    }
    return wifi_manager_connect(ssid, password, true);
}
