#include "oil_control.h"
#include "gpio.h"
#include <Arduino.h>
#include "jiandan_data_manager.h"
#include "jiandan_maker.h"
#include "emergency_stop_control.h"

namespace
{
const int OIL_SAMPLE_MS = 20;
const int OIL_SAMPLE_COUNT = 10;
const int OIL_MAX_TRY_COUNT = 50;
const int OIL_FIRST_PRIME_TRY_THRESHOLD = 15;
volatile bool s_oil_self_test_busy = false;

void oilSelfTestTask(void *params)
{
  (void)params;

  OilControl::getInstance()->giveOil(JiandanMaker::calcOilTime(JiandanDataManager::getInstance()->getOilLevel()));
  s_oil_self_test_busy = false;
  vTaskDelete(NULL);
}
}

OilControl OilControl::instance;
OilControl::OilControl()
{
}

OilControl *OilControl::getInstance()
{
  return &instance;
}

void OilControl::init()
{
  pinMode(GIVE_OIL_A, OUTPUT);
  pinMode(GIVE_OIL_B, OUTPUT);
  pinMode(OIL_CHECK, INPUT);
  ledcSetup(OIL_PWM_CHANNEL_IN1, OIL_PWM_FREQ, OIL_PWM_RESOLUTION);
  ledcSetup(OIL_PWM_CHANNEL_IN2, OIL_PWM_FREQ, OIL_PWM_RESOLUTION);
  ledcAttachPin(GIVE_OIL_A, OIL_PWM_CHANNEL_IN1);
  ledcAttachPin(GIVE_OIL_B, OIL_PWM_CHANNEL_IN2);
  stopMotor();
}

bool OilControl::giveOil(int milliseconds, bool requireFeedback)
{
  EmergencyStopControl *stop = EmergencyStopControl::getInstance();
  if (stop->isRequested())
  {
    stopMotor();
    return false;
  }

  if (!requireFeedback)
  {
    Serial.printf("[Oil][WARN] feedback bypassed, timed dispense=%dms\n", milliseconds);
    startMotor(true, milliseconds);
    return true;
  }

  int need_count = milliseconds / 200;
  int success_count = 0;
  int try_count = 0;
  int compensation_ms = 0;

  if (need_count <= 0)
  {
    stopMotor();
    return true;
  }

  setMotorDuty(true, OIL_PWM_KICK_DUTY);
  if (!stop->delayMs(OIL_PWM_KICK_MS))
  {
    stopMotor();
    return false;
  }
  setMotorDuty(true, OIL_PWM_RUN_DUTY);

  while (!stop->isRequested() &&
         try_count++ < OIL_MAX_TRY_COUNT && success_count < need_count)
  {
    bool success = true;
    for (int i = 0; i < OIL_SAMPLE_COUNT; i++)
    {
      if (stop->isRequested())
      {
        stopMotor();
        return false;
      }
      success = success && (digitalRead(OIL_CHECK) == LOW);
      if (!stop->delayMs(OIL_SAMPLE_MS))
      {
        stopMotor();
        return false;
      }
    }
    if (success)
    {
      success_count++;
    }
  }

  // 如果建立油路耗时明显偏长，但最终给油已达标，补偿半程时长覆盖首段空转。
  if (try_count > OIL_FIRST_PRIME_TRY_THRESHOLD && success_count >= need_count)
  {
    compensation_ms = milliseconds / 2;
    if (compensation_ms > 0)
    {
      Serial.printf("[Oil][INFO] prime_delay detected, try_count=%d, compensation_ms=%d\n",
                    try_count,
                    compensation_ms);
      if (!stop->delayMs(compensation_ms))
      {
        stopMotor();
        return false;
      }
    }
  }

  stopMotor();
  Serial.printf("[Oil][INFO] give_oil done need_count=%d try_count=%d success_count=%d compensation_ms=%d\n",
                need_count,
                try_count,
                success_count,
                compensation_ms);

  return success_count >= need_count;
}

void OilControl::backOil()
{
  Serial.printf("回油...\n");
  startMotor(false, 5000);
  Serial.printf("回油结束...\n");
}

void OilControl::startMotor(bool dir, int mills)
{
  EmergencyStopControl *stop = EmergencyStopControl::getInstance();
  if (stop->isRequested())
  {
    stopMotor();
    return;
  }

  if (mills <= 0)
  {
    stopMotor();
    return;
  }

  int kick_ms = min(mills, OIL_PWM_KICK_MS);
  int run_ms = mills - kick_ms;

  setMotorDuty(dir, OIL_PWM_KICK_DUTY);
  if (!stop->delayMs(kick_ms))
  {
    stopMotor();
    return;
  }

  if (run_ms > 0)
  {
    setMotorDuty(dir, OIL_PWM_RUN_DUTY);
    if (!stop->delayMs(run_ms))
    {
      stopMotor();
      return;
    }
  }

  stopMotor();
}

void OilControl::stopImmediately()
{
  stopMotor();
}

void OilControl::setMotorDuty(bool dir, int duty)
{
  if (duty > 0 && EmergencyStopControl::getInstance()->isRequested())
  {
    stopMotor();
    return;
  }

  if (dir)
  {
    ledcWrite(OIL_PWM_CHANNEL_IN1, 0);
    ledcWrite(OIL_PWM_CHANNEL_IN2, duty);
  }
  else
  {
    ledcWrite(OIL_PWM_CHANNEL_IN1, duty);
    ledcWrite(OIL_PWM_CHANNEL_IN2, 0);
  }
}

void OilControl::stopMotor()
{
  ledcWrite(OIL_PWM_CHANNEL_IN1, 0);
  ledcWrite(OIL_PWM_CHANNEL_IN2, 0);
}

bool OilControl::runSelfTest()
{
  if (s_oil_self_test_busy)
  {
    return false;
  }

  s_oil_self_test_busy = true;
  BaseType_t task_ok = xTaskCreatePinnedToCore(oilSelfTestTask,
                                               "oilSelfTestTask",
                                               1024 * 4,
                                               NULL,
                                               2,
                                               NULL,
                                               1);
  if (task_ok != pdPASS)
  {
    s_oil_self_test_busy = false;
    return false;
  }

  return true;
}
