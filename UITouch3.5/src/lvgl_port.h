#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"

/**
 * @brief Initialize LVGL and attach the display + touch input devices.
 *
 *  Call order in app_main():
 *    1. lcd_init()          <- hardware
 *    2. tp_init()           <- hardware
 *    3. lvgl_port_init()    <- this function
 *
 *  After this returns, a background task is running lv_task_handler().
 *  Create UI widgets inside lvgl_port_lock / lvgl_port_unlock.
 */
esp_err_t lvgl_port_init(void);

/**
 * @brief Acquire the LVGL mutex before calling any lv_* API from a task
 *        other than the LVGL task itself.
 *
 * @param timeout_ms  Maximum wait time in ms. Pass portMAX_DELAY for forever.
 * @return true  if the lock was acquired.
 * @return false on timeout.
 */
bool lvgl_port_lock(uint32_t timeout_ms);

/**
 * @brief Release the LVGL mutex.
 */
void lvgl_port_unlock(void);

/**
 * @brief Set the touch input device long press threshold in milliseconds.
 */
void lvgl_port_set_touch_long_press_time(uint16_t long_press_time_ms);
