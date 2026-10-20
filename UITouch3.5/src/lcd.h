#ifndef LCD_H
#define LCD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "gpio.h"   /* LCD_H_RES, LCD_V_RES */

/**
 * @brief Initialize the manufacturer-validated ST7796 LCD path.
 *        Must be called once before any other lcd_* function.
 */
esp_err_t lcd_init(void);

/**
 * @brief Allocate a DMA-aligned LCD draw buffer.
 *
 * Uses the alignment constraints reported by the actual I80 DMA channel.
 * External RAM is selected when @p use_psram is true.
 */
void *lcd_alloc_draw_buffer(size_t size, bool use_psram);

/**
 * @brief Fill a rectangular region with pixel data.
 *
 * @param x1      Left column  (inclusive)
 * @param y1      Top row      (inclusive)
 * @param x2      Right column (exclusive)
 * @param y2      Bottom row   (exclusive)
 * @param colors  RGB565 pixel buffer, row-major, (x2-x1)*(y2-y1) pixels.
 *                Buffer must remain valid until the color transfer completes.
 *
 * ── LVGL 9.x flush callback ────────────────────────────────────────────────
 *
 *   static void lvgl_flush_cb(lv_display_t *disp,
 *                             const lv_area_t *area,
 *                             uint8_t *px_map)
 *   {
 *       lcd_flush_area(area->x1, area->y1,
 *                      area->x2 + 1, area->y2 + 1,   // LVGL uses inclusive end
 *                      (uint16_t *)px_map);
 *       lv_display_flush_ready(disp);
 *   }
 *
 * ───────────────────────────────────────────────────────────────────────────
 */
void lcd_flush_area(int x1, int y1, int x2, int y2, const uint16_t *colors);

/** Async flush completion callback, invoked from the color-transfer ISR. */
typedef void (*lcd_flush_done_cb_t)(void *user_ctx);

/** Register a completion callback to enable async (non-blocking) flushing.
 *  Pass NULL to return to synchronous lcd_flush_area(). */
void lcd_set_flush_done_cb(lcd_flush_done_cb_t cb, void *user_ctx);

/** Non-blocking rectangular flush. @p colors must stay valid until the
 *  registered completion callback fires and must be DMA-capable (internal
 *  SRAM). x2/y2 are exclusive; the region must fit one I80 transfer. */
void lcd_flush_area_async(int x1, int y1, int x2, int y2, const uint16_t *colors);

/**
 * @brief Wait for the panel tearing-effect (vertical blank) edge.
 *
 * Returns false on timeout or when TE synchronization is unavailable. After
 * repeated timeouts the driver disables TE waiting and continues unsynced.
 */
bool lcd_wait_for_te(uint32_t timeout_ms);

/**
 * @brief Turn the backlight on or off.
 *        Re-enabling restores the last brightness set via lcd_set_brightness().
 */
void lcd_set_backlight(bool on);

/**
 * @brief Set backlight brightness in percent (0-100, clamped).
 *        Backlight defaults to 100 (maximum) at startup.
 */
void lcd_set_brightness(uint8_t percent);

/**
 * @brief Get the current backlight brightness in percent.
 */
uint8_t lcd_get_brightness(void);

#endif /* LCD_H */
