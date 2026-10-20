#include "buzzer.h"
#include "gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* ---- LEDC 配置 ------------------------------------------------------------ */
#define BUZZER_LEDC_SPEED  LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_TIMER  LEDC_TIMER_0
#define BUZZER_LEDC_CH     LEDC_CHANNEL_0
#define BUZZER_FREQ_HZ     2700U
#define BUZZER_DUTY_RES    LEDC_TIMER_10_BIT
#define BUZZER_DUTY_OFF    (0U)
#define BUZZER_DUTY_CLICK  (256U)   /* 25%  — 按键点击音量 */
#define BUZZER_DUTY_DONE_LONG  (384U)   /* 37.5% — 完成长滴，清晰但不刺耳 */
#define BUZZER_DUTY_DONE_SHORT (192U)   /* 18.75% — 结尾轻短滴，收束感更强 */
#define BUZZER_DUTY_ALARM  (512U)   /* 50%  — 报警音量，更明显 */

/* ---- 点击蜂鸣 ------------------------------------------------------------ */
static esp_timer_handle_t s_beep_timer = NULL;
static SemaphoreHandle_t  s_buzzer_mutex = NULL;

/* ---- 完成提示音状态机 ---------------------------------------------------- */
/*
 * 完成音型：长滴 + 轻短滴
 * 750ms ON -> 120ms OFF -> 140ms ON -> 结束
 */
static const uint32_t k_complete_duties[] = {
    BUZZER_DUTY_DONE_LONG,
    BUZZER_DUTY_OFF,
    BUZZER_DUTY_DONE_SHORT,
    BUZZER_DUTY_OFF,
};
static const uint32_t k_complete_delays_ms[] = {
    750,
    120,
    140,
    0,
};
#define COMPLETE_PHASE_COUNT 4

static esp_timer_handle_t s_complete_timer  = NULL;
static bool               s_complete_active = false;
static int                s_complete_phase  = 0;

/* ---- 报警状态机 ----------------------------------------------------------- */
/*
 * 三组报警序列：每组是双蜂鸣“滴-滴”，组间留明显停顿。
 * 一组：200ms ON -> 100ms OFF -> 200ms ON -> 800ms OFF
 * 播放 3 组后自动停止，避免变成简单响三次。
 */
static const uint32_t k_alarm_duties[] = {
    BUZZER_DUTY_ALARM, BUZZER_DUTY_OFF, BUZZER_DUTY_ALARM, BUZZER_DUTY_OFF,
    BUZZER_DUTY_ALARM, BUZZER_DUTY_OFF, BUZZER_DUTY_ALARM, BUZZER_DUTY_OFF,
    BUZZER_DUTY_ALARM, BUZZER_DUTY_OFF, BUZZER_DUTY_ALARM, BUZZER_DUTY_OFF,
};
static const uint32_t k_alarm_delays_ms[] = {
    200, 100, 200, 800,
    200, 100, 200, 800,
    200, 100, 200, 0,
};
#define ALARM_PHASE_COUNT 12

static esp_timer_handle_t s_alarm_timer  = NULL;
static bool               s_alarm_active = false;
static int                s_alarm_phase  = 0;

/* ---- 内部实现 ------------------------------------------------------------- */

static bool buzzer_lock(void)
{
    return s_buzzer_mutex != NULL &&
           xSemaphoreTake(s_buzzer_mutex, portMAX_DELAY) == pdTRUE;
}

static void buzzer_unlock(void)
{
    xSemaphoreGive(s_buzzer_mutex);
}

static void beep_stop_cb(void *arg)
{
    (void)arg;
    if (!buzzer_lock()) {
        return;
    }
    if (s_alarm_active || s_complete_active) {
        buzzer_unlock();
        return;
    }
    ledc_set_duty(BUZZER_LEDC_SPEED, BUZZER_LEDC_CH, BUZZER_DUTY_OFF);
    ledc_update_duty(BUZZER_LEDC_SPEED, BUZZER_LEDC_CH);
    buzzer_unlock();
}

static void complete_tick_locked(void)
{
    if (!s_complete_active) {
        if (!s_alarm_active) {
            ledc_set_duty(BUZZER_LEDC_SPEED, BUZZER_LEDC_CH, BUZZER_DUTY_OFF);
            ledc_update_duty(BUZZER_LEDC_SPEED, BUZZER_LEDC_CH);
        }
        return;
    }

    ledc_set_duty(BUZZER_LEDC_SPEED, BUZZER_LEDC_CH, k_complete_duties[s_complete_phase]);
    ledc_update_duty(BUZZER_LEDC_SPEED, BUZZER_LEDC_CH);

    if (s_complete_phase + 1 < COMPLETE_PHASE_COUNT) {
        esp_timer_start_once(s_complete_timer,
                             (uint64_t)k_complete_delays_ms[s_complete_phase] * 1000ULL);
        s_complete_phase++;
    } else {
        s_complete_active = false;
        s_complete_phase = 0;
        ledc_set_duty(BUZZER_LEDC_SPEED, BUZZER_LEDC_CH, BUZZER_DUTY_OFF);
        ledc_update_duty(BUZZER_LEDC_SPEED, BUZZER_LEDC_CH);
    }
}

static void complete_tick_cb(void *arg)
{
    (void)arg;
    if (!buzzer_lock()) {
        return;
    }
    complete_tick_locked();
    buzzer_unlock();
}

static void alarm_tick_locked(void)
{
    if (!s_alarm_active) {
        if (!s_complete_active) {
            ledc_set_duty(BUZZER_LEDC_SPEED, BUZZER_LEDC_CH, BUZZER_DUTY_OFF);
            ledc_update_duty(BUZZER_LEDC_SPEED, BUZZER_LEDC_CH);
        }
        return;
    }
    ledc_set_duty(BUZZER_LEDC_SPEED, BUZZER_LEDC_CH, k_alarm_duties[s_alarm_phase]);
    ledc_update_duty(BUZZER_LEDC_SPEED, BUZZER_LEDC_CH);

    if (s_alarm_phase + 1 < ALARM_PHASE_COUNT) {
        esp_timer_start_once(s_alarm_timer,
                             (uint64_t)k_alarm_delays_ms[s_alarm_phase] * 1000ULL);
        s_alarm_phase++;
    } else {
        /* 序列播完，自动停止 */
        s_alarm_active = false;
        s_alarm_phase  = 0;
    }
}

static void alarm_tick_cb(void *arg)
{
    (void)arg;
    if (!buzzer_lock()) {
        return;
    }
    alarm_tick_locked();
    buzzer_unlock();
}

/* ---- 公开 API ------------------------------------------------------------- */

esp_err_t buzzer_init(void)
{
    const ledc_timer_config_t timer_conf = {
        .speed_mode      = BUZZER_LEDC_SPEED,
        .duty_resolution = BUZZER_DUTY_RES,
        .timer_num       = BUZZER_LEDC_TIMER,
        .freq_hz         = BUZZER_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) return ret;

    const ledc_channel_config_t channel_conf = {
        .gpio_num   = BUZZER_PIN,
        .speed_mode = BUZZER_LEDC_SPEED,
        .channel    = BUZZER_LEDC_CH,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = BUZZER_LEDC_TIMER,
        .duty       = BUZZER_DUTY_OFF,
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&channel_conf);
    if (ret != ESP_OK) return ret;

    s_buzzer_mutex = xSemaphoreCreateMutex();
    if (s_buzzer_mutex == NULL) return ESP_ERR_NO_MEM;

    const esp_timer_create_args_t beep_args = {
        .callback = beep_stop_cb,
        .name     = "buzzer_beep",
    };
    ret = esp_timer_create(&beep_args, &s_beep_timer);
    if (ret != ESP_OK) return ret;

    const esp_timer_create_args_t alarm_args = {
        .callback = alarm_tick_cb,
        .name     = "buzzer_alarm",
    };
    ret = esp_timer_create(&alarm_args, &s_alarm_timer);
    if (ret != ESP_OK) return ret;

    const esp_timer_create_args_t complete_args = {
        .callback = complete_tick_cb,
        .name     = "buzzer_complete",
    };
    return esp_timer_create(&complete_args, &s_complete_timer);
}

void buzzer_beep_async(uint32_t duration_ms)
{
    if (!buzzer_lock()) {
        return;
    }
    /* 报警期间点击音不打断报警 */
    if (s_beep_timer == NULL || duration_ms == 0 || s_alarm_active || s_complete_active) {
        buzzer_unlock();
        return;
    }
    esp_timer_stop(s_beep_timer);
    ledc_set_duty(BUZZER_LEDC_SPEED, BUZZER_LEDC_CH, BUZZER_DUTY_CLICK);
    ledc_update_duty(BUZZER_LEDC_SPEED, BUZZER_LEDC_CH);
    esp_timer_start_once(s_beep_timer, (uint64_t)duration_ms * 1000ULL);
    buzzer_unlock();
}

void buzzer_complete_async(void)
{
    if (!buzzer_lock()) {
        return;
    }
    if (s_complete_timer == NULL || s_alarm_active || s_complete_active) {
        buzzer_unlock();
        return;
    }

    s_complete_active = true;
    s_complete_phase = 0;
    esp_timer_stop(s_beep_timer);
    esp_timer_stop(s_complete_timer);
    complete_tick_locked();
    buzzer_unlock();
}

void buzzer_alarm_start(void)
{
    if (!buzzer_lock()) {
        return;
    }
    if (s_alarm_active) {
        buzzer_unlock();
        return;
    }
    /* 先提升报警状态，防止并发的旧点击回调关闭报警首音。 */
    s_alarm_active = true;
    esp_timer_stop(s_beep_timer);
    s_complete_active = false;
    s_complete_phase = 0;
    if (s_complete_timer != NULL) {
        esp_timer_stop(s_complete_timer);
    }
    s_alarm_phase  = 0;
    /* 直接调用一次驱动首个状态，无需额外延迟 */
    alarm_tick_locked();
    buzzer_unlock();
}

void buzzer_alarm_stop(void)
{
    if (!buzzer_lock()) {
        return;
    }
    s_alarm_active = false;
    esp_timer_stop(s_alarm_timer);
    if (s_complete_timer != NULL) {
        esp_timer_stop(s_complete_timer);
    }
    s_complete_active = false;
    s_complete_phase = 0;
    s_alarm_phase = 0;
    ledc_set_duty(BUZZER_LEDC_SPEED, BUZZER_LEDC_CH, BUZZER_DUTY_OFF);
    ledc_update_duty(BUZZER_LEDC_SPEED, BUZZER_LEDC_CH);
    buzzer_unlock();
}
