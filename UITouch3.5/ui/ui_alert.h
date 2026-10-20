#pragma once

#include <stdbool.h>

#include "ui_data.h"

bool ui_alert_should_show(const ui_data_state_t *state);
void ui_alert_create(ui_data_t *data);
void ui_alert_create_preview(ui_data_t *data, ui_error_code_t code);
void ui_alert_on_data_changed(void);

/* 返回指定错误码的当前语言标题文案，供错误日志等页面复用。 */
const char *ui_alert_error_headline(ui_error_code_t code);
