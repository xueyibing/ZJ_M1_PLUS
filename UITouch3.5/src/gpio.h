#ifndef GPIO_H
#define GPIO_H

/* Pin map selector: 1 = current 3.5-inch board schematic, 0 = custom dev board. */
#define QMZJ 0

#if QMZJ

/* Backlight */
#define PIN_LCD_BL            45
#define LCD_BL_ACTIVE_LEVEL   1

/* LCD 8080 control lines */
#define PIN_LCD_RST           4
#define PIN_LCD_RS            0
#define PIN_LCD_WR            47
#define PIN_LCD_TE            48

/* LCD 8-bit data bus */
#define PIN_LCD_D0            9
#define PIN_LCD_D1            46
#define PIN_LCD_D2            3
#define PIN_LCD_D3            8
#define PIN_LCD_D4            18
#define PIN_LCD_D5            17
#define PIN_LCD_D6            16
#define PIN_LCD_D7            15
#define LCD_BUS_WIDTH         8
#define LCD_DATA_GPIO_NUMS \
    PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3, \
    PIN_LCD_D4, PIN_LCD_D5, PIN_LCD_D6, PIN_LCD_D7

/* Touch I2C */
#define PIN_TP_INT            7
#define PIN_TP_SDA            6
#define PIN_TP_SCL            5
#define PIN_TP_RST            4

/* DS18B20 one-wire temperature sensor. DQ requires a 4.7 kOhm pull-up. */
#define PIN_DS18B20_DQ        41

/* Migrated UITouch peripherals */
#define UART_RX_PIN           10
#define UART_TX_PIN           11
#define BUZZER_PIN            2

#else

/* Backlight: LEDC PWM driven, see lcd.c (brightness via lcd_set_brightness) */
#define PIN_LCD_BL            38
#define LCD_BL_ACTIVE_LEVEL   1   /* 1 = duty high lights LED, 0 = active-low */

/* LCD 8080 control lines */
#define PIN_LCD_RST           4
#define PIN_LCD_RS            12
#define PIN_LCD_WR            47
#define PIN_LCD_TE            48

/* LCD 8-bit data bus */
#define PIN_LCD_D0            9
#define PIN_LCD_D1            46
#define PIN_LCD_D2            3
#define PIN_LCD_D3            8
#define PIN_LCD_D4            18
#define PIN_LCD_D5            17
#define PIN_LCD_D6            16
#define PIN_LCD_D7            15
#define LCD_BUS_WIDTH         8
#define LCD_DATA_GPIO_NUMS \
    PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3, \
    PIN_LCD_D4, PIN_LCD_D5, PIN_LCD_D6, PIN_LCD_D7

/* Touch I2C */
#define PIN_TP_INT            7
#define PIN_TP_SDA            6
#define PIN_TP_SCL            5
#define PIN_TP_RST            4

/* DS18B20 one-wire temperature sensor. DQ requires a 4.7 kOhm pull-up. */
#define PIN_DS18B20_DQ        41

/* Migrated UITouch peripherals */
#define UART_RX_PIN           10
#define UART_TX_PIN           11
#define BUZZER_PIN            1

#endif

/* Physical 3.5-inch LCD resolution, landscape. */
#define LCD_H_RES             480
#define LCD_V_RES             320

/* UITouch was authored against this canvas; native UI code adapts from it. */
#define UI_BASE_H_RES         320
#define UI_BASE_V_RES         240

/* LVGL renders natively at the physical LCD resolution. */
#define UI_H_RES              LCD_H_RES
#define UI_V_RES              LCD_V_RES

/* Physical portrait coordinate space reported by the touch controller. */
#define LCD_PHYS_W            320
#define LCD_PHYS_H            480

/* Rotation controls shared by LCD and touch mapping.
 * Keep SWAP_XY unchanged and toggle both mirror axes for a 180° turn. */
#define LCD_SWAP_XY           1
#define LCD_MIRROR_X          0
#define LCD_MIRROR_Y          0

/* Hardware MADCTL rotation above is effective on this panel. Enabling the
 * software path as well would apply a second 180° turn and cancel it. */
#define UI_SW_ROTATE_180      0

/* LCD bus parameters. */
#define LCD_CMD_BITS          8
#define LCD_PARAM_BITS        8
#define LCD_PCLK_HZ           (40 * 1000 * 1000)
#define LCD_SWAP_COLOR_BYTES  1
#define LCD_USE_ST7796_INIT   1
/* ST7796 manufacturer panel tuning. */
#define LCD_INVERSION_MODE    0x01
#define LCD_VCOM_V1           0x16
/* Manufacturer-validated internal SRAM ping-pong bank size. */
#define LCD_I80_BOUNCE_BYTES  (16 * 1024)

/* Wi-Fi/LCD power-integrity tuning. TX power is expressed in 0.25 dBm units. */
#define WIFI_DIAG_FORCE_OFF       0
#define WIFI_MAX_TX_POWER_QDBM   80  /* 20 dBm, restored for full coverage. */

/* UART protocol used by the migrated UITouch business layer. */
#define UART_PORT_NUM         1
#define UART_BAUD_RATE        115200

#endif /* GPIO_H */
