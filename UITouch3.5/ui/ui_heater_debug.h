#pragma once

#include "ui_data.h"

#include <stdbool.h>

void ui_heater_debug_create(ui_data_t *data);
bool ui_heater_debug_handle_rx_line(const char *rx);
void ui_heater_debug_poll_if_needed(void);
