// 这个电机的作用是把蛋旋转到锅上
// 电流设置为1A
// uart连接方式，成功，

#include <Arduino.h>
#include <TMCStepper.h>
// #include <Streaming.h>
// #include <FlexyStepper.h>

#include "gpio.h"
#include "pot_b_motor.h"
#include "emergency_stop_control.h"
#include "oil_control.h"
#include "jiandan_data_manager.h"
#include "tmc_config.h"

namespace
{
volatile bool s_potb_self_test_busy = false;

void potBSelfTestTask(void *params)
{
  (void)params;

  PotBMotor::getInstance()->turn();
  s_potb_self_test_busy = false;
  vTaskDelete(NULL);
}
}

PotBMotor PotBMotor::instance;
PotBMotorState PotBMotor::s_state = PotBMotorState::POTB_UNINIT;

PotBMotor::PotBMotor()
{
}

PotBMotor *PotBMotor::getInstance()
{
  return &instance;
}

void PotBMotor::init(FastAccelStepperEngine *engine)
{
  pinMode(POTB_MOTOR_EN, OUTPUT);
  potBStepper = engine->stepperConnectToPin(POTB_MOTOR_STEP);
  potBStepper->setDirectionPin(POTB_MOTOR_DIR, false);
  relax();
  // 因为stepper库无法控制电机保持力矩，所以这里注释掉，手动控制电机通电
  // potBStepper->setEnablePin(POTB_MOTOR_EN);
  // potBStepper->setAutoEnable(true);
}


void PotBMotor::home()
{
  Serial.println("potBMotorHome");
  enable();
  move(1750, true, 5000, 2000);
  relax();
}

void IRAM_ATTR PotBMotor::potB_limit()
{
}

void PotBMotor::move(int angle, bool block, int speed, int accel)
{
  Serial.println("potBMove");
  if (EmergencyStopControl::getInstance()->isRequested())
  {
    return;
  }
  if (!TMCConfig::getInstance()->ensureReady(TMC_MOTOR_POTB))
  {
    Serial.println("potBMove skipped: TMC not ready");
    return;
  }
  const uint16_t microsteps = TMCConfig::getInstance()->getMicrosteps(TMC_MOTOR_POTB);
  int steps = ONE_CIRCLE_STEPS * microsteps * (angle * 1.0000 / 360) * (36 / 12);
  potBStepper->setSpeedInHz(speed);
  potBStepper->setAcceleration(accel);
  potBStepper->applySpeedAcceleration();
  potBStepper->move(steps, false);
  if (block)
  {
    while (potBStepper->isRunning())
    {
      if (EmergencyStopControl::getInstance()->isRequested())
      {
        potBStepper->forceStop();
        break;
      }
      vTaskDelay(1);
    }
  }
}

void PotBMotor::turn()
{
  EmergencyStopControl *stopControl = EmergencyStopControl::getInstance();
  if (stopControl->isRequested()) return;
  Serial.printf("potBTurnOut\n");
  enable();
  move(155, true, 18000, 18000);
  if (!stopControl->delayMs(500)) return;
  // shakeTurn();
  if (!stopControl->delayMs(1000)) return;
  move(-155, true, 7000, 7000);
  if (stopControl->isRequested()) return;
  relax();
}

void PotBMotor::stop()
{
  if (potBStepper != NULL)
  {
    potBStepper->forceStop();
  }
}

void PotBMotor::shakeTurn()
{
  Serial.printf("potBShakeTurn\n");
  enable();
  move(-40, true, 10000, 10000);
  vTaskDelay(500);
  move(40, true, 10000, 10000);
}

bool PotBMotor::runSelfTest()
{
  if (s_potb_self_test_busy)
  {
    return false;
  }

  s_potb_self_test_busy = true;
  BaseType_t task_ok = xTaskCreatePinnedToCore(potBSelfTestTask,
                                               "potBSelfTestTask",
                                               1024 * 4,
                                               NULL,
                                               2,
                                               NULL,
                                               1);
  if (task_ok != pdPASS)
  {
    s_potb_self_test_busy = false;
    return false;
  }

  return true;
}

void PotBMotor::relax()
{
  digitalWrite(POTB_MOTOR_EN, HIGH); // 失能
}

void PotBMotor::enable()
{
  digitalWrite(POTB_MOTOR_EN, LOW); // 使能
}
