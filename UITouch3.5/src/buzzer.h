#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t buzzer_init(void);

/* 任务上下文调用；非阻塞短促蜂鸣，用于按键反馈（报警/完成音期间自动忽略） */
void buzzer_beep_async(uint32_t duration_ms);

/* 完成提示音：长滴 + 轻短滴（报警期间自动忽略） */
void buzzer_complete_async(void);

/* 三组报警音：每组双蜂鸣，组间有停顿 */
void buzzer_alarm_start(void);
void buzzer_alarm_stop(void);
