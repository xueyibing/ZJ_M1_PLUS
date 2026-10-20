// 这个电机的作用是把蛋旋转到锅上
// 电流设置为1A
// uart连接方式，成功，

#include <Arduino.h>
#include <atomic>
#include <stdint.h>
#include <TMCStepper.h>
// #include <Streaming.h>
// #include <FlexyStepper.h>

#include "gpio.h"
#include "trans_motor.h"
#include "emergency_stop_control.h"
#include "oil_control.h"
#include "jiandan_data_manager.h"
#include "push_motor.h"
#include "tmc_config.h"

namespace
{
  volatile bool s_trans_self_test_busy = false;
  const float TRANS_HOME_SEARCH_ANGLE = 180.0f;
  const float TRANS_HOME_PRELOAD_ANGLE = 3.0f;
  const float TRANS_POTB_OFFSET_FROM_HOME_ANGLE = 111.0f;
  const float TRANS_POTA_OFFSET_FROM_HOME_ANGLE = 60.5f;
  const float TRANS_DUMP_OFFSET_FROM_HOME_ANGLE = 120.0f;
  const float TRANS_HOME_FROM_DUMP_APPROACH_ANGLE = -125.0f;
  const int TRANS_HOME_SEARCH_SPEED = 1000;
  const int TRANS_HOME_SEARCH_ACCEL = 1000;
  const int TRANS_HOME_PRELOAD_SPEED = 1000;
  const int TRANS_HOME_PRELOAD_ACCEL = 1000;
  const TickType_t TRANS_HOME_SETTLE_DELAY_MS = 500;

  // 位置开关极性：转运臂位于“靠锅(make)一侧”时，TRANS_POS_SENSOR 读到的电平。
  // 硬件把开关电平变化方向反了之后，此处由原来的 HIGH 改为 LOW。
  // 说明：home 触发用的是 CHANGE 双边沿中断，不受极性影响；真正依赖电平的只有
  // “判断当前在 make 侧还是 dump 侧”，统一走 transIsInMake()。以后若再反，只改这一处。
  constexpr int TRANS_SENSOR_MAKE_LEVEL = LOW;

  bool transIsInMake()
  {
    return digitalRead(TRANS_POS_SENSOR) == TRANS_SENSOR_MAKE_LEVEL;
  }

  bool runHomeSearch(TransMotor *motor, bool startInMake, float searchAngle,
                     bool allowDuringEmergencyStop = false)
  {
    TransMotor::s_state = startInMake ? TRANS_IN_MAKE : TRANS_IN_DUMP;
    TransMotor::s_limitTriggered = false;
    attachInterrupt(TRANS_POS_SENSOR, TransMotor::trans_limit, CHANGE);
    motor->move(searchAngle, true, TRANS_HOME_SEARCH_SPEED, TRANS_HOME_SEARCH_ACCEL,
          allowDuringEmergencyStop);
    detachInterrupt(TRANS_POS_SENSOR);
    return TransMotor::s_state == TRANS_IN_MIDDLE;
  }

  void transSelfTestTask(void *params)
  {
    (void)params;

    TransMotor::getInstance()->dumpMock();
    s_trans_self_test_busy = false;
    vTaskDelete(NULL);
  }
}

TransMotor TransMotor::instance;
volatile TransMotorState TransMotor::s_state;
volatile bool TransMotor::s_limitTriggered = false;
TransMotor::TransMotor()
{
}

TransMotor *TransMotor::getInstance()
{
  return &instance;
}

void TransMotor::init(FastAccelStepperEngine *engine)
{
  pinMode(TRANS_MOTOR_EN, OUTPUT);
  pinMode(TRANS_POS_SENSOR, INPUT_PULLDOWN);
  digitalWrite(TRANS_MOTOR_EN, HIGH); // 断电
  // 送蛋
  stepper = engine->stepperConnectToPin(TRANS_MOTOR_STEP);
  stepper->setDirectionPin(TRANS_MOTOR_DIR, false);
  relax();
}

void TransMotor::home()
{
  (void)homeInternal(false);
}

bool TransMotor::homeAfterEmergencyStop()
{
  return homeInternal(true);
}

bool TransMotor::homeInternal(bool allowDuringEmergencyStop)
{
  s_limitTriggered = false;
  enable();
  const bool startInMake = transIsInMake();
  if (startInMake)
  {
    Serial.printf("trans in make \n");
        move(-TRANS_HOME_PRELOAD_ANGLE, true, TRANS_HOME_PRELOAD_SPEED, TRANS_HOME_PRELOAD_ACCEL,
          allowDuringEmergencyStop);
  }
  else
  {
    Serial.printf("trans in dump\n");
    move(TRANS_HOME_PRELOAD_ANGLE, true, TRANS_HOME_PRELOAD_SPEED, TRANS_HOME_PRELOAD_ACCEL,
         allowDuringEmergencyStop);
  }

  const bool homed = runHomeSearch(this, startInMake,
                                   startInMake ? TRANS_HOME_SEARCH_ANGLE : -TRANS_HOME_SEARCH_ANGLE,
                                   allowDuringEmergencyStop);
  if (homed)
  {
    stepper->setCurrentPosition(0);
  }
  else
  {
    s_state = TRANS_UNINIT;
  }

  vTaskDelay(TRANS_HOME_SETTLE_DELAY_MS);
  relax();

  if (homed)
  {
    Serial.println("trans homed");
  }
  else
  {
    Serial.printf("trans home failed, sensor=%d\n", digitalRead(TRANS_POS_SENSOR));
  }
  return homed;
}

void TransMotor::homeFromDump()
{
  enable();
  move(TRANS_HOME_FROM_DUMP_APPROACH_ANGLE, true, 6000, 6000);

  const bool startInMake = transIsInMake();
  if (startInMake)
  {
    Serial.println("trans homeFromDump start in make");
  }
  else
  {
    Serial.println("trans homeFromDump start in dump");
  }

  const bool homed = runHomeSearch(this, startInMake, startInMake ? TRANS_HOME_SEARCH_ANGLE : -TRANS_HOME_SEARCH_ANGLE);
  if (homed)
  {
    stepper->setCurrentPosition(0);
    Serial.println("trans homeFromDump ok");
  }
  else
  {
    s_state = TRANS_UNINIT;
    Serial.printf("trans homeFromDump failed, sensor=%d\n", digitalRead(TRANS_POS_SENSOR));
  }

  relax();
}

void TransMotor::dumpMock()
{
  // home();
  // vTaskDelay(500);

  // goPotB();
  // vTaskDelay(500);

  goDumpFromHome();
  vTaskDelay(500);

  homeFromDump();
}

void TransMotor::shakeSideways()
{
  enable();
  for (int i = 0; i < 3; i++)
  {
    move(15, true, 25000, 25000);
    move(-15, true, 25000, 25000);
  }
  relax();
}

bool TransMotor::runSelfTest()
{
  if (s_trans_self_test_busy)
  {
    return false;
  }

  s_trans_self_test_busy = true;
  BaseType_t task_ok = xTaskCreatePinnedToCore(transSelfTestTask,
                                               "transSelfTestTask",
                                               1024 * 4,
                                               NULL,
                                               2,
                                               NULL,
                                               1);
  if (task_ok != pdPASS)
  {
    s_trans_self_test_busy = false;
    return false;
  }

  return true;
}

void IRAM_ATTR TransMotor::trans_limit()
{
  if (s_state == TRANS_IN_DUMP || s_state == TRANS_IN_MAKE)
  {
    s_state = TRANS_IN_MIDDLE;
    s_limitTriggered = true;
  }
}

void TransMotor::enable()
{
  digitalWrite(TRANS_MOTOR_EN, LOW); // 使能
}

void TransMotor::relax()
{
  digitalWrite(TRANS_MOTOR_EN, HIGH); // 断电
}

void TransMotor::move(float angle, bool block, int speed, int accel,
                      bool allowDuringEmergencyStop)
{
  static std::atomic<uint32_t> moveSequence{0};
  const uint32_t moveId = moveSequence.fetch_add(1, std::memory_order_relaxed) + 1;
  const uint32_t moveStartMs = millis();
  const int32_t startPosition = stepper != NULL ? stepper->getCurrentPosition() : 0;
  Serial.printf("[TRANS][MOVE] id=%lu phase=start ms=%lu angle=%.2f speed=%d accel=%d block=%d cmd_pos=%ld sensor=%d estop=%d\n",
                static_cast<unsigned long>(moveId), static_cast<unsigned long>(moveStartMs),
                angle, speed, accel, block ? 1 : 0, static_cast<long>(startPosition),
                digitalRead(TRANS_POS_SENSOR),
                EmergencyStopControl::getInstance()->isRequested() ? 1 : 0);
  if (!allowDuringEmergencyStop && EmergencyStopControl::getInstance()->isRequested())
  {
    Serial.printf("[TRANS][MOVE] id=%lu result=skipped reason=emergency_before_start\n",
                  static_cast<unsigned long>(moveId));
    return;
  }
  if (!TMCConfig::getInstance()->ensureReady(TMC_MOTOR_TRANS))
  {
    Serial.printf("[TRANS][MOVE] id=%lu result=skipped reason=tmc_not_ready\n",
                  static_cast<unsigned long>(moveId));
    return;
  }
  uint8_t gstatStart = 0;
  const bool hasGstatStart = TMCConfig::getInstance()->readGstat(TMC_MOTOR_TRANS, gstatStart);
  const uint16_t microsteps = TMCConfig::getInstance()->getMicrosteps(TMC_MOTOR_TRANS);
  int steps = ONE_CIRCLE_STEPS * microsteps * (angle * 1.0000 / 360) * (60 / 12);
  const int64_t targetPosition = static_cast<int64_t>(startPosition) + steps;
  Serial.printf("[TRANS][MOVE] id=%lu phase=armed steps=%ld target_cmd_pos=%lld microsteps=%u gstat_start=%s0x%02X\n",
                static_cast<unsigned long>(moveId), static_cast<long>(steps),
                static_cast<long long>(targetPosition), microsteps,
                hasGstatStart ? "" : "unavailable/", gstatStart);
  stepper->setSpeedInHz(speed);
  stepper->setAcceleration(accel);
  stepper->applySpeedAcceleration();
  if (!block)
  {
    stepper->move(steps, false);
    Serial.printf("[TRANS][MOVE] id=%lu result=queued cmd_pos=%ld\n",
                  static_cast<unsigned long>(moveId),
                  static_cast<long>(stepper->getCurrentPosition()));
    return;
  }

  const char *stopReason = "completed";
  constexpr uint32_t TMC_DIAGNOSTIC_POLL_INTERVAL_MS = 100;
  uint32_t nextGstatPollMs = moveStartMs + TMC_DIAGNOSTIC_POLL_INTERVAL_MS;
  bool driverResetObserved = false;
  uint32_t driverResetObservedElapsedMs = 0;
  s_limitTriggered = false;
  stepper->move(steps, false);
  while (stepper->isRunning())
  {
    if (!allowDuringEmergencyStop && EmergencyStopControl::getInstance()->isRequested())
    {
      stopReason = "emergency_stop";
      stepper->forceStop();
      break;
    }
    if (s_limitTriggered)
    {
      stopReason = "position_sensor_limit";
      s_limitTriggered = false;
      stepper->forceStop();
      while (stepper->isRunning())
      {
        vTaskDelay(1);
      }
      break;
    }

    const uint32_t nowMs = millis();
    if (static_cast<int32_t>(nowMs - nextGstatPollMs) >= 0)
    {
      uint8_t gstat = 0;
      if (TMCConfig::getInstance()->readGstat(TMC_MOTOR_TRANS, gstat) &&
          (gstat & 0x05U) != 0 && !driverResetObserved)
      {
        driverResetObserved = true;
        driverResetObservedElapsedMs = nowMs - moveStartMs;
        const int32_t observedPosition = stepper->getCurrentPosition();
        const int64_t observedDelta = static_cast<int64_t>(observedPosition) - startPosition;
        const int64_t observedRemaining = static_cast<int64_t>(steps) - observedDelta;
        Serial.printf("[TRANS][MOVE] id=%lu phase=driver_reset_first_seen elapsed_ms=%lu gstat=0x%02X cmd_pos=%ld cmd_delta=%lld remaining_steps=%lld sensor=%d estop=%d\n",
                      static_cast<unsigned long>(moveId),
                      static_cast<unsigned long>(driverResetObservedElapsedMs), gstat,
                      static_cast<long>(observedPosition), static_cast<long long>(observedDelta),
                      static_cast<long long>(observedRemaining), digitalRead(TRANS_POS_SENSOR),
                      EmergencyStopControl::getInstance()->isRequested() ? 1 : 0);
      }
      // One poll per interval at most; do not spin on a delayed task wake-up.
      nextGstatPollMs = nowMs + TMC_DIAGNOSTIC_POLL_INTERVAL_MS;
    }
    vTaskDelay(1);
  }

  const int32_t endPosition = stepper->getCurrentPosition();
  const int64_t commandedDelta = static_cast<int64_t>(endPosition) - startPosition;
  const int64_t remainingSteps = static_cast<int64_t>(steps) - commandedDelta;
  uint8_t gstatEnd = 0;
  const bool hasGstatEnd = TMCConfig::getInstance()->readGstat(TMC_MOTOR_TRANS, gstatEnd);
  const bool positionCommandComplete = remainingSteps == 0;
  Serial.printf("[TRANS][MOVE] id=%lu phase=end result=%s reason=%s elapsed_ms=%lu steps=%ld cmd_delta=%lld remaining_steps=%lld cmd_pos=%ld sensor=%d estop=%d reset_seen_during_move=%d reset_first_seen_ms=%lu gstat_end=%s0x%02X reset=%d uv_cp=%d drv_err=%d\n",
                static_cast<unsigned long>(moveId),
                positionCommandComplete ? "command_complete" : "command_partial",
                stopReason,
                static_cast<unsigned long>(millis() - moveStartMs), static_cast<long>(steps),
                static_cast<long long>(commandedDelta), static_cast<long long>(remainingSteps),
                static_cast<long>(endPosition), digitalRead(TRANS_POS_SENSOR),
                EmergencyStopControl::getInstance()->isRequested() ? 1 : 0,
                driverResetObserved ? 1 : 0,
                static_cast<unsigned long>(driverResetObservedElapsedMs),
                hasGstatEnd ? "" : "unavailable/", gstatEnd,
                (gstatEnd & 0x01U) ? 1 : 0,
                (gstatEnd & 0x04U) ? 1 : 0,
                (gstatEnd & 0x02U) ? 1 : 0);
}

void TransMotor::goPotB()
{
  enable();
  move(-TRANS_POTB_OFFSET_FROM_HOME_ANGLE, true, 8000, 8000);
  relax();
}

void TransMotor::goPotA()
{
  enable();
  move(-TRANS_POTA_OFFSET_FROM_HOME_ANGLE, true, 8000, 8000);
  relax();
}


void TransMotor::backHomeFromPot(bool fromPotA)
{
  enable();
  Serial.println("goHomeFromPot");
  const float homeAngle = fromPotA ? TRANS_POTA_OFFSET_FROM_HOME_ANGLE : TRANS_POTB_OFFSET_FROM_HOME_ANGLE;
  move(homeAngle, true, 8000, 8000);
  relax();
}

void TransMotor::goDumpFromHome()
{
  enable();
  Serial.println("goDumpFromHome");
  move(TRANS_DUMP_OFFSET_FROM_HOME_ANGLE, true, 8000, 8000);
  relax();
}


void TransMotor::dumpRetry()
{
  // 继续清理蛋壳
  enable();
  Serial.println("goDumpFromPot");
  // 泡蛋壳230°，磕一下
  move(150, true, 12000, 12000);
  vTaskDelay(500);
  PushMotor::getInstance()->pushForDump();
  vTaskDelay(500);
  homeFromDump();
  relax();
}

void TransMotor::clearBeat()
{
  enable();
  Serial.println("clearBeat");
  move(110, true, 8000, 8000);
}

void TransMotor::stop()
{
  if (stepper != NULL)
  {
    Serial.printf("[TRANS][STOP] ms=%lu running=%d cmd_pos=%ld estop=%d sensor=%d\n",
                  static_cast<unsigned long>(millis()), stepper->isRunning() ? 1 : 0,
                  static_cast<long>(stepper->getCurrentPosition()),
                  EmergencyStopControl::getInstance()->isRequested() ? 1 : 0,
                  digitalRead(TRANS_POS_SENSOR));
    stepper->forceStop();
  }
}
