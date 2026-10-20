// 这个电机的作用是把蛋旋转到锅上
// 电流设置为1A
// uart连接方式，成功，

#include <Arduino.h>
#include <TMCStepper.h>
// #include <Streaming.h>
// #include <FlexyStepper.h>
#include "gpio.h"
#include "oil_control.h"
#include "pot_a_motor.h"
#include "tmc_config.h"
#include "emergency_stop_control.h"

namespace
{
volatile bool s_pota_self_test_busy = false;

void potASelfTestTask(void *params)
{
  (void)params;

  PotAMotor::getInstance()->takeOut();
  s_pota_self_test_busy = false;
  vTaskDelete(NULL);
}
}

PotAMotor PotAMotor::instance;

PotAMotor::PotAMotor()
{
}

PotAMotor *PotAMotor::getInstance()
{
  return &instance;
}

void PotAMotor::init(FastAccelStepperEngine *engine)
{
  pinMode(POTA_MOTOR_EN, OUTPUT);
  potAStepper = engine->stepperConnectToPin(POTA_MOTOR_STEP);
  potAStepper->setDirectionPin(POTA_MOTOR_DIR, false);
  potAStepper->setEnablePin(POTA_MOTOR_EN);
  relax();
  // potAStepper->setAutoEnable(true);
}

void PotAMotor::home()
{
  Serial.printf("potAHome\n");
  enable();
  move(1750, true, 5000, 2000);
  relax();
}

void PotAMotor::move(int angle, bool block, int speed, int accel)
{
  Serial.println("potAMove");
  if (EmergencyStopControl::getInstance()->isRequested())
  {
    return;
  }
  if (!TMCConfig::getInstance()->ensureReady(TMC_MOTOR_POTA))
  {
    Serial.println("potAMove skipped: TMC not ready");
    return;
  }
  const uint16_t microsteps = TMCConfig::getInstance()->getMicrosteps(TMC_MOTOR_POTA);
  int steps = ONE_CIRCLE_STEPS * microsteps * (angle * 1.0000 / 360) * (36 / 12);
  potAStepper->setSpeedInHz(speed);
  potAStepper->setAcceleration(accel);
  potAStepper->applySpeedAcceleration();
  potAStepper->move(steps, false);
  if (block)
  {
    while (potAStepper->isRunning())
    {
      if (EmergencyStopControl::getInstance()->isRequested())
      {
        potAStepper->forceStop();
        break;
      }
      vTaskDelay(1);
    }
  }
}

void PotAMotor::takeOut()
{
  EmergencyStopControl *stopControl = EmergencyStopControl::getInstance();
  if (stopControl->isRequested()) return;
  Serial.printf("potATakeOut:\n");
  turnOut();
  if (!stopControl->delayMs(2000)) return;
  turnBack();
}

void PotAMotor::shakeTurn()
{
  Serial.printf("potAShakeTurn\n");
  enable();
  move(-20, true, 10000, 10000);
  vTaskDelay(500);
  move(20, true, 10000, 10000);
}

bool PotAMotor::runSelfTest()
{
  if (s_pota_self_test_busy)
  {
    return false;
  }

  s_pota_self_test_busy = true;
  BaseType_t task_ok = xTaskCreatePinnedToCore(potASelfTestTask,
                                               "potASelfTestTask",
                                               1024 * 4,
                                               NULL,
                                               2,
                                               NULL,
                                               1);
  if (task_ok != pdPASS)
  {
    s_pota_self_test_busy = false;
    return false;
  }

  return true;
}

void PotAMotor::turnOut()
{
  if (EmergencyStopControl::getInstance()->isRequested()) return;
  Serial.printf("potATurnOut:\n");
  enable();
  move(165, true, 10000, 15000);
  // relax();
}
void PotAMotor::turnBack()
{
  if (EmergencyStopControl::getInstance()->isRequested()) return;
  Serial.printf("potATurnBack:\n");
  enable();
  move(-165, true, 5000, 5000);
  relax();
}
void PotAMotor::bumpTurn()
{
  Serial.printf("potABumpTurn:\n");
  enable();
  move(-60, true, 3000, 3000);
  relax();
  vTaskDelay(1000);
}

void PotAMotor::potAStop()
{
  if (potAStepper != NULL)
  {
    potAStepper->forceStop();
  }
}

void PotAMotor::enable()
{
  digitalWrite(POTA_MOTOR_EN, LOW); // 使能
}

void PotAMotor::relax()
{
  digitalWrite(POTA_MOTOR_EN, HIGH); // 失能
}

void IRAM_ATTR PotAMotor::potA_limit()
{
}
