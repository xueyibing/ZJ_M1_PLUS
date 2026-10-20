#pragma once

#include "lvgl.h"
#include "ui_data.h"

/* Rebuild the main UI page. */
void ui_create(ui_data_t *data);

/* Forward state changes to the currently active UI modules. */
void ui_on_data_changed(void);
void ui_on_debug_state_changed(void);
