#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_wifi_types_generic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MANAGER_MAX_APS 20U
#define WIFI_MANAGER_SSID_MAX_LEN 32U
#define WIFI_MANAGER_PASSWORD_MAX_LEN 63U

typedef enum {
    WIFI_MANAGER_STATE_DISABLED = 0,
    WIFI_MANAGER_STATE_IDLE,
    WIFI_MANAGER_STATE_SCANNING,
    WIFI_MANAGER_STATE_CONNECTING,
    WIFI_MANAGER_STATE_WAITING_IP,
    WIFI_MANAGER_STATE_CONNECTED,
    WIFI_MANAGER_STATE_RETRY_WAIT,
    WIFI_MANAGER_STATE_FAILED,
} wifi_manager_state_t;

typedef enum {
    WIFI_MANAGER_ERROR_NONE = 0,
    WIFI_MANAGER_ERROR_NOT_FOUND,
    WIFI_MANAGER_ERROR_AUTH,
    WIFI_MANAGER_ERROR_WEAK_SIGNAL,
    WIFI_MANAGER_ERROR_DHCP,
    WIFI_MANAGER_ERROR_TIMEOUT,
    WIFI_MANAGER_ERROR_INTERNAL,
} wifi_manager_error_t;

typedef struct {
    char ssid[WIFI_MANAGER_SSID_MAX_LEN + 1U];
    int8_t rssi;
    wifi_auth_mode_t authmode;
    uint8_t channel;
} wifi_manager_ap_t;

typedef struct {
    wifi_manager_state_t state;
    wifi_manager_error_t error;
    bool initialized;
    bool enabled;
    bool driver_started;
    bool has_saved_network;
    bool connected;
    uint8_t retry_count;
    uint16_t disconnect_reason;
    uint32_t revision;
    char ssid[WIFI_MANAGER_SSID_MAX_LEN + 1U];
    char saved_ssid[WIFI_MANAGER_SSID_MAX_LEN + 1U];
    char ip[16];
    int8_t rssi;
} wifi_manager_snapshot_t;

/*
 * Initializes esp-netif, the default event loop and the Wi-Fi station driver.
 * NVS must already be initialized. The call is idempotent and does not block
 * while connecting.
 */
esp_err_t wifi_manager_init(void);

/* Enable or disable the station radio and persist that preference. */
esp_err_t wifi_manager_set_enabled(bool enabled);

/* Starts an asynchronous all-channel scan. */
esp_err_t wifi_manager_request_scan(void);

/* Copies the latest scan result snapshot. */
size_t wifi_manager_get_scan_results(wifi_manager_ap_t *items, size_t capacity);

/*
 * Tries credentials from RAM. If save_after_success is true they are written
 * to NVS only after an IP address has been obtained.
 */
esp_err_t wifi_manager_connect(const char *ssid,
                               const char *password,
                               bool save_after_success);

/* Disconnect without forgetting the saved network. No automatic retry. */
esp_err_t wifi_manager_disconnect(void);

/* Retry the pending/current/saved network immediately. */
esp_err_t wifi_manager_reconnect(void);

/* Disconnect and erase the saved SSID/password. */
esp_err_t wifi_manager_forget_network(void);

/* Thread-safe state snapshot for UI polling. */
void wifi_manager_get_snapshot(wifi_manager_snapshot_t *snapshot);

/* Compatibility helpers retained for existing call sites. */
wifi_manager_state_t wifi_manager_get_state(void);
bool wifi_manager_is_connected(void);
esp_err_t wifi_manager_get_ip(char *buf, size_t len);
esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif
