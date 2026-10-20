#include "uart_link.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "gpio.h"

#define UART_LINK_RX_BUFFER_SIZE   512
#define UART_LINK_TX_BUFFER_SIZE   512
#define UART_LINK_RX_CHUNK_SIZE    64
#define UART_LINK_RX_TASK_STACK    (6 * 1024)
#define UART_LINK_RX_TASK_PRIO     4
#define UART_LINK_TX_WAIT_MS       200
#define UART_LINK_PRINTF_BUFFER    160

static const char *TAG = "uart_link";

typedef struct {
    bool initialized;
    bool rx_task_running;
    SemaphoreHandle_t tx_mutex;
    TaskHandle_t rx_task;
    uart_link_rx_callback_t rx_callback;
    void *rx_user_data;
} uart_link_ctx_t;

static uart_link_ctx_t s_uart;

static uart_port_t uart_link_port(void)
{
    return (uart_port_t)UART_PORT_NUM;
}

static void uart_link_rx_task(void *arg)
{
    uint8_t rx_buffer[UART_LINK_RX_CHUNK_SIZE];

    (void)arg;

    while(s_uart.rx_task_running) {
        if(s_uart.rx_callback == NULL) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        int len = uart_read_bytes(uart_link_port(),
                                  rx_buffer,
                                  sizeof(rx_buffer),
                                  pdMS_TO_TICKS(100));
        if(len > 0 && s_uart.rx_callback != NULL) {
            s_uart.rx_callback(rx_buffer, (size_t)len, s_uart.rx_user_data);
        }
    }

    s_uart.rx_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t uart_link_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {
            .allow_pd = 0,
        },
    };
    BaseType_t task_ok;

    if(s_uart.initialized) {
        return ESP_OK;
    }

    memset(&s_uart, 0, sizeof(s_uart));

    s_uart.tx_mutex = xSemaphoreCreateMutex();
    if(s_uart.tx_mutex == NULL) {
        ESP_LOGE(TAG, "tx mutex create failed");
        return ESP_ERR_NO_MEM;
    }

    if(uart_driver_install(uart_link_port(),
                           UART_LINK_RX_BUFFER_SIZE,
                           UART_LINK_TX_BUFFER_SIZE,
                           0,
                           NULL,
                           0) != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed");
        vSemaphoreDelete(s_uart.tx_mutex);
        memset(&s_uart, 0, sizeof(s_uart));
        return ESP_FAIL;
    }

    if(uart_param_config(uart_link_port(), &uart_config) != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed");
        uart_driver_delete(uart_link_port());
        vSemaphoreDelete(s_uart.tx_mutex);
        memset(&s_uart, 0, sizeof(s_uart));
        return ESP_FAIL;
    }

    if(uart_set_pin(uart_link_port(),
                    UART_TX_PIN,
                    UART_RX_PIN,
                    UART_PIN_NO_CHANGE,
                    UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed");
        uart_driver_delete(uart_link_port());
        vSemaphoreDelete(s_uart.tx_mutex);
        memset(&s_uart, 0, sizeof(s_uart));
        return ESP_FAIL;
    }

    s_uart.rx_task_running = true;
    task_ok = xTaskCreate(uart_link_rx_task,
                          "uart_rx",
                          UART_LINK_RX_TASK_STACK,
                          NULL,
                          UART_LINK_RX_TASK_PRIO,
                          &s_uart.rx_task);
    if(task_ok != pdPASS) {
        s_uart.rx_task_running = false;
        uart_driver_delete(uart_link_port());
        vSemaphoreDelete(s_uart.tx_mutex);
        memset(&s_uart, 0, sizeof(s_uart));
        ESP_LOGE(TAG, "rx task create failed");
        return ESP_ERR_NO_MEM;
    }

    s_uart.initialized = true;
    ESP_LOGI(TAG,
             "UART ready port=%d baud=%d tx=%d rx=%d",
             UART_PORT_NUM,
             UART_BAUD_RATE,
             UART_TX_PIN,
             UART_RX_PIN);
    return ESP_OK;
}

void uart_link_deinit(void)
{
    if(!s_uart.initialized) {
        return;
    }

    s_uart.rx_task_running = false;
    vTaskDelay(pdMS_TO_TICKS(120));
    uart_wait_tx_done(uart_link_port(), pdMS_TO_TICKS(UART_LINK_TX_WAIT_MS));

    if(s_uart.rx_task != NULL) {
        vTaskDelete(s_uart.rx_task);
        s_uart.rx_task = NULL;
    }

    uart_driver_delete(uart_link_port());

    if(s_uart.tx_mutex != NULL) {
        vSemaphoreDelete(s_uart.tx_mutex);
    }

    memset(&s_uart, 0, sizeof(s_uart));
}

bool uart_link_is_ready(void)
{
    return s_uart.initialized;
}

void uart_link_set_rx_callback(uart_link_rx_callback_t callback, void *user_data)
{
    s_uart.rx_callback = callback;
    s_uart.rx_user_data = user_data;
}

esp_err_t uart_link_write(const void *data, size_t len)
{
    int written;

    if(!s_uart.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if((data == NULL && len > 0U) || s_uart.tx_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if(len == 0U) {
        return ESP_OK;
    }

    if(xSemaphoreTake(s_uart.tx_mutex, pdMS_TO_TICKS(UART_LINK_TX_WAIT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    written = uart_write_bytes(uart_link_port(), data, len);
    xSemaphoreGive(s_uart.tx_mutex);

    if(written < 0 || (size_t)written != len) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t uart_link_write_string(const char *text)
{
    if(text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return uart_link_write(text, strlen(text));
}

esp_err_t uart_link_writef(const char *fmt, ...)
{
    va_list args;
    int len;
    char buffer[UART_LINK_PRINTF_BUFFER];

    if(fmt == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    va_start(args, fmt);
    len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if(len < 0) {
        return ESP_FAIL;
    }
    if((size_t)len >= sizeof(buffer)) {
        ESP_LOGW(TAG, "uart message truncated");
    }

    return uart_link_write(buffer, strnlen(buffer, sizeof(buffer)));
}

esp_err_t uart_link_read(uint8_t *buffer, size_t buffer_size, size_t *out_len, uint32_t timeout_ms)
{
    int len;

    if(!s_uart.initialized || buffer == NULL || buffer_size == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    len = uart_read_bytes(uart_link_port(),
                          buffer,
                          buffer_size,
                          pdMS_TO_TICKS(timeout_ms));
    if(len < 0) {
        return ESP_FAIL;
    }

    if(out_len != NULL) {
        *out_len = (size_t)len;
    }

    return ESP_OK;
}
