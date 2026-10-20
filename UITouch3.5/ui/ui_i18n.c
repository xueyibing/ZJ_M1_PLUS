#include "ui_i18n.h"

#include "data_manager.h"

/* 文案表：[key][language]。中文/英文一一对应。 */
static const char *const s_strings[UI_STR_COUNT][2] = {
    /* UI_STR_BACK          */ { u8"< 返回",   "< Back" },
    /* UI_STR_BACK_PLAIN    */ { u8"返回",     "Back" },
    /* UI_STR_START         */ { u8"开始",     "Start" },
    /* UI_STR_START_MAKE    */ { u8"开始制作", "Start" },
    /* UI_STR_COOKING_WAIT  */ { u8"请稍候…",  "Please wait…" },
    /* UI_STR_DONE_CHECK    */ { u8"✓ 已完成", "✓ Done" },
    /* UI_STR_CANCEL        */ { u8"取消",     "Cancel" },
    /* UI_STR_CONFIRM       */ { u8"确认",     "OK" },
    /* UI_STR_NOTICE        */ { u8"提示",     "Notice" },
    /* UI_STR_NOTICE_DEFAULT*/ { u8"请按提示完成后继续", "Follow the prompt to continue" },
    /* UI_STR_SYSTEM_TITLE  */ { u8"系统设置", "System" },
    /* UI_STR_LANGUAGE      */ { u8"语言设置", "Language" },
    /* UI_STR_LASER_CHECK   */ { u8"激光检测", "Laser Detect" },
    /* UI_STR_SECOND_OIL    */ { u8"2次给油", "Second Oil" },
    /* UI_STR_JIANDAN_TITLE */ { u8"煎蛋设置", "Egg Setup" },
    /* UI_STR_OIL_OFFSET    */ { u8"油量调节", "Oil Adj" },
    /* UI_STR_DON_OFFSET    */ { u8"熟度调节", "Done Adj" },
    /* UI_STR_POWER_OFFSET  */ { u8"功率调节", "Power Adj" },
    /* UI_STR_POWER_FIRST   */ { u8"B锅功率", "Pot B Power" },
    /* UI_STR_POWER_SECOND  */ { u8"A锅功率", "Pot A Power" },
    /* UI_STR_INIT_LEVEL    */ { u8"初始档位", "Init Gear" },
    /* UI_STR_MAX_POT_TEMP  */ { u8"最高锅温", "Max Temp" },
    /* UI_STR_SYS_ENTRY     */ { u8"系统设置 >", "System >" },
    /* UI_STR_CLEAN_ENTRY   */ { u8"清洁模式 >", "Clean Mode >" },

    /* UI_STR_BRAND_JIANDAN */ { u8"口味选择", "Flavor" },
    /* UI_STR_OIL           */ { u8"油量",     "Oil" },
    /* UI_STR_OIL_OPTIONS   */ { u8"轻油\n适油\n多油", "Light\nMedium\nHeavy" },
    /* UI_STR_DONENESS      */ { u8"熟度",     "Doneness" },
    /* UI_STR_DON_OPTIONS   */ { u8"流心\n溏心\n软黄\n全熟", "Runny\nJammy\nSoft\nWell" },
    /* UI_STR_DON_RUNNY     */ { u8"流心",     "Runny" },
    /* UI_STR_DON_JAMMY     */ { u8"溏心",     "Jammy" },
    /* UI_STR_DON_SOFT      */ { u8"软黄",     "Soft" },
    /* UI_STR_DON_WELL      */ { u8"全熟",     "Well" },
    /* UI_STR_CANCEL_CONFIRM_TIP */ { u8"确认取消当前制作吗？确认后将停止当前动作和后续制作。",
                                      "Cancel the current batch? Current motion and remaining eggs will stop." },
    /* UI_STR_CONFIRM_CANCEL */ { u8"确认停止", "Stop" },
    /* UI_STR_CONTINUE_MAKING */ { u8"继续制作", "Continue" },
    /* UI_STR_CANCEL_TIP    */ { u8"正在取消。动作停止后，请清理锅内鸡蛋并确认机构已回到安全位置，再点击确认。",
                                 "Canceling. When motion stops, clear the pans, check the mechanisms, then confirm." },
    /* UI_STR_ESTOP         */ { u8"紧急停止", "E-Stop" },
    /* UI_STR_CANCEL_REST   */ { u8"取消后续", "Skip Rest" },
    /* UI_STR_CANCEL_REST_TIP */ { u8"确认取消后续制作吗？当前这枚完成后自动结束。",
                                   "Skip remaining eggs? The batch ends after the current one." },

    /* UI_STR_ST_OFFLINE    */ { u8"通信离线", "Offline" },
    /* UI_STR_ST_COMM_ERROR */ { u8"通信异常", "Comm Error" },
    /* UI_STR_ST_FAULT      */ { u8"故障报警", "Fault" },
    /* UI_STR_ST_PENDING    */ { u8"待启动",   "Pending" },
    /* UI_STR_ST_IDLE       */ { u8"待机",     "Idle" },
    /* UI_STR_ST_CHECK_TEMP */ { u8"检测温度", "Temp Check" },
    /* UI_STR_ST_CHECK_DIST */ { u8"检测测距", "Range Check" },
    /* UI_STR_ST_CHECK_POT  */ { u8"检测锅",   "Pot Check" },
    /* UI_STR_ST_HOMING     */ { u8"回零复位", "Homing" },
    /* UI_STR_ST_PREHEAT    */ { u8"正在预热", "Preheating" },
    /* UI_STR_ST_GIVE_EGG   */ { u8"正在给蛋", "Loading Egg" },
    /* UI_STR_ST_GIVE_OIL   */ { u8"正在给油", "Adding Oil" },
    /* UI_STR_ST_MAKING     */ { u8"正在制作", "Cooking" },
    /* UI_STR_ST_STOPPING   */ { u8"正在取消", "Canceling" },
    /* UI_STR_ST_STOPPED_RECOVERY */ { u8"待清理确认", "Check & Confirm" },
    /* UI_STR_ST_UNKNOWN    */ { u8"未知",     "Unknown" },

    /* UI_STR_CLEAN_TITLE   */ { u8"清洁模式", "Clean Mode" },
    /* UI_STR_SCREEN_LOCKED */ { u8"屏幕锁定", "Screen Locked" },
    /* UI_STR_UNLOCK_HINT   */ { u8"长按1秒解锁", "Hold 1s to unlock" },
    /* UI_STR_UNLOCK_SCREEN */ { u8"解锁屏幕", "Unlock" },
    /* UI_STR_LOCK_SCREEN   */ { u8"锁定屏幕", "Lock Screen" },
    /* UI_STR_AUTO_OPEN     */ { u8"煎蛋完成自动打开", "Auto open when done" },
    /* UI_STR_PUSH_OPEN     */ { u8"推开打蛋器", "Push Beater" },
    /* UI_STR_PUSH_CLOSE    */ { u8"关闭打蛋器", "Close Beater" },
    /* UI_STR_ACT_GIVE_OIL  */ { u8"给油",     "Give Oil" },
    /* UI_STR_ACT_BACK_OIL  */ { u8"回油",     "Return Oil" },
    /* UI_STR_EGG_GATE_OPEN */ { u8"打开给蛋口", "Open Egg Gate" },
    /* UI_STR_EGG_GATE_CLOSE*/ { u8"关闭给蛋口", "Close Egg Gate" },


    /* UI_STR_ERR_CODE_FMT  */ { u8"错误码 E%02d\n%s\n处理后点击返回",
                                 "Error E%02d\n%s\nTap Back when resolved" },

    /* UI_STR_ERROR_LOG_TITLE */ { u8"错误日志", "Error Log" },
    /* UI_STR_ERROR_LOG_EMPTY */ { u8"暂无错误记录", "No error records" },
    /* UI_STR_ERROR_LOG_CLEAR */ { u8"清空", "Clear" },

    /* Wi-Fi 配网页面 */
    /* UI_STR_WIFI               */ { "Wi-Fi", "Wi-Fi" },
    /* UI_STR_WIFI_OFF           */ { u8"已关闭", "Off" },
    /* UI_STR_WIFI_NOT_CONFIGURED*/ { u8"未配置", "Not configured" },
    /* UI_STR_WIFI_SCANNING      */ { u8"正在扫描...", "Scanning..." },
    /* UI_STR_WIFI_CONNECTING    */ { u8"正在连接...", "Connecting..." },
    /* UI_STR_WIFI_CONNECTED     */ { u8"已连接", "Connected" },
    /* UI_STR_WIFI_DISCONNECTED  */ { u8"未连接", "Disconnected" },
    /* UI_STR_WIFI_FAILED        */ { u8"连接失败", "Failed" },
    /* UI_STR_WIFI_REFRESH       */ { u8"刷新", "Refresh" },
    /* UI_STR_WIFI_NO_NETWORKS   */ { u8"未搜索到可用网络", "No networks found" },
    /* UI_STR_WIFI_PASSWORD      */ { u8"密码", "Password" },
    /* UI_STR_WIFI_PASSWORD_LENGTH*/{ u8"密码需要8至63个字符", "Password must be 8 to 63 characters" },
    /* UI_STR_WIFI_SHOW_PASSWORD */ { u8"显示密码", "Show password" },
    /* UI_STR_WIFI_CONNECT_HINT  */ { u8"按确认键连接", "Press OK to connect" },
    /* UI_STR_WIFI_WRONG_PASSWORD*/ { u8"密码错误或认证失败", "Wrong password or authentication failed" },
    /* UI_STR_WIFI_NOT_FOUND     */ { u8"未找到该网络", "Network not found" },
    /* UI_STR_WIFI_WEAK_SIGNAL   */ { u8"Wi-Fi信号太弱", "Wi-Fi signal is too weak" },
    /* UI_STR_WIFI_DHCP_FAILED   */ { u8"无法获取IP地址", "Could not obtain an IP address" },
    /* UI_STR_WIFI_CONNECT_FAILED*/ { u8"连接失败，请重试", "Connection failed, please retry" },
    /* UI_STR_WIFI_IP_ADDRESS    */ { u8"IP地址", "IP address" },
    /* UI_STR_WIFI_SIGNAL        */ { u8"信号强度", "Signal" },
    /* UI_STR_WIFI_SECURITY      */ { u8"安全性", "Security" },
    /* UI_STR_WIFI_OPEN_NETWORK  */ { u8"开放网络", "Open network" },
    /* UI_STR_WIFI_SECURED_NETWORK*/{ u8"加密网络", "Secured network" },
    /* UI_STR_WIFI_RECONNECT     */ { u8"重新连接", "Reconnect" },
    /* UI_STR_WIFI_DISCONNECT    */ { u8"断开连接", "Disconnect" },
    /* UI_STR_WIFI_FORGET        */ { u8"忘记网络", "Forget" },
    /* UI_STR_WIFI_FORGET_CONFIRM*/ { u8"确定忘记此网络？", "Forget this network?" },
    /* UI_STR_WIFI_ONLY_24G      */ { u8"仅支持2.4GHz网络", "2.4 GHz networks only" },
};


static ui_language_t s_language = UI_LANG_ZH;

static ui_language_t ui_i18n_normalize(int32_t value)
{
    return (value == UI_LANG_EN) ? UI_LANG_EN : UI_LANG_ZH;
}

void ui_i18n_init(void)
{
    s_language = ui_i18n_normalize((int32_t)data_manager_get_language());
}

ui_language_t ui_i18n_get_language(void)
{
    return s_language;
}

bool ui_i18n_set_language(ui_language_t language)
{
    const ui_language_t next = ui_i18n_normalize((int32_t)language);

    if(data_manager_set_language((data_manager_language_t)next) != ESP_OK) {
        return false;
    }

    s_language = next;
    return true;
}

const char *ui_t(ui_str_key_t key)
{
    if((int)key < 0 || key >= UI_STR_COUNT) {
        return "";
    }

    return s_strings[key][s_language];
}
