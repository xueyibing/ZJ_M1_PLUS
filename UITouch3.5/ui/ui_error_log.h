#pragma once

#include "ui_data.h"

/* 创建错误日志页面。进入后会向主控发送 ELG 查询命令拉取历史错误记录。 */
void ui_error_log_create(ui_data_t *data);
