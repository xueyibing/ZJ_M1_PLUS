#pragma once

#include <stdbool.h>

#include "ui_data.h"

/*
 * 轻量多语言层：集中管理界面文案，按当前语言返回字符串。
 * - 语言状态由 data_manager 持久化（NVS），本层只负责缓存与查表。
 * - 页面创建时用 ui_t(key) 取文案，切换语言后重建页面即可生效。
 */

typedef enum {
    UI_LANG_ZH = 0, /* 中文 */
    UI_LANG_EN = 1, /* English */
} ui_language_t;

/* 文案键。新增文案时在此追加，并在 ui_i18n.c 的文案表补齐两种语言。 */
typedef enum {
    UI_STR_BACK = 0,          /* 返回按钮(带箭头) */
    UI_STR_BACK_PLAIN,        /* 返回(无箭头) */
    UI_STR_START,             /* 开始 */
    UI_STR_START_MAKE,        /* 开始制作(煎蛋主页大按钮待机态) */
    UI_STR_COOKING_WAIT,      /* 请稍候…(制作中态) */
    UI_STR_DONE_CHECK,        /* ✓ 已完成(完成态) */
    UI_STR_CANCEL,            /* 取消 */
    UI_STR_CONFIRM,           /* 确认 */
    UI_STR_NOTICE,            /* 提示 */
    UI_STR_NOTICE_DEFAULT,    /* 默认提示内容 */
    UI_STR_SYSTEM_TITLE,      /* 系统设置页标题 */
    UI_STR_LANGUAGE,          /* 语言设置行标题 */
    UI_STR_LASER_CHECK,       /* 激光检测行标题 */
    UI_STR_SECOND_OIL,        /* 二次给油行标题 */
    UI_STR_JIANDAN_TITLE,     /* 煎蛋设置页标题 */
    UI_STR_OIL_OFFSET,        /* 油量调节 */
    UI_STR_DON_OFFSET,        /* 熟度调节 */
    UI_STR_POWER_OFFSET,      /* 功率调节 */
    UI_STR_POWER_FIRST,       /* 第一面功率调节 */
    UI_STR_POWER_SECOND,      /* 第二面功率调节 */
    UI_STR_INIT_LEVEL,        /* 初始档位 */
    UI_STR_MAX_POT_TEMP,      /* 最高锅温 */
    UI_STR_SYS_ENTRY,         /* 系统设置入口按钮 */
    UI_STR_CLEAN_ENTRY,       /* 清洁模式入口按钮 */

    /* 主页/功能页 */
    UI_STR_BRAND_JIANDAN,     /* 双面煎蛋 */
    UI_STR_OIL,               /* 油量 */
    UI_STR_OIL_OPTIONS,       /* 轻油/适油/多油(多行) */
    UI_STR_DONENESS,          /* 熟度 */
    UI_STR_DON_OPTIONS,       /* 流心/溏心/软黄/全熟(多行) */
    UI_STR_DON_RUNNY,         /* 流心 */
    UI_STR_DON_JAMMY,         /* 溏心 */
    UI_STR_DON_SOFT,          /* 软黄 */
    UI_STR_DON_WELL,          /* 全熟 */
    UI_STR_CANCEL_CONFIRM_TIP,/* 取消前二次确认 */
    UI_STR_CONFIRM_CANCEL,    /* 确认停止 */
    UI_STR_CONTINUE_MAKING,   /* 继续制作 */
    UI_STR_CANCEL_TIP,        /* 取消确认提示 */
    UI_STR_ESTOP,             /* 紧急停止(制作中浮层按钮) */
    UI_STR_CANCEL_REST,       /* 取消后续(制作中浮层按钮) */
    UI_STR_CANCEL_REST_TIP,   /* 取消后续前二次确认 */

    /* 运行状态 */
    UI_STR_ST_OFFLINE,        /* 通信离线 */
    UI_STR_ST_COMM_ERROR,     /* 通信异常 */
    UI_STR_ST_FAULT,          /* 故障报警 */
    UI_STR_ST_PENDING,        /* 待启动 */
    UI_STR_ST_IDLE,           /* 待机 */
    UI_STR_ST_CHECK_TEMP,     /* 检测温度 */
    UI_STR_ST_CHECK_DIST,     /* 检测测距 */
    UI_STR_ST_CHECK_POT,      /* 检测锅 */
    UI_STR_ST_HOMING,         /* 回零复位 */
    UI_STR_ST_PREHEAT,        /* 正在预热 */
    UI_STR_ST_GIVE_EGG,       /* 正在给蛋 */
    UI_STR_ST_GIVE_OIL,       /* 正在给油 */
    UI_STR_ST_MAKING,         /* 正在制作 */
    UI_STR_ST_STOPPING,       /* 正在取消 */
    UI_STR_ST_STOPPED_RECOVERY, /* 已停止，等待清理确认 */
    UI_STR_ST_UNKNOWN,        /* 未知 */

    /* 清洁模式页 */
    UI_STR_CLEAN_TITLE,       /* 清洁模式 */
    UI_STR_SCREEN_LOCKED,     /* 屏幕锁定 */
    UI_STR_UNLOCK_HINT,       /* 长按1秒解锁 */
    UI_STR_UNLOCK_SCREEN,     /* 解锁屏幕 */
    UI_STR_LOCK_SCREEN,       /* 锁定屏幕 */
    UI_STR_AUTO_OPEN,         /* 煎蛋完成自动打开 */
    UI_STR_PUSH_OPEN,         /* 推开打蛋器 */
    UI_STR_PUSH_CLOSE,        /* 关闭打蛋器 */
    UI_STR_ACT_GIVE_OIL,      /* 给油 */
    UI_STR_ACT_BACK_OIL,      /* 回油 */
    UI_STR_EGG_GATE_OPEN,     /* 打开给蛋口 */
    UI_STR_EGG_GATE_CLOSE,    /* 关闭给蛋口 */

    /* 太阳蛋设置页 */

    /* 告警页 */
    UI_STR_ERR_CODE_FMT,      /* 错误码格式(含 %02d 与 %s) */

    /* 错误日志页 */
    UI_STR_ERROR_LOG_TITLE,   /* 错误日志 */
    UI_STR_ERROR_LOG_EMPTY,   /* 暂无错误记录 */
    UI_STR_ERROR_LOG_CLEAR,   /* 清空 */

    /* Wi-Fi 配网页面 */
    UI_STR_WIFI,
    UI_STR_WIFI_OFF,
    UI_STR_WIFI_NOT_CONFIGURED,
    UI_STR_WIFI_SCANNING,
    UI_STR_WIFI_CONNECTING,
    UI_STR_WIFI_CONNECTED,
    UI_STR_WIFI_DISCONNECTED,
    UI_STR_WIFI_FAILED,
    UI_STR_WIFI_REFRESH,
    UI_STR_WIFI_NO_NETWORKS,
    UI_STR_WIFI_PASSWORD,
    UI_STR_WIFI_PASSWORD_LENGTH,
    UI_STR_WIFI_SHOW_PASSWORD,
    UI_STR_WIFI_CONNECT_HINT,
    UI_STR_WIFI_WRONG_PASSWORD,
    UI_STR_WIFI_NOT_FOUND,
    UI_STR_WIFI_WEAK_SIGNAL,
    UI_STR_WIFI_DHCP_FAILED,
    UI_STR_WIFI_CONNECT_FAILED,
    UI_STR_WIFI_IP_ADDRESS,
    UI_STR_WIFI_SIGNAL,
    UI_STR_WIFI_SECURITY,
    UI_STR_WIFI_OPEN_NETWORK,
    UI_STR_WIFI_SECURED_NETWORK,
    UI_STR_WIFI_RECONNECT,
    UI_STR_WIFI_DISCONNECT,
    UI_STR_WIFI_FORGET,
    UI_STR_WIFI_FORGET_CONFIRM,
    UI_STR_WIFI_ONLY_24G,

    UI_STR_COUNT,
} ui_str_key_t;


/* 从 data_manager 读取并缓存当前语言。应在 data_manager_init 之后调用一次。 */
void ui_i18n_init(void);

/* 获取当前语言。 */
ui_language_t ui_i18n_get_language(void);

/* 设置语言：持久化并更新缓存。返回是否成功（调用方负责重建页面）。 */
bool ui_i18n_set_language(ui_language_t language);

/* 按当前语言返回文案。key 非法时返回空串。 */
const char *ui_t(ui_str_key_t key);
