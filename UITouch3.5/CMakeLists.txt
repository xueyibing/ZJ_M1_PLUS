cmake_minimum_required(VERSION 3.16.0)
set(COMPONENTS __pio_env src lvgl esp_lcd esp_lcd_st7796 esp_lcd_touch esp_lcd_touch_ft5x06 esp_driver_gpio esp_driver_i2c esp_driver_uart esp_driver_ledc esp_timer nvs_flash esp_psram spi_flash)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(UITouch3_5)
