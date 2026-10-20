#include "esp_log.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "nvs_flash.h"
#include "lcd.h"
#include "touch.h"
#include "data_manager.h"
#include "lvgl_port.h"
#include "buzzer.h"
#include "ui.h"
#include "ui_i18n.h"
#include "uart_link.h"
#include "ui_uart_bridge.h"
#include "wifi_manager.h"
#include "ds18b20.h"

static const char *TAG = "main";
static ui_data_t s_ui_data;

#define BOARD_MODEL          "ESP32-S3-WROOM-1-N16R2"
#define BOARD_FLASH_BYTES    (16U * 1024U * 1024U)
#define BOARD_PSRAM_BYTES    (2U * 1024U * 1024U)

#ifndef ZJ_M1_BOARD_ESP32S3_N16R2
#error "Select the project-local esp32-s3-n16r2 PlatformIO board"
#endif

#ifndef CONFIG_ESPTOOLPY_FLASHSIZE_16MB
#error "N16R2 requires CONFIG_ESPTOOLPY_FLASHSIZE_16MB"
#endif

#ifndef CONFIG_SPIRAM_MODE_QUAD
#error "N16R2 requires Quad SPI PSRAM mode"
#endif

static void board_resources_verify(void)
{
    uint32_t flash_bytes = 0;
    size_t psram_bytes;

    ESP_ERROR_CHECK(esp_flash_get_size(NULL, &flash_bytes));
    psram_bytes = esp_psram_get_size();
    ESP_LOGI(TAG, "board=%s flash=%uMB psram=%uMB (Quad)", BOARD_MODEL,
             (unsigned)(flash_bytes / (1024U * 1024U)),
             (unsigned)(psram_bytes / (1024U * 1024U)));

    if(flash_bytes < BOARD_FLASH_BYTES || psram_bytes < BOARD_PSRAM_BYTES) {
        ESP_LOGE(TAG, "hardware mismatch: expected flash>=16MB and psram>=2MB");
        abort();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "app_main start");
    board_resources_verify();

    esp_err_t err = nvs_flash_init();

    if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "nvs ready");

    ESP_ERROR_CHECK(data_manager_init());
    ESP_LOGI(TAG, "data manager ready");

    ui_i18n_init();
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_LOGI(TAG, "wifi manager ready");

    ESP_ERROR_CHECK(lcd_init());
    lcd_set_brightness(data_manager_get_brightness());
    ESP_LOGI(TAG, "lcd ready");

    err = tp_init();
    if(err == ESP_OK) {
        ESP_LOGI(TAG, "touch ready");
    } else {
        ESP_LOGW(TAG, "touch init skipped: %s", esp_err_to_name(err));
    }

    err = ds18b20_init();
    if(err == ESP_OK) {
        ESP_LOGI(TAG, "DS18B20 sampling started");
    } else {
        ESP_LOGW(TAG, "DS18B20 init skipped: %s", esp_err_to_name(err));
    }

    ESP_ERROR_CHECK(uart_link_init());
    ESP_LOGI(TAG, "uart link ready");

    ESP_ERROR_CHECK(buzzer_init());
    ESP_LOGI(TAG, "buzzer ready");

    ui_data_init(&s_ui_data);
    ui_data_set_listener(&s_ui_data, ui_uart_bridge_handle_event, NULL);
    ui_uart_bridge_bind_ui_data(&s_ui_data);

    ESP_ERROR_CHECK(lvgl_port_init());
    ESP_LOGI(TAG, "lvgl ready");

    if(lvgl_port_lock(1000)) {
        ui_create(&s_ui_data);
        lvgl_port_unlock();
    } else {
        ESP_LOGE(TAG, "Could not acquire LVGL lock");
    }

    ESP_ERROR_CHECK(ui_uart_bridge_init());
    ESP_LOGI(TAG, "UI created");
}
