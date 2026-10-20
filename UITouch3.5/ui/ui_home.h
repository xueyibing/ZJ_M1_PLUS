#pragma once

#include "lvgl.h"
#include "ui_data.h"

/* 煎蛋功能页面：主界面（数量/熟度控制）。 */
void ui_home_create(ui_data_t *data);
void ui_home_on_data_changed(void);
