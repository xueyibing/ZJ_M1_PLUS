#ifndef TOUCH_H
#define TOUCH_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* FT6636 I2C address */
#define FT6636_ADDR      0x38

/* Touch event flags */
#define TP_EVENT_PRESS   0x00
#define TP_EVENT_LIFT    0x01
#define TP_EVENT_CONTACT 0x02

#define TP_MAX_POINTS    2

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t  event;  /* TP_EVENT_* */
    uint8_t  id;
} tp_point_t;

typedef struct {
    uint8_t    count;                 /* 0 = no touch */
    tp_point_t points[TP_MAX_POINTS];
} tp_data_t;

/**
 * @brief Initialize the manufacturer FT5x06/FT6636 component and I2C bus.
 *        The shared GPIO 4 reset is handled by LCD board initialization.
 */
esp_err_t tp_init(void);

/**
 * @brief Read all current touch points into @p out.
 *        out->count == 0 means no finger on screen.
 */
esp_err_t tp_read(tp_data_t *out);

/**
 * @brief Simplified single-point read for LVGL (or any polling code).
 *
 * @param[out] x       X coordinate of first touch point
 * @param[out] y       Y coordinate of first touch point
 * @return true if at least one finger is pressing the screen, false otherwise.
 *
 * ── LVGL 9.x input-device callback ────────────────────────────────────────
 *
 *   static void lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
 *   {
 *       uint16_t x, y;
 *       if (tp_get_point(&x, &y)) {
 *           data->point.x = x;
 *           data->point.y = y;
 *           data->state   = LV_INDEV_STATE_PRESSED;
 *       } else {
 *           data->state   = LV_INDEV_STATE_RELEASED;
 *       }
 *   }
 *
 * ───────────────────────────────────────────────────────────────────────────
 */
bool tp_get_point(uint16_t *x, uint16_t *y);

#endif /* TOUCH_H */
