#include "touch.h"
#include "gpio.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"

static const char *TAG = "touch";

static i2c_master_bus_handle_t s_bus;
static esp_lcd_panel_io_handle_t s_touch_io;
static esp_lcd_touch_handle_t s_touch;

esp_err_t tp_init(void)
{
    if(PIN_TP_SDA < 0 || PIN_TP_SCL < 0) {
        ESP_LOGW(TAG, "Touch I2C pins are not connected");
        return ESP_ERR_NOT_SUPPORTED;
    }

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_TP_SDA,
        .scl_io_num = PIN_TP_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG,
                        "touch I2C bus failed");

    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    io_cfg.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_bus, &io_cfg, &s_touch_io), TAG,
                        "FT5x06 panel IO failed");

    /* Keep raw portrait coordinates here. The public wrapper below preserves
     * the product's tested landscape/180-degree mapping in one place. */
    const esp_lcd_touch_config_t touch_cfg = {
        .x_max = LCD_PHYS_W,
        .y_max = LCD_PHYS_H,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = PIN_TP_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(s_touch_io, &touch_cfg,
                                                      &s_touch), TAG,
                        "FT5x06 init failed");

#if PIN_TP_INT >= 0
    const gpio_config_t int_cfg = {
        .pin_bit_mask = BIT64(PIN_TP_INT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&int_cfg), TAG, "touch INT config failed");
#endif

    ESP_LOGI(TAG, "FT5x06/FT6636 ready, I2C=400kHz int=GPIO%d", PIN_TP_INT);
    return ESP_OK;
}

esp_err_t tp_read(tp_data_t *out)
{
    esp_lcd_touch_point_data_t points[TP_MAX_POINTS] = {0};
    uint8_t count = 0;

    if(out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    out->count = 0;
    if(s_touch == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_touch_read_data(s_touch), TAG,
                        "touch read failed");
    ESP_RETURN_ON_ERROR(esp_lcd_touch_get_data(s_touch, points, &count,
                                               TP_MAX_POINTS), TAG,
                        "touch data copy failed");

    if(count > TP_MAX_POINTS) {
        count = TP_MAX_POINTS;
    }
    out->count = count;
    for(uint8_t i = 0; i < count; ++i) {
        out->points[i].x = points[i].x;
        out->points[i].y = points[i].y;
        out->points[i].event = TP_EVENT_CONTACT;
        out->points[i].id = points[i].track_id;
    }
    return ESP_OK;
}

bool tp_get_point(uint16_t *x, uint16_t *y)
{
    tp_data_t td;
    if(tp_read(&td) != ESP_OK || td.count == 0) {
        return false;
    }

    const uint16_t px = td.points[0].x;
    const uint16_t py = td.points[0].y;
    uint16_t lx;
    uint16_t ly;

#if LCD_SWAP_XY
    /* 官方 ST7796 驱动下，swap_xy 后 mirror_x 翻的是显示 Y 轴，而触摸上报的 px
     * 方向与其相反，故 ly 相对 LCD_MIRROR_X 取反才能与画面一致。 */
    lx = LCD_MIRROR_Y ? (LCD_H_RES - 1U - py) : py;
    ly = LCD_MIRROR_X ? px : (LCD_V_RES - 1U - px);
#else
    lx = LCD_MIRROR_X ? (LCD_H_RES - 1U - px) : px;
    ly = LCD_MIRROR_Y ? (LCD_V_RES - 1U - py) : py;
#endif

    if(x != NULL) {
        *x = lx;
    }
    if(y != NULL) {
        *y = ly;
    }
    return true;
}
