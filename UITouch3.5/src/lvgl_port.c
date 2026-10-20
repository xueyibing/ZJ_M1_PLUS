#include "lvgl_port.h"
#include "lcd.h"
#include "touch.h"
#include "gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "lvgl_port";

/* LVGL task */
#define LVGL_TASK_STACK     (16 * 1024)
#define LVGL_TASK_PRIO      5
/* tskNO_AFFINITY: 允许调度器在任一核上运行 LVGL 任务。之前钉死核 1 时，
 * 重帧（双整屏图合成）只能用一半 CPU；解除绑定后渲染可与另一核上的
 * 系统任务并行，且不与 WiFi（若启用）抢占同一核心。 */
#define LVGL_TASK_CORE      tskNO_AFFINITY

static SemaphoreHandle_t s_mux   = NULL;
static lv_display_t     *s_disp  = NULL;
static lv_indev_t       *s_indev = NULL;
static bool              s_wait_te_before_flush = true;
static bool              s_async_flush = false;

/* 历史教训（08-20）：曾把整帧推屏剥离到独立任务与渲染并行（流水线
 * 异步化），但推屏的 bounce memcpy（PSRAM→SRAM）与渲染线程的读写
 * 争抢 PSRAM 带宽，memcpy 带宽被挤压后跑不赢面板扫描线，画面出现
 * 撕裂带。I80 整帧推屏必须与渲染串行、独占总线才能保证无撕裂。 */

/* Invoked from the LCD color-transfer ISR when a strip finishes streaming. */
static void lvgl_flush_done_cb(void *ctx)
{
    (void)ctx;
    if(s_disp != NULL) {
        lv_display_flush_ready(s_disp);
    }
}

static void lv_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(1);
}

static void tick_timer_start(void)
{
    esp_timer_handle_t timer;
    const esp_timer_create_args_t args = {
        .callback = lv_tick_cb,
        .name     = "lv_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 1000));
}

static int32_t clamp_i32(int32_t value, int32_t min, int32_t max)
{
    if(value < min) {
        return min;
    }
    if(value > max) {
        return max;
    }
    return value;
}

#if UI_SW_ROTATE_180
/* The panel ignores MADCTL mirroring, so rotation is mandatory. Keep this
 * small hot loop in IRAM and process four swaps per iteration to reduce the
 * cost without changing the visible orientation or touch mapping. */
static void IRAM_ATTR rotate_180_rgb565(uint16_t *pixels, size_t count)
{
    size_t i = 0;
    size_t j;

    if(count < 2U) {
        return;
    }
    j = count - 1U;

    while(i <= j && (j - i + 1U) >= 8U) {
        uint16_t t0 = pixels[i];
        uint16_t t1 = pixels[i + 1U];
        uint16_t t2 = pixels[i + 2U];
        uint16_t t3 = pixels[i + 3U];
        pixels[i] = pixels[j];
        pixels[i + 1U] = pixels[j - 1U];
        pixels[i + 2U] = pixels[j - 2U];
        pixels[i + 3U] = pixels[j - 3U];
        pixels[j] = t0;
        pixels[j - 1U] = t1;
        pixels[j - 2U] = t2;
        pixels[j - 3U] = t3;
        i += 4U;
        j -= 4U;
    }

    while(i < j) {
        uint16_t t = pixels[i];
        pixels[i] = pixels[j];
        pixels[j] = t;
        i++;
        j--;
    }
}
#endif

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint16_t *src = (uint16_t *)px_map;
    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;
    bool is_last = lv_display_flush_is_last(disp);

    if(s_async_flush) {
        /* 内部 SRAM 条带回退路径（不常走）：维持原有 TE 帧首对齐 +
         * ISR 回调直接 flush_ready 的异步流。 */
        if(s_wait_te_before_flush) {
            (void)lcd_wait_for_te(25);
            s_wait_te_before_flush = false;
        }
        if(is_last) {
            s_wait_te_before_flush = true;
        }
        /* Non-blocking: the color-transfer callback signals flush-ready, so the
         * strips stream back-to-back inside one TE window (tear-free + fast). */
        lcd_flush_area_async(x1, y1, x2 + 1, y2 + 1, src);
        return;
    }

    /* PSRAM 整帧主路径（同步推屏）：帧首等 TE 对齐，随后 bounce 推屏
     * 全程独占 PSRAM 总线，确保 memcpy 跑赢面板扫描线（无撕裂前提）。
     * 勿改成与渲染并行的异步任务，见文件头部历史教训。 */
    {
        if(s_wait_te_before_flush) {
            (void)lcd_wait_for_te(25);
            s_wait_te_before_flush = false;
        }
        if(is_last) {
            s_wait_te_before_flush = true;
        }

        lcd_flush_area(x1, y1, x2 + 1, y2 + 1, src);

        /* 同步推屏已完成，必须就地上报 flush_ready 清除 flushing 标志；
         * 漏掉会导致 LVGL 下一帧在 wait_for_flushing 忙转（实测：仅首帧
         * 完成后永久卡死，IDLE0 饿死触发 task_wdt）。 */
        lv_display_flush_ready(disp);
    }
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint16_t x, y;

    if(tp_get_point(&x, &y)) {
#if UI_SW_ROTATE_180
        /* Match the software-rotated display: flip both touch axes. */
        x = (uint16_t)(UI_H_RES - 1 - x);
        y = (uint16_t)(UI_V_RES - 1 - y);
#endif
        data->point.x = clamp_i32((int32_t)x, 0, UI_H_RES - 1);
        data->point.y = clamp_i32((int32_t)y, 0, UI_V_RES - 1);
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state   = LV_INDEV_STATE_RELEASED;
    }
}

static void lvgl_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "LVGL task started");

    while(1) {
        xSemaphoreTakeRecursive(s_mux, portMAX_DELAY);
        uint32_t sleep_ms = lv_task_handler();
        xSemaphoreGiveRecursive(s_mux);

        if(sleep_ms < 2) {
            sleep_ms = 2;
        }
        if(sleep_ms > 100) {
            sleep_ms = 100;
        }
        vTaskDelay(pdMS_TO_TICKS(sleep_ms));
    }
}

esp_err_t lvgl_port_init(void)
{
    size_t buf_bytes;
    void *buf1;
    void *buf2;
    BaseType_t ret;

    s_mux = xSemaphoreCreateRecursiveMutex();
    if(!s_mux) {
        ESP_LOGE(TAG, "mutex create failed");
        return ESP_ERR_NO_MEM;
    }

    lv_init();
    tick_timer_start();
    ESP_LOGI(TAG, "LVGL %d.%d.%d initialized",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    /* Full-frame double buffers in PSRAM: the whole frame is rendered first
     * and only then pushed over I80, so the panel write pointer never stalls
     * mid-frame. When the write starts at the TE edge and the bounce memcpy
     * (PSRAM -> internal SRAM) outruns the panel scan, the frame stays
     * gap-less and tear-free even if each animation frame is expensive to
     * render (e.g. two full-screen images composited during a swipe).
     * Internal-SRAM strip buffers remain only as a fallback: their async
     * streaming assumes per-strip rendering is faster than one strip's DMA
     * transfer, which heavy full-screen animations violate, producing a
     * visible tear band mid-screen. */
    static const int k_try_lines[] = { 32, 28, 24, 20, 16 };
    bool used_internal = false;
    buf1 = NULL;
    buf2 = NULL;
    buf_bytes = (size_t)UI_H_RES * (size_t)UI_V_RES * sizeof(uint16_t);
    buf1 = lcd_alloc_draw_buffer(buf_bytes, true);
    buf2 = lcd_alloc_draw_buffer(buf_bytes, true);
    if(buf1 != NULL && buf2 != NULL) {
        ESP_LOGI(TAG, "Draw buffer: 2 x %u bytes PSRAM full-frame (TE aligned)",
                 (unsigned)buf_bytes);
    } else {
        free(buf1);
        free(buf2);
        buf1 = NULL;
        buf2 = NULL;
        for(size_t i = 0; i < sizeof(k_try_lines) / sizeof(k_try_lines[0]); i++) {
            size_t bytes = (size_t)UI_H_RES * (size_t)k_try_lines[i] * sizeof(uint16_t);
            buf1 = lcd_alloc_draw_buffer(bytes, false);
            buf2 = lcd_alloc_draw_buffer(bytes, false);
            if(buf1 != NULL && buf2 != NULL) {
                buf_bytes = bytes;
                used_internal = true;
                ESP_LOGI(TAG, "Draw buffer: 2 x %u bytes internal SRAM (%d lines)",
                         (unsigned)bytes, k_try_lines[i]);
                break;
            }
            free(buf1);
            free(buf2);
            buf1 = NULL;
            buf2 = NULL;
        }
    }
    if(buf1 == NULL || buf2 == NULL) {
        ESP_LOGE(TAG, "draw buffer alloc failed");
        free(buf1);
        free(buf2);
        return ESP_ERR_NO_MEM;
    }

    s_disp = lv_display_create(UI_H_RES, UI_V_RES);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_display_set_buffers(s_disp, buf1, buf2, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    /* Internal-SRAM buffers are DMA-capable, so stream them asynchronously and
     * let the transfer ISR report flush-ready. PSRAM full-frame buffers are
     * pushed synchronously inside flush_cb so the bounce memcpy owns the PSRAM
     * bus exclusively and always outruns the panel scan (tear-free). */
    s_async_flush = used_internal;
    if(s_async_flush) {
        lcd_set_flush_done_cb(lvgl_flush_done_cb, NULL);
    }

    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, touch_read_cb);

    ret = xTaskCreatePinnedToCore(
        lvgl_task, "lvgl", LVGL_TASK_STACK, NULL,
        LVGL_TASK_PRIO, NULL, LVGL_TASK_CORE);
    if(ret != pdPASS) {
        ESP_LOGE(TAG, "LVGL task create failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LVGL port ready ui=%dx%d lcd=%dx%d", UI_H_RES, UI_V_RES, LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}

bool lvgl_port_lock(uint32_t timeout_ms)
{
    return xSemaphoreTakeRecursive(s_mux, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    xSemaphoreGiveRecursive(s_mux);
}

void lvgl_port_set_touch_long_press_time(uint16_t long_press_time_ms)
{
    if(s_indev == NULL) {
        return;
    }

    lv_indev_set_long_press_time(s_indev, long_press_time_ms);
}
