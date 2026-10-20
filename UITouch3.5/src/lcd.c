#include "lcd.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"
#include "esp_log.h"

static const char *TAG = "lcd";

static esp_lcd_i80_bus_handle_t s_i80_bus;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_flush_done;
static lcd_flush_done_cb_t s_flush_done_cb;
static void *s_flush_done_ctx;
static volatile bool s_async_mode;
static SemaphoreHandle_t s_te_edge;
static uint8_t *s_bounce[2];
static bool s_te_enabled;
static uint8_t s_te_timeout_count;
static st7796_vendor_config_t s_vendor_config;

#define LCD_TE_DISABLE_AFTER_TIMEOUTS 3U
#define LCD_I80_BOUNCE_ALIGN          64U

#if LCD_BUS_WIDTH != 8
#error "The manufacturer-validated ZX3D50 driver requires an 8-bit I80 bus"
#endif

/* Manufacturer ZX3D50CE02S-USRC tables. D0/D3 are sampled after reset to
 * select the screen hardware revision before the I80 peripheral owns them.
 * MADCTL(0x36)/COLMOD(0x3A) are intentionally omitted: the official esp_lcd_st7796
 * driver sends them from rgb_ele_order + swap_xy/mirror and bits_per_pixel, so
 * listing them here only triggers override warnings without changing the result. */
static const st7796_lcd_init_cmd_t s_st7796_init_v1[] = {
    {0x11, NULL, 0, 120},
    {0xF0, (uint8_t[]){0xC3}, 1, 0},
    {0xF0, (uint8_t[]){0x96}, 1, 0},
    {0xB4, (uint8_t[]){LCD_INVERSION_MODE}, 1, 0},
    {0xB5, (uint8_t[]){0x1E}, 1, 0},
    {0xB7, (uint8_t[]){0xC6}, 1, 0},
    {0xB9, (uint8_t[]){0x02, 0xE0}, 2, 0},
    {0xC0, (uint8_t[]){0x80, 0x16}, 2, 0},
    {0xC1, (uint8_t[]){0x19}, 1, 0},
    {0xC2, (uint8_t[]){0xA7}, 1, 0},
    {0xC5, (uint8_t[]){LCD_VCOM_V1}, 1, 0},
    {0xE8, (uint8_t[]){0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33}, 8, 0},
    {0xE0, (uint8_t[]){0xF0, 0x07, 0x0D, 0x04, 0x05, 0x14, 0x36, 0x54, 0x4C, 0x38, 0x13, 0x14, 0x2E, 0x34}, 14, 0},
    {0xE1, (uint8_t[]){0xF0, 0x10, 0x14, 0x0E, 0x0C, 0x08, 0x35, 0x44, 0x4C, 0x26, 0x10, 0x12, 0x2C, 0x32}, 14, 0},
    {0xF0, (uint8_t[]){0x3C}, 1, 0},
    {0xF0, (uint8_t[]){0x69}, 1, 120},
    {0x29, NULL, 0, 0},
    {0x21, NULL, 0, 0},
    {0x35, NULL, 0, 0},
};

static const st7796_lcd_init_cmd_t s_st7796_init_v2[] = {
    {0x11, NULL, 0, 120},
    {0xF0, (uint8_t[]){0xC3}, 1, 0},
    {0xF0, (uint8_t[]){0x96}, 1, 0},
    {0xB1, (uint8_t[]){0x90, 0x18}, 2, 0},
    {0xB4, (uint8_t[]){LCD_INVERSION_MODE}, 1, 0},
    {0xB7, (uint8_t[]){0xC6}, 1, 0},
    {0xB9, (uint8_t[]){0x02, 0xE0}, 2, 0},
    {0xC0, (uint8_t[]){0x00, 0x00}, 2, 0},
    {0xC1, (uint8_t[]){0x16}, 1, 0},
    {0xC6, (uint8_t[]){0x80}, 1, 0},
    {0xC2, (uint8_t[]){0xA7}, 1, 0},
    {0xC5, (uint8_t[]){0x00}, 1, 0},
    {0xE8, (uint8_t[]){0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33}, 8, 0},
    {0xE0, (uint8_t[]){0xF0, 0x09, 0x0F, 0x0D, 0x0D, 0x1C, 0x3D, 0x44, 0x55, 0x39, 0x18, 0x18, 0x36, 0x39}, 14, 0},
    {0xE1, (uint8_t[]){0xF0, 0x09, 0x0F, 0x09, 0x08, 0x01, 0x31, 0x33, 0x46, 0x09, 0x13, 0x13, 0x2A, 0x31}, 14, 0},
    {0xF0, (uint8_t[]){0x3C}, 1, 0},
    {0xF0, (uint8_t[]){0x69}, 1, 120},
    {0x21, NULL, 0, 0},
    {0x29, NULL, 0, 0},
};

static bool lcd_color_trans_done_cb(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx)
{
    BaseType_t need_yield = pdFALSE;
    (void)panel_io;
    (void)edata;
    (void)user_ctx;
    if(s_async_mode && s_flush_done_cb != NULL) {
        s_flush_done_cb(s_flush_done_ctx);
    } else if(s_flush_done != NULL) {
        xSemaphoreGiveFromISR(s_flush_done, &need_yield);
    }
    return need_yield == pdTRUE;
}

static void IRAM_ATTR lcd_te_isr(void *arg)
{
    BaseType_t need_yield = pdFALSE;
    (void)arg;
    if(s_te_edge != NULL) {
        xSemaphoreGiveFromISR(s_te_edge, &need_yield);
    }
    if(need_yield == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

/* Backlight PWM stays product-specific; the manufacturer BSP only drives a
 * fixed GPIO level and cannot provide the application's brightness setting. */
#define BL_LEDC_SPEED     LEDC_LOW_SPEED_MODE
#define BL_LEDC_TIMER     LEDC_TIMER_1
#define BL_LEDC_CH        LEDC_CHANNEL_1
#define BL_LEDC_DUTY_RES  LEDC_TIMER_10_BIT
#define BL_LEDC_MAX_DUTY  ((1U << 10) - 1U)
#define BL_LEDC_FREQ_HZ   5000

#ifndef LCD_BL_BRIGHTNESS_DEFAULT
#define LCD_BL_BRIGHTNESS_DEFAULT 100
#endif

static uint8_t s_bl_brightness = LCD_BL_BRIGHTNESS_DEFAULT;
static bool s_bl_on;

static void backlight_ledc_init(void)
{
#if PIN_LCD_BL >= 0
    const ledc_timer_config_t timer = {
        .speed_mode = BL_LEDC_SPEED,
        .timer_num = BL_LEDC_TIMER,
        .duty_resolution = BL_LEDC_DUTY_RES,
        .freq_hz = BL_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    const ledc_channel_config_t channel = {
        .speed_mode = BL_LEDC_SPEED,
        .channel = BL_LEDC_CH,
        .timer_sel = BL_LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = PIN_LCD_BL,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
#endif
}

static void backlight_apply(void)
{
#if PIN_LCD_BL >= 0
    uint32_t duty = 0;
    if(s_bl_on && s_bl_brightness > 0) {
        duty = s_bl_brightness >= 100
                   ? BL_LEDC_MAX_DUTY
                   : ((uint32_t)s_bl_brightness * BL_LEDC_MAX_DUTY) / 100U;
    }
#if LCD_BL_ACTIVE_LEVEL == 0
    duty = BL_LEDC_MAX_DUTY - duty;
#endif
    ESP_ERROR_CHECK(ledc_set_duty(BL_LEDC_SPEED, BL_LEDC_CH, duty));
    ESP_ERROR_CHECK(ledc_update_duty(BL_LEDC_SPEED, BL_LEDC_CH));
#endif
}

static void backlight_set_enabled(bool on)
{
    s_bl_on = on;
    backlight_apply();
}

static void lcd_board_reset(void)
{
    const gpio_config_t reset_cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = BIT64(PIN_LCD_RST),
    };
    ESP_ERROR_CHECK(gpio_config(&reset_cfg));
    ESP_ERROR_CHECK(gpio_set_level(PIN_LCD_RST, 0));
    vTaskDelay(pdMS_TO_TICKS(2));
    ESP_ERROR_CHECK(gpio_set_level(PIN_LCD_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(120));
}

static uint8_t lcd_detect_screen_version(void)
{
    const gpio_config_t detect_cfg = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = BIT64(PIN_LCD_D0) | BIT64(PIN_LCD_D3),
    };
    ESP_ERROR_CHECK(gpio_config(&detect_cfg));
    const uint8_t select = (uint8_t)((gpio_get_level(PIN_LCD_D0) << 4) |
                                     gpio_get_level(PIN_LCD_D3));
    ESP_LOGI(TAG, "ZX3D50 screen select=0x%02X (%s)", select,
             select == 0x11 ? "v1" : "v2");
    return select;
}

static void lcd_te_init(void)
{
#if PIN_LCD_TE >= 0
    const gpio_config_t te_cfg = {
        .pin_bit_mask = BIT64(PIN_LCD_TE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    s_te_edge = xSemaphoreCreateBinary();
    if(s_te_edge == NULL) {
        ESP_LOGW(TAG, "TE semaphore allocation failed; synchronization disabled");
        return;
    }
    ESP_ERROR_CHECK(gpio_config(&te_cfg));
    esp_err_t ret = gpio_install_isr_service(0);
    if(ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "TE ISR service failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_LCD_TE, lcd_te_isr, NULL));

    /* Apply TEON to both screen revisions. The vendor v2 init table omits it. */
    ret = esp_lcd_panel_io_tx_param(s_io, LCD_CMD_TEON, (uint8_t[]){0x00}, 1);
    if(ret != ESP_OK) {
        ESP_LOGW(TAG, "ST7796 TEON failed: %s", esp_err_to_name(ret));
        gpio_isr_handler_remove(PIN_LCD_TE);
        return;
    }
    s_te_timeout_count = 0;
    s_te_enabled = true;
    ESP_LOGI(TAG, "TE synchronization enabled on GPIO%d", PIN_LCD_TE);
#endif
}

static size_t lcd_rows_per_chunk(size_t line_bytes)
{
    if(line_bytes == 0 || line_bytes > LCD_I80_BOUNCE_BYTES) {
        return 0;
    }
    size_t rows = LCD_I80_BOUNCE_BYTES / line_bytes;
    size_t aligned = rows;
    while(aligned > 0 && ((aligned * line_bytes) % LCD_I80_BOUNCE_ALIGN) != 0) {
        --aligned;
    }
    return aligned > 0 ? aligned : rows;
}

static void lcd_drain_flush_sem(void)
{
    while(s_flush_done != NULL && xSemaphoreTake(s_flush_done, 0) == pdTRUE) {
    }
}

esp_err_t lcd_init(void)
{
    backlight_ledc_init();
    backlight_set_enabled(false);

    lcd_board_reset();
    const uint8_t screen_version = lcd_detect_screen_version();

    const gpio_config_t wr_idle = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = BIT64(PIN_LCD_WR),
    };
    ESP_RETURN_ON_ERROR(gpio_config(&wr_idle), TAG, "WR GPIO config failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(PIN_LCD_WR, 0), TAG, "WR idle failed");

    const esp_lcd_i80_bus_config_t bus_cfg = {
        .dc_gpio_num = PIN_LCD_RS,
        .wr_gpio_num = PIN_LCD_WR,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums = {LCD_DATA_GPIO_NUMS},
        .bus_width = LCD_BUS_WIDTH,
        .max_transfer_bytes = LCD_I80_BOUNCE_BYTES * 2U,
        .dma_burst_size = 64,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_i80_bus(&bus_cfg, &s_i80_bus), TAG,
                        "I80 bus init failed");

    const esp_lcd_panel_io_i80_config_t io_cfg = {
        .cs_gpio_num = GPIO_NUM_NC,
        .pclk_hz = LCD_PCLK_HZ,
        .trans_queue_depth = 2,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
        .flags.swap_color_bytes = LCD_SWAP_COLOR_BYTES,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i80(s_i80_bus, &io_cfg, &s_io), TAG,
                        "ST7796 panel IO failed");

    s_flush_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_flush_done != NULL, ESP_ERR_NO_MEM, TAG,
                        "flush semaphore allocation failed");
    const esp_lcd_panel_io_callbacks_t callbacks = {
        .on_color_trans_done = lcd_color_trans_done_cb,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(s_io, &callbacks, NULL),
                        TAG, "color callback registration failed");

    s_bounce[0] = esp_lcd_i80_alloc_draw_buffer(
        s_io, LCD_I80_BOUNCE_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    s_bounce[1] = esp_lcd_i80_alloc_draw_buffer(
        s_io, LCD_I80_BOUNCE_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(s_bounce[0] != NULL && s_bounce[1] != NULL,
                        ESP_ERR_NO_MEM, TAG, "I80 SRAM bounce allocation failed");

    const st7796_lcd_init_cmd_t *init_cmds = screen_version == 0x11
                                                 ? s_st7796_init_v1
                                                 : s_st7796_init_v2;
    const size_t init_count = screen_version == 0x11
                                  ? sizeof(s_st7796_init_v1) / sizeof(s_st7796_init_v1[0])
                                  : sizeof(s_st7796_init_v2) / sizeof(s_st7796_init_v2[0]);
    s_vendor_config.init_cmds = init_cmds;
    s_vendor_config.init_cmds_size = init_count;

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
        .vendor_config = &s_vendor_config,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7796(s_io, &panel_cfg, &s_panel), TAG,
                        "ST7796 panel create failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "ST7796 reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "ST7796 init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, 0, 0), TAG, "LCD gap failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, LCD_SWAP_XY), TAG,
                        "LCD swap failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, LCD_MIRROR_X, LCD_MIRROR_Y), TAG,
                        "LCD mirror failed");

    lcd_te_init();
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG,
                        "LCD display-on failed");
    backlight_set_enabled(true);

    ESP_LOGI(TAG,
             "ZX3D50 ST7796 %s ready, %dx%d I80=%dbit pclk=%dMHz bounce=2x%u",
             screen_version == 0x11 ? "v1" : "v2", LCD_H_RES, LCD_V_RES,
             LCD_BUS_WIDTH, LCD_PCLK_HZ / 1000000,
             (unsigned)LCD_I80_BOUNCE_BYTES);
    return ESP_OK;
}

void *lcd_alloc_draw_buffer(size_t size, bool use_psram)
{
    if(s_io == NULL || size == 0) {
        return NULL;
    }
    const uint32_t caps = use_psram
                              ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA)
                              : (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    return esp_lcd_i80_alloc_draw_buffer(s_io, size, caps);
}

bool lcd_wait_for_te(uint32_t timeout_ms)
{
    if(!s_te_enabled || s_te_edge == NULL) {
        return false;
    }
    xSemaphoreTake(s_te_edge, 0);
    if(xSemaphoreTake(s_te_edge, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        s_te_timeout_count = 0;
        return true;
    }
    if(++s_te_timeout_count >= LCD_TE_DISABLE_AFTER_TIMEOUTS) {
        s_te_enabled = false;
#if PIN_LCD_TE >= 0
        gpio_intr_disable(PIN_LCD_TE);
#endif
        ESP_LOGW(TAG, "No TE pulses on GPIO%d; synchronization disabled", PIN_LCD_TE);
    }
    return false;
}

void lcd_flush_area(int x1, int y1, int x2, int y2, const uint16_t *colors)
{
    const int width = x2 - x1;
    int remaining = y2 - y1;
    if(width <= 0 || remaining <= 0 || colors == NULL) {
        return;
    }

    const size_t line_bytes = (size_t)width * sizeof(uint16_t);
    const size_t rows_per_chunk = lcd_rows_per_chunk(line_bytes);
    if(rows_per_chunk == 0) {
        ESP_LOGE(TAG, "bounce too small for %d-pixel line", width);
        abort();
    }

    const uint8_t *src = (const uint8_t *)colors;
    int y = y1;
    int chunk_index = 0;
    lcd_drain_flush_sem();

    while(remaining > 0) {
        const int rows = remaining > (int)rows_per_chunk
                             ? (int)rows_per_chunk
                             : remaining;
        const size_t bytes = (size_t)rows * line_bytes;
        uint8_t *dst = s_bounce[chunk_index & 1];

        memcpy(dst, src, bytes);
        if(chunk_index > 0) {
            ESP_ERROR_CHECK(xSemaphoreTake(s_flush_done, portMAX_DELAY) == pdTRUE
                                ? ESP_OK
                                : ESP_FAIL);
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, x1, y, x2, y + rows, dst));

        src += bytes;
        y += rows;
        remaining -= rows;
        ++chunk_index;
    }

    ESP_ERROR_CHECK(xSemaphoreTake(s_flush_done, portMAX_DELAY) == pdTRUE
                        ? ESP_OK
                        : ESP_FAIL);
}

void lcd_set_flush_done_cb(lcd_flush_done_cb_t cb, void *user_ctx)
{
    s_flush_done_cb = cb;
    s_flush_done_ctx = user_ctx;
    s_async_mode = (cb != NULL);
}

void lcd_flush_area_async(int x1, int y1, int x2, int y2, const uint16_t *colors)
{
    if(x2 <= x1 || y2 <= y1 || colors == NULL) {
        if(s_flush_done_cb != NULL) {
            s_flush_done_cb(s_flush_done_ctx);
        }
        return;
    }
    /* Direct DMA from the internal-SRAM LVGL buffer; completion is reported
     * through the color-transfer callback. */
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, x1, y1, x2, y2, colors));
}

void lcd_set_backlight(bool on)
{
    backlight_set_enabled(on);
}

void lcd_set_brightness(uint8_t percent)
{
    s_bl_brightness = percent > 100 ? 100 : percent;
    backlight_apply();
}

uint8_t lcd_get_brightness(void)
{
    return s_bl_brightness;
}
