#include "egg_control.h"
#include "emergency_stop_control.h"
#include "gpio.h"
#include <Arduino.h>
#include "jiandan_data_manager.h"
const int PWM_CHANNEL_A = 0;
const int PWM_CHANNEL_B = 1;
const int PWM_FREQ = 20000;
const int PWM_RESOLUTION = 8;  // 占空比范围 0-255
const int PWM_MAX_DUTY = 180;  // 50% 占空比，24V×50%=12V，正常运行用
const int PWM_HOME_DUTY = 128; // 50% 占空比，24V×50%=12V，home用
const int PWM_KICK_DUTY = 200; // 100% 满功率，启动瞬间冲击力矩用
const int PWM_KICK_MS = 20;   // 满功率持续时间，足以克服机械卡死
const int EGG_OPEN_MOVE_MS = 600;
const int EGG_OPEN_SETTLE_MS = 600;
const int EGG_CLOSE_MOVE_MS = 700;

namespace
{
volatile bool s_egg_self_test_busy = false;

void eggSelfTestTask(void *params)
{
  (void)params;

  EggControl::getInstance()->give();
  s_egg_self_test_busy = false;
  vTaskDelete(NULL);
}
}

EggControl EggControl::instance;
EggControl::EggControl()
{
}

EggControl *EggControl::getInstance()
{
  return &instance;
}

void EggControl::init()
{
  ledcSetup(PWM_CHANNEL_A, PWM_FREQ, PWM_RESOLUTION);
  ledcSetup(PWM_CHANNEL_B, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(GIVE_EGG_A, PWM_CHANNEL_A);
  ledcAttachPin(GIVE_EGG_B, PWM_CHANNEL_B);
  ledcWrite(PWM_CHANNEL_A, 0);
  ledcWrite(PWM_CHANNEL_B, 0);
  vTaskDelay(100);
}

void EggControl::give()
{
  Serial.printf("给蛋...\n");

  open();
  if (!EmergencyStopControl::getInstance()->delayMs(EGG_OPEN_SETTLE_MS))
  {
    stopImmediately();
    return;
  }
  close();
  Serial.printf("给蛋结束...\n");
}

void EggControl::open()
{
  Serial.printf("蛋托打开...\n");

  ledcWrite(PWM_CHANNEL_B, 0);
  if (EmergencyStopControl::getInstance()->isRequested())
  {
    stopImmediately();
    return;
  }
  ledcWrite(PWM_CHANNEL_A, PWM_KICK_DUTY);
  if (!EmergencyStopControl::getInstance()->delayMs(PWM_KICK_MS))
  {
    stopImmediately();
    return;
  }
  ledcWrite(PWM_CHANNEL_A, PWM_MAX_DUTY);
  (void)EmergencyStopControl::getInstance()->delayMs(EGG_OPEN_MOVE_MS - PWM_KICK_MS);

  ledcWrite(PWM_CHANNEL_A, 0);
}

void EggControl::close()
{
  closeInternal(false);
}

void EggControl::closeInternal(bool allowDuringCancel)
{
  Serial.printf("蛋托关闭...\n");

  ledcWrite(PWM_CHANNEL_A, 0);
  if (!allowDuringCancel && EmergencyStopControl::getInstance()->isRequested())
  {
    stopImmediately();
    return;
  }
  ledcWrite(PWM_CHANNEL_B, PWM_KICK_DUTY);
  if (allowDuringCancel)
  {
    vTaskDelay(pdMS_TO_TICKS(PWM_KICK_MS));
  }
  else if (!EmergencyStopControl::getInstance()->delayMs(PWM_KICK_MS))
  {
    stopImmediately();
    return;
  }
  ledcWrite(PWM_CHANNEL_B, PWM_MAX_DUTY);
  if (allowDuringCancel)
  {
    vTaskDelay(pdMS_TO_TICKS(EGG_CLOSE_MOVE_MS - PWM_KICK_MS));
  }
  else
  {
    (void)EmergencyStopControl::getInstance()->delayMs(EGG_CLOSE_MOVE_MS - PWM_KICK_MS);
  }

  ledcWrite(PWM_CHANNEL_B, 0);
}

void EggControl::stopImmediately()
{
  ledcWrite(PWM_CHANNEL_A, 0);
  ledcWrite(PWM_CHANNEL_B, 0);
}

void EggControl::home()
{
  Serial.printf("蛋托回位...\n");
  close();

}

void EggControl::homeAfterCancel()
{
  // 取消令牌保持有效期间，仅允许这一项确定方向、确定时长的收尾动作。
  Serial.printf("取消收尾：蛋托回Home...\n");
  closeInternal(true);
  stopImmediately();
}

bool EggControl::runSelfTest()
{
  if (s_egg_self_test_busy)
  {
    return false;
  }

  s_egg_self_test_busy = true;
  BaseType_t task_ok = xTaskCreatePinnedToCore(eggSelfTestTask,
                                               "eggSelfTestTask",
                                               1024 * 4,
                                               NULL,
                                               2,
                                               NULL,
                                               1);
  if (task_ok != pdPASS)
  {
    s_egg_self_test_busy = false;
    return false;
  }

  return true;
}
