// 这个电机的作用是把蛋旋转到锅上
// 电流设置为1A
// uart连接方式，成功，

#include <Arduino.h>
#include <TMCStepper.h>
// #include <Streaming.h>
// #include <FlexyStepper.h>

#include "gpio.h"
#include "push_motor.h"
#include "emergency_stop_control.h"
#include "oil_control.h"
#include "jiandan_data_manager.h"
#include "tmc_config.h"
#include "trans_motor.h"

PushMotor PushMotor::instance;
volatile uint8_t PushMotor::s_clearOpenDistanceMm = 0;

namespace
{
// 测试动作前推角度。

// 回零时最多回退的找限位角度。
constexpr int PUSH_HOME_FIND_LIMIT_ANGLE = calcAngleByDist(-20);

// 常规打蛋前推角度。
constexpr int PUSH_BEAT_EGG_ANGLE = calcAngleByDist(23.0);

// 常规打蛋完成后回退找限位角度。
constexpr int PUSH_BEAT_EGG_BACK_ANGLE = calcAngleByDist(-30);

// 轻推蛋托前推角度。
constexpr int PUSH_TRAY_FORWARD_ANGLE = calcAngleByDist(5);

// 轻推蛋托完成后回退找限位角度。
constexpr int PUSH_TRAY_BACK_ANGLE = calcAngleByDist(-5);

// 抖落蛋壳时单次往复的角度幅度。
// 注意: 6mm 时高速冲击失步明显，偶现 close limit not reached。降到 3mm 后，
// 抖动惯性仍足以甩落蛋壳，但电机位置漂移显著减小。
constexpr int PUSH_SHAKE_ANGLE = calcAngleByDist(3);

// 倒料动作前推角度。
constexpr int PUSH_DUMP_FORWARD_ANGLE = calcAngleByDist(23);

// 倒料动作完成后回退找限位角度。
constexpr int PUSH_DUMP_BACK_ANGLE = calcAngleByDist(-24);

// 清洗模式打开机构的前推角度。
constexpr uint8_t PUSH_CLEAR_OPEN_DISTANCE_MM = 25;
constexpr uint8_t PUSH_DEBUG_STEP_DISTANCE_MM = 5;
constexpr int PUSH_CLEAR_OPEN_ANGLE = calcAngleByDist(PUSH_CLEAR_OPEN_DISTANCE_MM);

// 清洗模式关闭机构的标称回退角度，当前未直接使用。
constexpr int PUSH_CLEAR_CLOSE_ANGLE = calcAngleByDist(-15);

// 清洗模式关闭机构时实际回退找限位角度。
constexpr int PUSH_CLEAR_BACK_ANGLE = calcAngleByDist(-26);

// 调试分步打开时，每次前推 1/5 清洗打开行程。

// 清洗和调试开合动作的速度、加速度。
constexpr int PUSH_CLEAR_SPEED = 6000;
constexpr int PUSH_CLEAR_ACCEL = 6000;

// 霍尔报呍到位后的“座紧”行程。
// 霍尔 FALLING 在“磁铁刚进入检测场”时触发，此点距“打蛋器真正完全闭合(机械到底)”
// 还差几毫米。到位后再以低速往闭合方向补一小段，把打蛋器压到底。
// 行程取 2mm，远小于霍尔检测场宽，磁铁仍留在检测场内(sensor_pin 保持 LOW)，
// 不会把磁铁推出检测场而破坏下一轮找限位；若闭合处有硬挡块，低速下仅轻微堆转。
constexpr int PUSH_HOME_SEAT_ANGLE = calcAngleByDist(-2);
constexpr int PUSH_SEAT_SPEED = 1500;
constexpr int PUSH_SEAT_ACCEL = 1500;

volatile bool s_push_self_test_busy = false;

bool pushIsAtLimit()
{
  return digitalRead(PUSH_POS_SENSOR) == LOW;
}

void pushSelfTestTask(void *params)
{
  (void)params;

  PushMotor::getInstance()->beatEgg();
  s_push_self_test_busy = false;
  vTaskDelete(NULL);
}

bool moveBackToLimit(PushMotor *motor, int angle, int speed, int accel, const char *tag)
{
  // 霍尔限位为“稳定电平”：磁铁进入检测场后 sensor_pin 持续为 LOW，直至离开。
  // 回退速度下磁铁在检测场内停留数百毫秒，1ms 轮询 pushIsAtLimit() 不会漏检，
  // 因此统一用 pushIsAtLimit() 作为唯一到位依据，无需中断/状态机。
  if (pushIsAtLimit())
  {
    Serial.printf("%s already at limit\n", tag);
    PushMotor::s_clearOpenDistanceMm = 0;
    return true;
  }

  motor->move(angle, true, speed, accel, false, true); // stopAtLimit=true：触到霍尔即停

  if (pushIsAtLimit())
  {
    // 座紧：霍尔触发点距真正完全闭合还差几毫米，低速再往闭合方向压一小段到底。
    Serial.printf("%s seat to home: before sensor_pin=%d\n", tag, digitalRead(PUSH_POS_SENSOR));
    motor->move(PUSH_HOME_SEAT_ANGLE, true, PUSH_SEAT_SPEED, PUSH_SEAT_ACCEL);
    Serial.printf("%s seat to home: after sensor_pin=%d\n", tag, digitalRead(PUSH_POS_SENSOR));
    PushMotor::s_clearOpenDistanceMm = 0;
    return true;
  }

  // 真正的失败：走完整段回退行程仍未触到霍尔（机械位置偏差 / 磁铁方向反 / 霍尔灵敏度）。
  Serial.printf("%s limit not reached, sensor_pin=%d\n", tag, digitalRead(PUSH_POS_SENSOR));
  JiandanDataManager::getInstance()->setErrorCode(PUSH_HOME_FAIL);
  EmergencyStopControl::getInstance()->request();
  motor->stop();
  return false;
}
}

PushMotor::PushMotor()
{
}

PushMotor *PushMotor::getInstance()
{
  return &instance;
}

void PushMotor::init(FastAccelStepperEngine *engine)
{
  
  pinMode(PUSH_POS_SENSOR, INPUT_PULLUP);
  pinMode(PUSH_MOTOR_EN, OUTPUT);
  pinMode(PUSH_MOTOR_STEP, OUTPUT);
  pinMode(PUSH_MOTOR_DIR, OUTPUT);

  constexpr uint8_t TMC_DRIVER_ADDRESS = 0b00;


  // 打蛋
  stepper = engine->stepperConnectToPin(PUSH_MOTOR_STEP);
  stepper->setDirectionPin(PUSH_MOTOR_DIR, false);
  relax();
}

void PushMotor::home()
{
  (void)homeInternal(false);
}

bool PushMotor::homeAfterEmergencyStop()
{
  return homeInternal(true);
}

bool PushMotor::homeInternal(bool allowDuringEmergencyStop)
{
  Serial.printf("pushHome\n");
  enable();

  // 未在限位则回退找限位；霍尔为稳定电平，直接轮询 pushIsAtLimit() 即可可靠捕捉。
  if (!pushIsAtLimit())
  {
    Serial.printf("push out limited\n");
    move(PUSH_HOME_FIND_LIMIT_ANGLE, true, 3000, 2000,
         allowDuringEmergencyStop, true); // stopAtLimit=true，回退最多 20 mm 找限位
  }

  const bool reached = pushIsAtLimit();
  if (reached)
  {
    Serial.printf("push in limited\n");
    s_clearOpenDistanceMm = 0;
  }
  else
  {
    Serial.printf("pushHome failed, sensor_pin=%d\n", digitalRead(PUSH_POS_SENSOR));
  }
  relax();
  return reached;
}

void PushMotor::enable()
{
  digitalWrite(PUSH_MOTOR_EN, LOW);
}

void PushMotor::relax()
{
  digitalWrite(PUSH_MOTOR_EN, HIGH);
}

void PushMotor::move(int angle, bool block, int speed, int accel,
                     bool allowDuringEmergencyStop, bool stopAtLimit)
{
  Serial.println("pushMove");
  if (!allowDuringEmergencyStop && EmergencyStopControl::getInstance()->isRequested())
  {
    return;
  }
  if (!TMCConfig::getInstance()->ensureReady(TMC_MOTOR_PUSH))
  {
    Serial.println("pushMove skipped: TMC not ready");
    return;
  }
  const uint16_t microsteps = TMCConfig::getInstance()->getMicrosteps(TMC_MOTOR_PUSH);
  int steps = ONE_CIRCLE_STEPS * microsteps * (angle * 1.0000 / 360);
  stepper->setSpeedInHz(speed);
  stepper->setAcceleration(accel);
  stepper->applySpeedAcceleration();
  if (!block)
  {
    stepper->move(steps, false);
    return;
  }

  stepper->move(steps, false);
  while (stepper->isRunning())
  {
    if (!allowDuringEmergencyStop && EmergencyStopControl::getInstance()->isRequested())
    {
      stepper->forceStop();
      break;
    }
    // 霍尔为稳定电平：回退/回零时一旦读到 LOW 即抵达限位，立即强停。
    if (stopAtLimit && pushIsAtLimit())
    {
      stepper->forceStop();
      while (stepper->isRunning())
      {
        vTaskDelay(1);
      }
      break;
    }
    vTaskDelay(1);
  }
}

void PushMotor::beatEgg()
{
  Serial.printf("打蛋:\n");
  enable();
  move(PUSH_BEAT_EGG_ANGLE, true, 6000, 6000); // 常规打蛋前推
  vTaskDelay(2500);
  moveBackToLimit(this, PUSH_BEAT_EGG_BACK_ANGLE, 4000, 4000, "Push beatEgg back"); // 常规打蛋后回退找限位
  vTaskDelay(3000);
  relax();
}

void PushMotor::beatEggNoClose()
{
  EmergencyStopControl *stopControl = EmergencyStopControl::getInstance();
  if (stopControl->isRequested()) return;
  Serial.printf("打蛋不关闭:\n");
  enable();
  move(PUSH_BEAT_EGG_ANGLE, true, 6000, 6000); // 打蛋但不关闭机构
  if (stopControl->isRequested()) return;
  if (!stopControl->delayMs(2500)) return;
  //抖蛋液
  TransMotor::getInstance()->shakeSideways();

  if (!stopControl->delayMs(4000)) return;
  // 不关闭电机
}

void PushMotor::pushTray()
{
  Serial.printf("轻推蛋托:\n");
  enable();
  move(PUSH_TRAY_FORWARD_ANGLE, true, 7000, 10000); // 前推 5 mm
  vTaskDelay(2000);
  moveBackToLimit(this, PUSH_TRAY_BACK_ANGLE, 7000, 10000, "Push tray back"); // 回退 5 mm
  relax();
}

// 抖落蛋壳：在打蛋器处于前伸(夹壳)状态下，以高加速度做小幅往复运动，
// 利用急启急停的惯性冲击把黏在打蛋器上的蛋壳甩落。
// 注意：往复始终在“回抽侧”进行(先回抽再前推)，净位移为 0，
// 不会超过当前前伸位置，避免撞击机械上限。
// 收尾：循环结束后强制回退找一次限位，消除高速冲击累积的失步，
// 避免后续 close() 偶现 "limit not reached"。
void PushMotor::pushShakeshell(int times)
{
  Serial.printf("抖落蛋壳:\n");
  enable();

  constexpr int SHAKE_SPEED = 10000;    // 高速，配合大加速度产生冲击
  constexpr int SHAKE_ACCEL = 10000;    // 大加速度，急启急停甩落蛋壳

  for (int i = 0; i < times; i++)
  {
    if (EmergencyStopControl::getInstance()->isRequested()) return;
    move(-PUSH_SHAKE_ANGLE, true, SHAKE_SPEED, SHAKE_ACCEL); // 快速回抽
    if (EmergencyStopControl::getInstance()->isRequested()) return;
    move(PUSH_SHAKE_ANGLE, true, SHAKE_SPEED, SHAKE_ACCEL);  // 快速前推复位
  }

  // 失步恢复：抖动后立即回退找限位。失败会触发 PUSH_HOME_FAIL + 急停。
  if (!EmergencyStopControl::getInstance()->isRequested())
  {
    moveBackToLimit(this, PUSH_BEAT_EGG_BACK_ANGLE, 4000, 4000, "Push shake back");
  }

  relax();
}

bool PushMotor::runSelfTest()
{
  if (s_push_self_test_busy)
  {
    return false;
  }

  s_push_self_test_busy = true;
  BaseType_t task_ok = xTaskCreatePinnedToCore(pushSelfTestTask,
                                               "pushSelfTestTask",
                                               1024 * 4,
                                               NULL,
                                               2,
                                               NULL,
                                               1);
  if (task_ok != pdPASS)
  {
    s_push_self_test_busy = false;
    return false;
  }

  return true;
}

void PushMotor::pushForDump()
{
  Serial.printf("Push For Dump:\n");
  enable();
  move(PUSH_DUMP_FORWARD_ANGLE, true, 6000, 10000); // 倒料前推
  vTaskDelay(2000);
  moveBackToLimit(this, PUSH_DUMP_BACK_ANGLE, 6000, 10000, "Push dump back"); // 倒料后回退找限位
  relax();
}

void PushMotor::stepOpenForDebug()
{
  Serial.printf("Push Step Open For Debug:\n");

  if (pushIsAtLimit())
  {
    s_clearOpenDistanceMm = 0;
  }

  if (s_clearOpenDistanceMm == 0 && !pushIsAtLimit())
  {
    Serial.printf("Push Step Open For Debug: position unknown, homing first\n");
    home();
  }

  // 调试用途：每次固定推进一小步，不再限制最大开合行程（去掉到顶拦截）。
  // 计数器仅用于原点跟踪，饱和到上限即可，防止 uint8 溢出。
  enable();
  move(calcAngleByDist(PUSH_DEBUG_STEP_DISTANCE_MM), true, PUSH_CLEAR_SPEED, PUSH_CLEAR_ACCEL);
  if (s_clearOpenDistanceMm < PUSH_CLEAR_OPEN_DISTANCE_MM)
  {
    s_clearOpenDistanceMm += PUSH_DEBUG_STEP_DISTANCE_MM;
  }
  relax();
}

void PushMotor::pushTest(int forward, float c)
{
  enable();
  move(forward * 360 * c, true, 4000, 4000);
  relax();
}

void PushMotor::stop()
{
  // stepper->stopMove();
  if (stepper != NULL)
  {
    stepper->forceStop();
  }
}

void PushMotor::openForClear()
{
  Serial.printf("Push Open For Clear:\n");

  if (s_clearOpenDistanceMm >= PUSH_CLEAR_OPEN_DISTANCE_MM)
  {
    Serial.printf("Push Open For Clear ignored: already opened for cleaning\n");
    return;
  }

  // 先回到已知原点，再执行固定开合行程，避免重复点按时继续外推。
  if (!pushIsAtLimit())
  {
    Serial.printf("Push Open For Clear: not at home, homing first\n");
    home();
  }

  if (!pushIsAtLimit())
  {
    Serial.printf("Push Open For Clear aborted: failed to reach home position\n");
    return;
  }

  enable();
  move(PUSH_CLEAR_OPEN_ANGLE, true, PUSH_CLEAR_SPEED, PUSH_CLEAR_ACCEL);
  s_clearOpenDistanceMm = PUSH_CLEAR_OPEN_DISTANCE_MM;
  relax();
}

void PushMotor::close()
{
  Serial.printf("Push Close: sensor_pin=%d\n", digitalRead(PUSH_POS_SENSOR));
  if (pushIsAtLimit())
  {
    Serial.printf("Push Close: already at home position\n");
    s_clearOpenDistanceMm = 0;
    relax();
    return;
  }

  enable();

  // moveBackToLimit 内部失败时会自动设 PUSH_HOME_FAIL 错误码并请求急停。
  // 这里再做一次 home() 兜底，覆盖"已经过冲到限位后方但又脱离传感器"的极少场景。
  const bool ok = moveBackToLimit(this, PUSH_CLEAR_BACK_ANGLE, 5000, 5000, "Push close");
  if (!ok)
  {
    Serial.printf("Push Close fallback home engaged\n");
    (void)homeInternal(true);
  }

  relax();
}
