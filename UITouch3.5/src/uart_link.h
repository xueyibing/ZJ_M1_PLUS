#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * UART2 uses ASCII CRLF-delimited frames at 115200 8N1:
 *   REQ <id> <operation> [key=value ...]
 *   RSP <id> <operation> result=<...> code=<...> [key=value ...]
 *   EVT <seq> <event> [key=value ...]
 *
 * MainControl accepts only this protocol. The operation and field contract is
 * documented in ../docs/UART_PROTOCOL.md; legacy command lines are translated
 * by ui_uart_bridge and must never be written to this link directly.
 */

typedef void (*uart_link_rx_callback_t)(const uint8_t *data, size_t len, void *user_data);

esp_err_t uart_link_init(void);

void uart_link_set_rx_callback(uart_link_rx_callback_t callback, void *user_data);

esp_err_t uart_link_write(const void *data, size_t len);
esp_err_t uart_link_write_string(const char *text);
esp_err_t uart_link_writef(const char *fmt, ...);
