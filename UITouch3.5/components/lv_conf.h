/**
 * @file lv_conf.h
 * LVGL 9.x 配置 for ESP32-S3 + ST7789 (240x240, RGB565)
 *
 * ⚠️ 注意：本项目 sdkconfig 中 CONFIG_LV_CONF_SKIP=y，LVGL 走 ESP-IDF
 * Kconfig 路线，本文件【不生效】！实际生效的配置在
 * sdkconfig.esp32-s3-n16r2[-nowifi] 的 CONFIG_LV_* 选项中。
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#if 1 /* 设为 0 可禁用本文件，使用 LVGL 内置默认值 */

/*====================
   颜色
 *====================*/
/* 颜色深度: 16 = RGB565 */
#define LV_COLOR_DEPTH 16
/* v9 移除了 LV_COLOR_16_SWAP，字节交换通过 io_cfg.flags.swap_color_bytes 或
   lv_display_set_color_format() 处理 */
#define LV_COLOR_CHROMA_KEY lv_color_hex(0x00ff00)

/*====================
   内存
 *====================*/
#define LV_MEM_CUSTOM 0
/* LVGL 内部堆大小 */
#define LV_MEM_SIZE (128U * 1024U)

/*====================
   OS / 线程
 *====================*/
/* LV_OS_NONE: 单任务模式（当前项目在单 FreeRTOS 任务中调用 lv_task_handler） */
#define LV_USE_OS LV_OS_NONE

/*====================
   HAL
 *====================*/
/* v9 将 LV_DISP_DEF_REFR_PERIOD 重命名为 LV_DEF_REFR_PERIOD */
#define LV_DEF_REFR_PERIOD  16   /* ms，刷新周期（60Hz 目标） */
#define LV_INDEV_DEF_READ_PERIOD 10  /* ms，输入轮询周期（100Hz 触摸采样） */
#define LV_TICK_CUSTOM 0
#define LV_DPI_DEF 130

/*====================
   绘制
 *====================*/
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_DRAW_BUF_ALIGN        4

/*====================
   日志
 *====================*/
#define LV_USE_LOG      0

/*====================
   断言
 *====================*/
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_OBJ           0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_ASSERT_HANDLER_INCLUDE   <stdint.h>
#define LV_ASSERT_HANDLER           while(1);

/*====================
   性能监控
 *====================*/
/* 注意：本文件因 CONFIG_LV_CONF_SKIP=y 不参与编译（见文件头注释）。
 * 实际开关在 sdkconfig 的 CONFIG_LV_USE_SYSMON / CONFIG_LV_USE_PERF_MONITOR，
 * 两者均已关闭（FPS 角标已于 2026-08-20 移除）。此处保持 0 与生效配置一致，
 * 避免误导。 */
#define LV_USE_SYSMON 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR  0

/*====================
   其他
 *====================*/
#define LV_USE_USER_DATA 1
#define LV_ENABLE_GC     0

/*====================
   编译属性
 *====================*/
#define LV_ATTRIBUTE_MEM_ALIGN_SIZE 4
#define LV_ATTRIBUTE_MEM_ALIGN      __attribute__((aligned(4)))
#define LV_ATTRIBUTE_LARGE_CONST
#define LV_ATTRIBUTE_LARGE_RAM_ARRAY
#define LV_EXPORT_CONST_INT(int_value) struct _silence_gcc_warning
#define LV_ATTRIBUTE_FAST_MEM
#define LV_ATTRIBUTE_TICK_INC
#define LV_ATTRIBUTE_TASK_HANDLER
#define LV_ATTRIBUTE_FLUSH_READY

/*====================
   字体 (仅启用 Montserrat 14)
 *====================*/
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 1

#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
#define LV_FONT_SIMSUN_16_CJK            0
#define LV_FONT_UNSCII_8                 0
#define LV_FONT_UNSCII_16                0
#define LV_FONT_CUSTOM_DECLARE

#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_USE_FONT_COMPRESSED  1
#define LV_USE_FONT_SUBPX       0
#define LV_USE_FONT_PLACEHOLDER 1

/*====================
   文本
 *====================*/
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_"
#define LV_USE_BIDI 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/*====================
   Sprintf
 *====================*/
#define LV_SPRINTF_CUSTOM    0
#define LV_SPRINTF_USE_FLOAT 0

/*====================
   部件 (Widgets)
   v9 重命名: BTN→BUTTON, BTNMATRIX→BUTTONMATRIX, IMG→IMAGE, ANIMIMG→ANIMIMAGE
 *====================*/
#define LV_USE_ARC          1
#define LV_USE_ARCLABEL     0
#define LV_USE_BAR          0
#define LV_USE_BUTTON       1
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CANVAS       0
#define LV_USE_CHECKBOX     0
#define LV_USE_DROPDOWN     0
#define LV_USE_IMAGE        0
#define LV_USE_LABEL        1
#  define LV_LABEL_TEXT_SELECTION 0
#  define LV_LABEL_LONG_TXT_HINT  0
#define LV_USE_LINE         0
#define LV_USE_ROLLER       0
#define LV_USE_SCALE        0
#define LV_USE_SLIDER       0
#define LV_USE_SWITCH       1
#define LV_USE_TEXTAREA     1   /* ui.c 使用了 lv_textarea */
#define LV_USE_TABLE        0

/*====================
   额外部件
 *====================*/
#define LV_USE_ANIMIMG     0
#define LV_USE_CALENDAR    0
#define LV_USE_CHART       0
#define LV_USE_COLORWHEEL  0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD    0
#define LV_USE_LED         0
#define LV_USE_LIST        0
#define LV_USE_MENU        0
#define LV_USE_METER       0
#define LV_USE_MSGBOX      0
#define LV_USE_SPAN        0
#define LV_USE_SPINBOX     0
#define LV_USE_SPINNER     0
#define LV_USE_TABVIEW     0
#define LV_USE_TILEVIEW    0
#define LV_USE_WIN         0

/*====================
   主题
 *====================*/
#define LV_USE_THEME_DEFAULT 1
#  define LV_THEME_DEFAULT_DARK            0
#  define LV_THEME_DEFAULT_GROW            1
#  define LV_THEME_DEFAULT_TRANSITION_TIME 0
#define LV_USE_THEME_SIMPLE  0
#define LV_USE_THEME_MONO    0

/*====================
   布局
 *====================*/
#define LV_USE_FLEX 1
#define LV_USE_GRID 0

/*====================
   第三方解码器 (全部关闭)
 *====================*/
#define LV_USE_PNG     0
#define LV_USE_BMP     0
#define LV_USE_SJPG    0
#define LV_USE_GIF     0
#define LV_USE_QRCODE  0
#define LV_USE_RLOTTIE 0
#define LV_USE_FFMPEG  0

/*====================
   文件系统 (全部关闭)
 *====================*/
#define LV_USE_FS_STDIO  0
#define LV_USE_FS_POSIX  0
#define LV_USE_FS_WIN32  0
#define LV_USE_FS_FATFS  0

#endif /* #if 1 */

#endif /* LV_CONF_H */
