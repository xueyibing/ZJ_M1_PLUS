#include "jiandan_maker.h"
#include <Arduino.h>
#include "pot_i2c.h"
#include "oil_control.h"
#include "pot_a_motor.h"
#include "pot_b_motor.h"
#include "egg_control.h"
#include "cyd_tof.h"
#include "trans_motor.h"
#include "push_motor.h"
#include "fan.h"
#include "common_data_manager.h"
#include <math.h>
#include <stdlib.h>
#include "oil_servo.h"
#include "debug_run_control.h"
#include "emergency_stop_control.h"

#define MAKER_LOG_INFO(stage, fmt, ...) Serial.printf("[JiandanMaker][INFO][%s] " fmt "\n", stage, ##__VA_ARGS__)
#define MAKER_LOG_WARN(stage, fmt, ...) Serial.printf("[JiandanMaker][WARN][%s] " fmt "\n", stage, ##__VA_ARGS__)
#define MAKER_LOG_ERROR(stage, fmt, ...) Serial.printf("[JiandanMaker][ERROR][%s] " fmt "\n", stage, ##__VA_ARGS__)

namespace
{
  // m_mutex负责串行化机械动作；该临界区只保护锅内蛋状态的短读写。
  // 两者分离，避免为了读取两个bool而阻塞机械任务，同时保证B->A是原子迁移。
  portMUX_TYPE pot_empty_state_mux = portMUX_INITIALIZER_UNLOCKED;
  portMUX_TYPE maker_lifecycle_mux = portMUX_INITIALIZER_UNLOCKED;

  constexpr unsigned long POTA_HEAT_LEAD_MS = 15UL * 1000UL;
  constexpr unsigned long POTA_HEAT_END_OFFSET_MS = 3UL * 1000UL;
  constexpr unsigned long POTB_HEAT_END_OFFSET_MS = 5UL * 1000UL;
  constexpr int POT_TEMP_POINT_CAPACITY = 200;
  constexpr float FAN_COOLDOWN_DELTA_C = 10.0f;
  constexpr int AUTO_DYNAMIC_DW_MIN = 1;
  constexpr int AUTO_DYNAMIC_DW_MAX_A = 4;
  constexpr int AUTO_DYNAMIC_DW_MAX_B = 5;
  constexpr int HAVE_EGG_DISTANCE_MAX = 100;
  constexpr int HAVE_EGG_DISTANCE_MIN = 50;
  // 玻璃片被污垢遮挡时的近距下限，上限紧邻有蛋窗口下沿。
  constexpr int BLOCK_DISTANCE_MIN = 10;
  // 过热软上限余量：锅温超过目标温度+该值时，本秒关闭该锅加热（不报错、不停机）
  constexpr int OVERHEAT_MARGIN_C = 50;

  ErrorCode getPotErrorCode(bool isPotA, POT_STATE state)
  {
    switch (state)
    {
    case IIC_CONN_ERROR:
      return isPotA ? POTA_IIC_ERROR : POTB_IIC_ERROR;
    case POT_NO:
      return isPotA ? POTA_NO : POTB_NO;
    case POT_NO_COIL:
      return isPotA ? POTA_NO_COIL : POTB_NO_COIL;
    case NTC_ERROR:
      return isPotA ? POTA_TMP_SENSOR_ERROR : POTB_TMP_SENSOR_ERROR;
    case POT_OK:
    default:
      return ALL_OK;
    }
  }

  ErrorCode getCydTofErrorCode(CydTofCheckState state)
  {
    return state == CydTofCheckState::BLOCKED ? CYD_TOF_HAVE_BLOCK : CYD_TOF_HAVE_ERROR;
  }

  int getTargetTempA()
  {
    // A锅煎第二面，温度可以低一些，避免过度加热导致蛋变老
    return JiandanDataManager::getInstance()->getMaxPotTempLimit() - 20;
  }

  int getTargetTempB()
  {
    return JiandanDataManager::getInstance()->getMaxPotTempLimit();
  }

  bool shouldKeepFanRunningForCooldown()
  {
    const float threshold = CommonDataManager::getInstance()->getMachineEnvTemp() + FAN_COOLDOWN_DELTA_C;
    const float tempA = (float)PotI2c::getInstance()->getTempA();
    const float tempB = (float)PotI2c::getInstance()->getTempB();

    return tempA > threshold || tempB > threshold;
  }

  void stopAllHeating()
  {
    PotI2c::getInstance()->setPotADW(0, 0);
    PotI2c::getInstance()->setPotBDW(0, 0);
    JiandanDataManager::getInstance()->setPotARemainingHeatTime(0);
    JiandanDataManager::getInstance()->setPotBRemainingHeatTime(0);
    JiandanDataManager::getInstance()->setTotalMakeTime(0);
  }

  int getRemainingSeconds(unsigned long deadlineMs)
  {
    unsigned long now = millis();
    if (deadlineMs <= now)
    {
      return 0;
    }
    return (int)((deadlineMs - now + 999UL) / 1000UL);
  }

  void resetHeatRemainingTimes()
  {
    JiandanDataManager::getInstance()->setPotARemainingHeatTime(0);
    JiandanDataManager::getInstance()->setPotBRemainingHeatTime(0);
  }

  bool shouldAbortPreheat()
  {
    JiandanDataManager *dataMg = JiandanDataManager::getInstance();
    if (!dataMg->getStart() || dataMg->getErrorCode() != ALL_OK || dataMg->getNeedMakeEggs() <= 0)
    {
      return true;
    }
    return false;
  }

  int reduceDwNearTarget(int baseDw, int currentTemp, int targetTemp)
  {
    if (currentTemp >= targetTemp - 2)
    {
      return AUTO_DYNAMIC_DW_MIN;
    }

    if (currentTemp >= targetTemp - 8)
    {
      return baseDw > 2 ? baseDw - 2 : AUTO_DYNAMIC_DW_MIN;
    }

    if (currentTemp >= targetTemp - 15)
    {
      return baseDw > AUTO_DYNAMIC_DW_MIN ? baseDw - 1 : AUTO_DYNAMIC_DW_MIN;
    }

    return baseDw;
  }

  int calculatePhaseDwA(int remainingDynamicTime, int totalDynamicTime)
  {
    if (totalDynamicTime <= 0)
    {
      return AUTO_DYNAMIC_DW_MIN;
    }

    int elapsedDynamicTime = totalDynamicTime - remainingDynamicTime;
    if (elapsedDynamicTime < 0)
    {
      elapsedDynamicTime = 0;
    }
    if (elapsedDynamicTime < totalDynamicTime / 3)
    {
      return AUTO_DYNAMIC_DW_MAX_A;
    }
    if (elapsedDynamicTime < (totalDynamicTime * 2) / 3)
    {
      return 3;
    }
    if (elapsedDynamicTime < (totalDynamicTime * 5) / 6)
    {
      return 2;
    }
    return AUTO_DYNAMIC_DW_MIN;
  }

  int calculatePhaseDwB(int remainingDynamicTime, int totalDynamicTime)
  {
    if (totalDynamicTime <= 0)
    {
      return AUTO_DYNAMIC_DW_MIN;
    }

    int elapsedDynamicTime = totalDynamicTime - remainingDynamicTime;
    if (elapsedDynamicTime < 0)
    {
      elapsedDynamicTime = 0;
    }
    if (elapsedDynamicTime < totalDynamicTime / 4)
    {
      return AUTO_DYNAMIC_DW_MAX_B;
    }
    if (elapsedDynamicTime < totalDynamicTime / 2)
    {
      return 4;
    }
    if (elapsedDynamicTime < (totalDynamicTime * 3) / 4)
    {
      return 3;
    }
    if (elapsedDynamicTime < (totalDynamicTime * 7) / 8)
    {
      return 2;
    }
    return AUTO_DYNAMIC_DW_MIN;
  }

  void recordTempDwSample(int *tempPoint, int *dwPoint, int &index, int temp, int dw)
  {
    if (index >= POT_TEMP_POINT_CAPACITY)
    {
      return;
    }

    tempPoint[index] = temp;
    if (dwPoint != NULL)
    {
      dwPoint[index] = dw;
    }
    index++;
  }

  void logTempPoints(const char *stage, const char *prefix, int *tempPoint, int *dwPoint, int index)
  {
    const int cycle_index = JiandanMaker::getLogCycleIndex();
    int top_temp_count = 0;
    int top_temps[10] = {0};
    String log_label = prefix;
    if (log_label.endsWith("="))
    {
      log_label.remove(log_label.length() - 1);
    }

    String points;
    for (int i = 0; i < index; i++)
    {
      if (i > 0)
      {
        points += ' ';
      }
      points += String(tempPoint[i]);

      int insert_pos = top_temp_count;
      if (insert_pos < 10)
      {
        top_temp_count++;
      }
      else if (tempPoint[i] <= top_temps[9])
      {
        insert_pos = -1;
      }
      else
      {
        insert_pos = 9;
      }

      if (insert_pos >= 0)
      {
        while (insert_pos > 0 && top_temps[insert_pos - 1] < tempPoint[i])
        {
          if (insert_pos < 10)
          {
            top_temps[insert_pos] = top_temps[insert_pos - 1];
          }
          insert_pos--;
        }
        if (insert_pos < 10)
        {
          top_temps[insert_pos] = tempPoint[i];
        }
      }
    }
    MAKER_LOG_INFO(stage, "========== cycle=%d %s ==========", cycle_index, log_label.c_str());
    MAKER_LOG_INFO(stage, "cycle=%d TEMP_POINTS[%d]: %s", cycle_index, index, points.c_str());

    if (dwPoint != NULL)
    {
      String temp_dw_points;
      int current_dw = dwPoint[0];
      temp_dw_points += "DW_GROUPS: ";
      temp_dw_points += String(current_dw);
      temp_dw_points += " [";

      for (int i = 0; i < index; i++)
      {
        if (i > 0 && dwPoint[i] != current_dw)
        {
          temp_dw_points += "] ";
          current_dw = dwPoint[i];
          temp_dw_points += String(current_dw);
          temp_dw_points += " [";
        }
        else if (i > 0)
        {
          temp_dw_points += ' ';
        }

        temp_dw_points += String(tempPoint[i]);
      }
      temp_dw_points += "]";

      MAKER_LOG_INFO(stage, "cycle=%d %s", cycle_index, temp_dw_points.c_str());
    }

    if (index <= 0)
    {
      MAKER_LOG_INFO(stage, "cycle=%d TEMP_STATS: count=0", cycle_index);
      return;
    }

    if (top_temp_count > 0)
    {
      float top_temp_sum = 0.0f;
      String top_temp_points = "TOP_HOT_POINTS[";
      top_temp_points += String(top_temp_count);
      top_temp_points += "]: ";
      top_temp_points += "[";
      for (int i = 0; i < top_temp_count; i++)
      {
        if (i > 0)
        {
          top_temp_points += ' ';
        }
        top_temp_points += String(top_temps[i]);
        top_temp_sum += (float)top_temps[i];
      }
      top_temp_points += "] avg=";
      top_temp_points += String(top_temp_sum / (float)top_temp_count, 2);
      MAKER_LOG_INFO(stage, "cycle=%d %s", cycle_index, top_temp_points.c_str());
    }

    int min_temp = tempPoint[0];
    int max_temp = tempPoint[0];
    int start_temp = tempPoint[0];
    int end_temp = tempPoint[index - 1];
    float sum_temp = 0.0f;
    float sum_square_diff = 0.0f;
    float sum_x = 0.0f;
    float sum_y = 0.0f;
    float sum_xy = 0.0f;
    float sum_x2 = 0.0f;

    for (int i = 0; i < index; i++)
    {
      const float x = (float)i;
      const float y = (float)tempPoint[i];

      if (tempPoint[i] < min_temp)
      {
        min_temp = tempPoint[i];
      }
      if (tempPoint[i] > max_temp)
      {
        max_temp = tempPoint[i];
      }

      sum_temp += y;
      sum_x += x;
      sum_y += y;
      sum_xy += x * y;
      sum_x2 += x * x;
    }

    const float average_temp = sum_temp / (float)index;
    for (int i = 0; i < index; i++)
    {
      const float diff = (float)tempPoint[i] - average_temp;
      sum_square_diff += diff * diff;
    }

    const int temp_range = max_temp - min_temp;
    const int temp_delta = end_temp - start_temp;
    const float standard_deviation = sqrtf(sum_square_diff / (float)index);
    const float coefficient_of_variation = fabsf(average_temp) > 0.001f
                                               ? (standard_deviation / fabsf(average_temp)) * 100.0f
                                               : 0.0f;
    const float slope_denominator = (float)index * sum_x2 - sum_x * sum_x;
    const float slope = fabsf(slope_denominator) > 0.001f
                            ? (((float)index * sum_xy) - (sum_x * sum_y)) / slope_denominator
                            : 0.0f;

    MAKER_LOG_INFO(stage,
                   "cycle=%d TEMP_STATS: count=%d avg=%.2f min=%d max=%d range=%d start=%d end=%d delta=%d slope=%.3fC/s std=%.3f cv=%.2f%%",
                   cycle_index,
                   index,
                   average_temp,
                   min_temp,
                   max_temp,
                   temp_range,
                   start_temp,
                   end_temp,
                   temp_delta,
                   slope,
                   standard_deviation,
                   coefficient_of_variation);
    MAKER_LOG_INFO(stage, "========== cycle=%d %s END ==========", cycle_index, log_label.c_str());
  }
}

// 初始化静态变量
SemaphoreHandle_t JiandanMaker::m_mutex = xSemaphoreCreateMutex();
OIL_CHECK_STATE JiandanMaker::oil_check = OIL_CHECK_IDLE;
EGG_CHECK_STATE JiandanMaker::egg_check = EGG_CHECK_IDLE;
ErrorCode JiandanMaker::error_code = ALL_OK;
int JiandanMaker::made_count = 0;
unsigned long JiandanMaker::START_TIME = 0;
unsigned long JiandanMaker::SINGLE_END_TIME = 0;
int JiandanMaker::SINGLE_SIDE_MAKE_TIME = 0;
unsigned long JiandanMaker::POTA_PRE_HEAT_TIME = 0;
unsigned long JiandanMaker::POTB_TURN_TIME = 0;
unsigned long JiandanMaker::POTA_TURN_TIME = 0;
bool JiandanMaker::preheat_done = false;
uint32_t JiandanMaker::potb_transfer_sequence = 0;
bool JiandanMaker::potb_is_empty = true;
bool JiandanMaker::pota_is_empty = true;
uint32_t JiandanMaker::active_operation_count = 0;
bool JiandanMaker::cancel_egg_home_required = false;
bool JiandanMaker::cancel_egg_home_in_progress = false;
bool JiandanMaker::fault_motion_home_required = false;
bool JiandanMaker::fault_motion_home_in_progress = false;
bool JiandanMaker::motion_home_required = false;
bool JiandanMaker::is_first_make = false;
int JiandanMaker::s_cycle_oil_level = 0;
int JiandanMaker::s_cycle_doneness = 0;

int JiandanMaker::getLogCycleIndex()
{
  return made_count + 1;
}

void JiandanMaker::getPotEmptyState(bool &potAEmpty, bool &potBEmpty)
{
  portENTER_CRITICAL(&pot_empty_state_mux);
  potAEmpty = pota_is_empty;
  potBEmpty = potb_is_empty;
  portEXIT_CRITICAL(&pot_empty_state_mux);
}

uint32_t JiandanMaker::getPotBTransferSequence()
{
  portENTER_CRITICAL(&pot_empty_state_mux);
  const uint32_t sequence = potb_transfer_sequence;
  portEXIT_CRITICAL(&pot_empty_state_mux);
  return sequence;
}

bool JiandanMaker::hasPotBTransferReached(uint32_t expectedSequence)
{
  const uint32_t currentSequence = getPotBTransferSequence();
  // 使用有符号差值比较，使uint32_t自然回绕后仍能正确判断先后；
  // 前提是未消费事件不超过2^31次，设备实际运行不可能接近该上限。
  return static_cast<int32_t>(currentSequence - expectedSequence) >= 0;
}

void JiandanMaker::markPotBOccupied()
{
  portENTER_CRITICAL(&pot_empty_state_mux);
  potb_is_empty = false;
  portEXIT_CRITICAL(&pot_empty_state_mux);
}

uint32_t JiandanMaker::markPotBTransferredToA()
{
  portENTER_CRITICAL(&pot_empty_state_mux);
  potb_is_empty = true;
  pota_is_empty = false;
  ++potb_transfer_sequence;
  const uint32_t completedSequence = potb_transfer_sequence;
  portEXIT_CRITICAL(&pot_empty_state_mux);
  return completedSequence;
}

void JiandanMaker::markPotAEmpty()
{
  portENTER_CRITICAL(&pot_empty_state_mux);
  pota_is_empty = true;
  portEXIT_CRITICAL(&pot_empty_state_mux);
}

void JiandanMaker::resetPotEmptyState()
{
  portENTER_CRITICAL(&pot_empty_state_mux);
  pota_is_empty = true;
  potb_is_empty = true;
  portEXIT_CRITICAL(&pot_empty_state_mux);
}

bool JiandanMaker::isEmergencyStopRequested()
{
  return EmergencyStopControl::getInstance()->isRequested();
}

uint32_t JiandanMaker::getActiveOperationCount()
{
  portENTER_CRITICAL(&maker_lifecycle_mux);
  const uint32_t count = active_operation_count;
  portEXIT_CRITICAL(&maker_lifecycle_mux);
  return count;
}

bool JiandanMaker::beginActiveOperation()
{
  portENTER_CRITICAL(&maker_lifecycle_mux);
  if (EmergencyStopControl::getInstance()->isRequested())
  {
    portEXIT_CRITICAL(&maker_lifecycle_mux);
    return false;
  }
  ++active_operation_count;
  portEXIT_CRITICAL(&maker_lifecycle_mux);
  return true;
}

void JiandanMaker::updateStoppedStateIfQuiescent()
{
  if (isEmergencyStopRequested() && getActiveOperationCount() == 0)
  {
    // 用户取消时，给蛋机构回Home属于停机收尾；完成前不能对UI发布“已停止”。
    if (isStopCleanupPending())
    {
      return;
    }

    JiandanDataManager *dataMg = JiandanDataManager::getInstance();
    if (dataMg->getMakerState() == STOPPED_NEEDS_RECOVERY)
    {
      return;
    }
    stopAllHeating();
    dataMg->setRemainingTime(0);
    dataMg->setTotalMakeTime(0);
    resetHeatRemainingTimes();
    dataMg->setMakerState(STOPPED_NEEDS_RECOVERY);
    MAKER_LOG_WARN("STOP", "all_operations_stopped recovery_required=true");
  }
}

bool JiandanMaker::isStopCleanupPending()
{
  portENTER_CRITICAL(&maker_lifecycle_mux);
  const bool pending = cancel_egg_home_required || cancel_egg_home_in_progress ||
                       fault_motion_home_required || fault_motion_home_in_progress;
  portEXIT_CRITICAL(&maker_lifecycle_mux);
  return pending;
}

void JiandanMaker::performStopCleanupIfReady()
{
  bool shouldHomeEgg = false;
  bool shouldHomeMotion = false;

  portENTER_CRITICAL(&maker_lifecycle_mux);
  if (EmergencyStopControl::getInstance()->isRequested() && active_operation_count == 0)
  {
    if (cancel_egg_home_required && !cancel_egg_home_in_progress)
    {
      cancel_egg_home_in_progress = true;
      shouldHomeEgg = true;
    }
    if (fault_motion_home_required && !fault_motion_home_in_progress)
    {
      fault_motion_home_in_progress = true;
      shouldHomeMotion = true;
    }
  }
  portEXIT_CRITICAL(&maker_lifecycle_mux);

  if (!shouldHomeEgg && !shouldHomeMotion)
  {
    return;
  }

  JiandanDataManager::getInstance()->setMakerState(STOPPING);
  if (shouldHomeEgg)
  {
    MAKER_LOG_INFO("STOP", "cancel_cleanup egg_home_start");
    EggControl::getInstance()->homeAfterCancel();
    MAKER_LOG_INFO("STOP", "cancel_cleanup egg_home_complete");
  }

  bool motionHomeSucceeded = false;
  if (shouldHomeMotion)
  {
    MAKER_LOG_INFO("STOP", "egg_drop_cleanup motion_home_start");
    const bool transHomed = TransMotor::getInstance()->homeAfterEmergencyStop();
    const bool pushHomed = PushMotor::getInstance()->homeAfterEmergencyStop();
    motionHomeSucceeded = transHomed && pushHomed;
    MAKER_LOG_INFO("STOP", "egg_drop_cleanup motion_home_complete trans=%d push=%d success=%d",
                   transHomed ? 1 : 0, pushHomed ? 1 : 0,
                   motionHomeSucceeded ? 1 : 0);
  }

  portENTER_CRITICAL(&maker_lifecycle_mux);
  if (shouldHomeEgg)
  {
    cancel_egg_home_required = false;
    cancel_egg_home_in_progress = false;
  }
  if (shouldHomeMotion)
  {
    fault_motion_home_required = false;
    fault_motion_home_in_progress = false;
    if (motionHomeSucceeded)
    {
      motion_home_required = false;
    }
  }
  portEXIT_CRITICAL(&maker_lifecycle_mux);
}

void JiandanMaker::endActiveOperation()
{
  portENTER_CRITICAL(&maker_lifecycle_mux);
  if (active_operation_count > 0)
  {
    --active_operation_count;
  }
  portEXIT_CRITICAL(&maker_lifecycle_mux);
  updateStoppedStateIfQuiescent();
}

bool JiandanMaker::createManagedTask(TaskFunction_t task,
                                     const char *name,
                                     uint32_t stackDepth,
                                     void *params)
{
  if (!beginActiveOperation())
  {
    return false;
  }
  const BaseType_t created = xTaskCreatePinnedToCore(task,
                                                     name,
                                                     stackDepth,
                                                     params,
                                                     2,
                                                     NULL,
                                                     1);
  if (created != pdPASS)
  {
    MAKER_LOG_ERROR("TASK", "create_failed name=%s", name);
    endActiveOperation();
    requestEmergencyStop();
    return false;
  }
  return true;
}

void JiandanMaker::finishManagedTask()
{
  endActiveOperation();
  vTaskDelete(NULL);
}

void JiandanMaker::requestEmergencyStop(bool returnEggHome)
{
  requestSafetyStop(returnEggHome, false,
                    returnEggHome ? "user_cancel" : "emergency");
}

void JiandanMaker::requestSafetyStop(bool returnEggHome,
                                     bool returnMotionHome,
                                     const char *reason)
{
  // 与新制作周期的登记共用临界区：先登记取消收尾，再发布停止令牌。
  portENTER_CRITICAL(&maker_lifecycle_mux);
  if (returnEggHome)
  {
    cancel_egg_home_required = true;
  }
  if (returnMotionHome)
  {
    fault_motion_home_required = true;
  }
  motion_home_required = true;
  EmergencyStopControl::getInstance()->request();
  portEXIT_CRITICAL(&maker_lifecycle_mux);

  JiandanDataManager *dataMg = JiandanDataManager::getInstance();
  dataMg->setStart(false);
  dataMg->setMakerState(STOPPING);

  // 危险输出不等待机械互斥锁，确保UART收到命令后立即执行。
  stopAllHeating();
  OilControl::getInstance()->stopImmediately();
  EggControl::getInstance()->stopImmediately();
  OilServo::getInstance()->stopImmediately();
  PushMotor::getInstance()->stop();
  PushMotor::getInstance()->relax();
  TransMotor::getInstance()->stop();
  TransMotor::getInstance()->relax();
  PotAMotor::getInstance()->potAStop();
  PotAMotor::getInstance()->relax();
  PotBMotor::getInstance()->stop();
  PotBMotor::getInstance()->relax();

  Fan::getInstance()->autoTurnOn(FAN_TOGGLE_DUTY);
  MAKER_LOG_WARN("STOP", "safety_stop_requested reason=%s active_operations=%lu",
                 reason,
                 static_cast<unsigned long>(getActiveOperationCount()));
  updateStoppedStateIfQuiescent();
}

bool JiandanMaker::confirmEmergencyRecovery()
{
  if (!isEmergencyStopRequested())
  {
    return true;
  }

  if (getActiveOperationCount() != 0 || isStopCleanupPending())
  {
    MAKER_LOG_WARN("STOP", "recovery_rejected active_operations=%lu cleanup_pending=%d",
                   static_cast<unsigned long>(getActiveOperationCount()),
                   isStopCleanupPending() ? 1 : 0);
    return false;
  }

  // 用户确认已经清锅并把机构恢复到安全初始位置后，才允许重新开始。
  resetPotEmptyState();
  EmergencyStopControl::getInstance()->clear();

  JiandanDataManager *dataMg = JiandanDataManager::getInstance();
  dataMg->setStart(false);
  dataMg->setErrorCode(ALL_OK);
  dataMg->setMakerState(IDLE);
  logPotEmptyState("emergency_recovery_confirmed");
  MAKER_LOG_INFO("STOP", "recovery_complete maker_state=IDLE");
  return true;
}

void JiandanMaker::logPotEmptyState(const char *event)
{
  bool potAEmpty = false;
  bool potBEmpty = false;
  getPotEmptyState(potAEmpty, potBEmpty);
  MAKER_LOG_INFO("POT_STATE", "%s pot_a_empty=%d pot_b_empty=%d",
                 event,
                 potAEmpty ? 1 : 0,
                 potBEmpty ? 1 : 0);
}

void JiandanMaker::clearError()
{
  error_code = ALL_OK;
}

void JiandanMaker::makerControlTask(void *pp)
{
  JiandanDataManager *dataMg = JiandanDataManager::getInstance();
  for (;;)
  {
    if (isEmergencyStopRequested())
    {
      performStopCleanupIfReady();
      updateStoppedStateIfQuiescent();
      vTaskDelay(100);
      continue;
    }

    clearError();
    if (dataMg->getNeedMakeEggs() >= 1 && dataMg->getStart())
    {
      // “检查启动条件”和“登记活动周期”之间可能收到停机命令；登记失败就不能进入流程。
      if (!beginActiveOperation())
      {
        updateStoppedStateIfQuiescent();
        continue;
      }
      MAKER_LOG_INFO("CTRL", "cycle_start remaining_eggs=%.1f", dataMg->getNeedMakeEggs());
      Fan::getInstance()->autoTurnOn(FAN_TOGGLE_DUTY);

      is_first_make = checkFirstMake();
      portENTER_CRITICAL(&maker_lifecycle_mux);
      const bool recovery_home_required = motion_home_required;
      portEXIT_CRITICAL(&maker_lifecycle_mux);
      // 配方快照：写入静态成员，potTurnTask 与 firstGiveOil 共用同一份值，
      // 保证本颗蛋的双锅油量时长一致。即使 UI 在煎蛋中改了油量，
      // 本颗蛋仍按起点快照执行，下一颗蛋才采用新值。
      s_cycle_oil_level = dataMg->getOilLevel();
      s_cycle_doneness = dataMg->getDoneness();
      const int cycle_oil_level = s_cycle_oil_level;

      MAKER_LOG_INFO("CTRL", "recipe_snapshot first_make=%s oil=%d doneness=%d",
                     is_first_make ? "true" : "false",
                     cycle_oil_level,
                     s_cycle_doneness);

      // 激光检测开关关闭时：不依赖打蛋位 VL53 传感器，跳过其自检。
      if (JiandanDataManager::getInstance()->getLaserCheckEnabled() &&
          !DebugRunControl::getInstance()->shouldAssumeSuccess(DebugCheckType::TOF_SELF_TEST))
      {
        dataMg->setMakerState(CHECKING_CYD_TOF);
        if (!checkCydTofSensor())
        {
          dataMg->setNeedMakeEggs(0);
          endActiveOperation();
          continue;
        }
        MAKER_LOG_INFO("SENSOR", "cyd_tof_check ok");
      }
      else
      {
        MAKER_LOG_WARN("SENSOR", "detection bypassed, skip cyd_tof self-check");
      }

      dataMg->setMakerState(HOMING);

      if (isEmergencyStopRequested())
      {
        endActiveOperation();
        continue;
      }

      const bool should_home = is_first_make || recovery_home_required;
      MAKER_LOG_INFO("CTRL", "home_decision first_make=%s recovery_required=%s execute=%s",
                     is_first_make ? "true" : "false",
                     recovery_home_required ? "true" : "false",
                     should_home ? "true" : "false");
      if (should_home)
      {
        home();
        if (isEmergencyStopRequested())
        {
          endActiveOperation();
          continue;
        }
        portENTER_CRITICAL(&maker_lifecycle_mux);
        if (!EmergencyStopControl::getInstance()->isRequested())
        {
          motion_home_required = false;
        }
        portEXIT_CRITICAL(&maker_lifecycle_mux);
        if (is_first_make)
        {
          made_count = 0;
        }
        dataMg->setMakerState(CHECKING_POT);
        if (!checkPot())
        {
          dataMg->setNeedMakeEggs(0);
          endActiveOperation();
          continue;
        }
        MAKER_LOG_INFO("SENSOR", "pot_check ok");
      }

      if (isEmergencyStopRequested())
      {
        endActiveOperation();
        continue;
      }

      dataMg->setNeedMakeFloor(dataMg->getNeedMakeFloor() + 1.0f);

      // 预热线程开启，不阻塞给蛋给油流程
      dataMg->setMakerState(PRE_HEATING);
      preheat_done = false;
      if (!createManagedTask(preheatTask, "preheatTask", 1024 * 4, NULL))
      {
        endActiveOperation();
        continue;
      }

      // 给蛋
      dataMg->setMakerState(GIVING_EGG);
      if (!giveEgg())
      {
        if (!isEmergencyStopRequested())
        {
          dataMg->setNeedMakeEggs(0);
        }
        endActiveOperation();
        continue;
      }

      if (isEmergencyStopRequested())
      {
        endActiveOperation();
        continue;
      }

      MAKER_LOG_INFO("EGG", "load_complete");

      // 第一锅给油
      dataMg->setMakerState(GIVING_OIL);
      if (!firstGiveOil(cycle_oil_level))
      {
        if (!isEmergencyStopRequested())
        {
          dataMg->setNeedMakeEggs(0);
        }
        endActiveOperation();
        continue;
      }
      MAKER_LOG_INFO("OIL", "load_complete");

      // 等待预热完成
      while (!preheat_done && !isEmergencyStopRequested())
      {
        vTaskDelay(100);
      }
      if (isEmergencyStopRequested() ||
          !dataMg->getStart() || dataMg->getErrorCode() != ALL_OK)
      {
        stopAllHeating();
        MAKER_LOG_WARN("PREHEAT", "aborted start=%s err=%d",
                       dataMg->getStart() ? "true" : "false",
                       dataMg->getErrorCode());
        endActiveOperation();
        continue;
      }
      MAKER_LOG_INFO("CTRL", "cook_start cycle_index=%d", made_count);

      // 记录开始时间和结束时间,毫秒时间戳类型
      START_TIME = millis();
      SINGLE_SIDE_MAKE_TIME = calcHeatTotalTime();       // 单面煎制时间，单位秒
      dataMg->setTotalMakeTime(SINGLE_SIDE_MAKE_TIME); // UI 数码管周围显示的是每一面的总时长

      POTB_TURN_TIME = START_TIME + SINGLE_SIDE_MAKE_TIME * 1000;
      POTA_TURN_TIME = START_TIME + SINGLE_SIDE_MAKE_TIME * 2 * 1000 - 10 * 1000;
      POTA_PRE_HEAT_TIME = (POTB_TURN_TIME > POTA_HEAT_LEAD_MS)
                               ? (POTB_TURN_TIME - POTA_HEAT_LEAD_MS)
                               : START_TIME;

      SINGLE_END_TIME = POTB_TURN_TIME;
      // 每轮领取自己的目标序号。序号只递增、不重置，下一轮不会覆盖上一轮的完成事件。
      const uint32_t expected_potb_transfer_sequence = getPotBTransferSequence() + 1U;
      resetHeatRemainingTimes();

      const bool tasks_created =
          createManagedTask(beatTask, "beatTask", 1024 * 4, NULL) &&
          createManagedTask(potTurnTask, "PotTurnTask", 1024 * 4, NULL) &&
          createManagedTask(heatPotBTask,
                  "HeatBTask",
                  1024 * 8,
                  reinterpret_cast<void *>(static_cast<uintptr_t>(expected_potb_transfer_sequence))) &&
          createManagedTask(heatPotATask,
                            "HeatATask",
                            1024 * 8,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(expected_potb_transfer_sequence)));
      if (!tasks_created)
      {
        endActiveOperation();
        continue;
      }

      MAKER_LOG_INFO("CTRL", "first_side_wait start_time=%lu single_side_time=%d expected_transfer_seq=%lu",
                     START_TIME,
                     SINGLE_SIDE_MAKE_TIME,
                     static_cast<unsigned long>(expected_potb_transfer_sequence));
      dataMg->setMakerState(MAKING);

      while (SINGLE_END_TIME > millis() ||
             !hasPotBTransferReached(expected_potb_transfer_sequence))
      {
        if (isEmergencyStopRequested())
        {
          break;
        }
        dataMg->setRemainingTime(getRemainingSeconds(SINGLE_END_TIME));
        vTaskDelay(500);
      }
      if (isEmergencyStopRequested())
      {
        endActiveOperation();
        continue;
      }
      dataMg->setNeedMakeFloor(dataMg->getNeedMakeFloor() - 0.5f);
      JiandanDataManager::getInstance()->setNeedMakeEggs(JiandanDataManager::getInstance()->getNeedMakeEggs() - 0.5);

      MAKER_LOG_INFO("CTRL", "first_side_complete transfer_seq=%lu remaining_eggs=%.1f",
                     static_cast<unsigned long>(expected_potb_transfer_sequence),
                     JiandanDataManager::getInstance()->getNeedMakeEggs());
      endActiveOperation();
    }
    else
    {
      if (!isEmergencyStopRequested() && dataMg->getNeedMakeFloor() <= 0.0f)
      {
        dataMg->setRemainingTime(0);
        dataMg->setTotalMakeTime(0);
        resetHeatRemainingTimes();
        dataMg->setMakerState(IDLE);
        if (shouldKeepFanRunningForCooldown())
        {
          Fan::getInstance()->autoTurnOn(FAN_TOGGLE_DUTY);
        }
        else
        {
          Fan::getInstance()->autoTurnOff();
        }
      }
      // Serial.printf("MakerControl idle...\n");
    }

    vTaskDelay(500);
  }
}

void JiandanMaker::home()
{
  TransMotor::getInstance()->home();
  PushMotor::getInstance()->home();
}

bool JiandanMaker::checkCydTofSensor()
{
  const CydTofCheckState state = CydTof::getInstance()->checkSensor();
  if (state != CydTofCheckState::OK)
  {
    const ErrorCode error = getCydTofErrorCode(state);
    MAKER_LOG_ERROR("SENSOR", "cyd_tof_check failed state=%d error=%d", (int)state, (int)error);
    JiandanDataManager::getInstance()->setErrorCode(error);
    return false;
  }

  return true;
}

bool JiandanMaker::giveEgg()
{
  DebugRunControl *debug = DebugRunControl::getInstance();
  const bool laserDisabled = !JiandanDataManager::getInstance()->getLaserCheckEnabled();
  const bool bypassBefore =
      laserDisabled || debug->shouldAssumeSuccess(DebugCheckType::EGG_BEFORE_LOAD);
  const bool bypassAfter =
      laserDisabled || debug->shouldAssumeSuccess(DebugCheckType::EGG_AFTER_LOAD);

  // 检查蛋托是否清空
  if (!bypassBefore)
  {
    const bool tray_has_egg = trayHasEgg();
    if (JiandanDataManager::getInstance()->getErrorCode() != ALL_OK)
    {
      return false;
    }

    if (tray_has_egg)
    {
      MAKER_LOG_WARN("EGG", "tray_not_empty skip_load");
      egg_check = EGG_CHECK_HAS_EGG;
      return true;
    }
  }
  else
  {
    MAKER_LOG_WARN("EGG", "before-load detection bypassed");
  }

  EggControl::getInstance()->give();
  if (isEmergencyStopRequested())
  {
    return false;
  }
  if (bypassAfter)
  {
    MAKER_LOG_WARN("EGG", "after-load detection bypassed");
    egg_check = EGG_CHECK_HAS_EGG;
    return true;
  }

  const bool egg_loaded = trayHasEgg();
  if (JiandanDataManager::getInstance()->getErrorCode() != ALL_OK)
  {
    return false;
  }

  if (!egg_loaded)
  {
    MAKER_LOG_ERROR("EGG", "tray_no_egg_after_load");
    egg_check = EGG_CHECK_NO_EGG;
    JiandanDataManager::getInstance()->setErrorCode(EGG_EMPTY);
    return false;
  }

  egg_check = EGG_CHECK_HAS_EGG;
  return true;
}

bool JiandanMaker::firstGiveOil(int oilLevel)
{
  bool r = false;
  if (isEmergencyStopRequested())
  {
    return false;
  }

  const bool bypassFeedback =
      DebugRunControl::getInstance()->shouldAssumeSuccess(DebugCheckType::OIL_FEEDBACK);
  bool success = OilControl::getInstance()->giveOil(calcOilTime(oilLevel), !bypassFeedback);
  if (!success)
  {
    if (isEmergencyStopRequested())
    {
      return false;
    }
    MAKER_LOG_ERROR("OIL", "dispense failed");
    oil_check = OIL_CHECK_NO_OIL;
    JiandanDataManager::getInstance()->setErrorCode(OIL_EMPTY);
    r = false;
  }
  else
  {
    oil_check = OIL_CHECK_HAS_OIL;
    r = true;
  }

  vTaskDelay(1000); // 等待油滴下落
  // firstGiveOil: 舵机刚去过 PotB(45°)，回零路径短，用 FromB 默认参数
  OilServo::getInstance()->goZeroFromB();

  return r;
}

bool JiandanMaker::secondGiveOil(int oilLevel)
{
  JiandanDataManager *dataMg = JiandanDataManager::getInstance();
  EmergencyStopControl *stopControl = EmergencyStopControl::getInstance();
  if (stopControl->isRequested() || !dataMg->getStart() || dataMg->getErrorCode() != ALL_OK)
  {
    MAKER_LOG_WARN("OIL", "second_dispense_skipped start=%d error=%d estop=%d",
                   dataMg->getStart() ? 1 : 0,
                   static_cast<int>(dataMg->getErrorCode()),
                   stopControl->isRequested() ? 1 : 0);
    return false;
  }

  const bool bypassFeedback =
      DebugRunControl::getInstance()->shouldAssumeSuccess(DebugCheckType::OIL_FEEDBACK);
  const int oilTimeMs = calcOilTime(oilLevel);
  MAKER_LOG_INFO("OIL", "second_dispense_start level=%d time_ms=%d target_angle=%d feedback=%d",
                 oilLevel, oilTimeMs, OIL_SERVO_POTA_ANGLE, bypassFeedback ? 0 : 1);

  OilServo::getInstance()->goPotA();
  if (stopControl->isRequested() || !dataMg->getStart() || dataMg->getErrorCode() != ALL_OK)
  {
    MAKER_LOG_WARN("OIL", "second_dispense_aborted stage=servo_move estop=%d error=%d",
                   stopControl->isRequested() ? 1 : 0,
                   static_cast<int>(dataMg->getErrorCode()));
    return false;
  }

  if (!OilControl::getInstance()->giveOil(oilTimeMs, !bypassFeedback))
  {
    if (stopControl->isRequested())
    {
      MAKER_LOG_WARN("OIL", "second_dispense_aborted stage=pump estop=1");
      return false;
    }

    oil_check = OIL_CHECK_NO_OIL;
    // 泵反馈失败但尚未急停时，先将油管从锅A安全移回零位，再上报错误。
    OilServo::getInstance()->goZeroFromA();
    if (stopControl->isRequested())
    {
      MAKER_LOG_WARN("OIL", "second_dispense_aborted stage=servo_return_after_failure estop=1");
      return false;
    }
    MAKER_LOG_ERROR("OIL", "second_dispense_failed level=%d time_ms=%d", oilLevel, oilTimeMs);
    dataMg->setErrorCode(OIL_EMPTY);
    return false;
  }

  oil_check = OIL_CHECK_HAS_OIL;
  if (!stopControl->delayMs(1000))
  {
    MAKER_LOG_WARN("OIL", "second_dispense_aborted stage=drip_wait estop=1");
    return false;
  }

  OilServo::getInstance()->goZeroFromA();
  if (stopControl->isRequested())
  {
    MAKER_LOG_WARN("OIL", "second_dispense_aborted stage=servo_return estop=1");
    return false;
  }

  MAKER_LOG_INFO("OIL", "second_dispense_complete level=%d time_ms=%d", oilLevel, oilTimeMs);
  return true;
}

bool JiandanMaker::checkPot()
{
  if (DebugRunControl::getInstance()->shouldAssumeSuccess(DebugCheckType::POT_PREFLIGHT))
  {
    MAKER_LOG_WARN("SENSOR", "process bypass enabled, skip pot preflight");
    return true;
  }

  PotI2c *potI2c = PotI2c::getInstance();
  if (!potI2c->checkPot())
  {
    ErrorCode error = ALL_OK;
    if (potI2c->getPOTA_S_CODE() != POT_OK)
    {
      error = getPotErrorCode(true, potI2c->getPOTA_S_CODE());
    }
    else if (potI2c->getPOTB_S_CODE() != POT_OK)
    {
      error = getPotErrorCode(false, potI2c->getPOTB_S_CODE());
    }
    if (error == ALL_OK)
    {
      error = POTA_IIC_ERROR;
    }
    MAKER_LOG_ERROR("SENSOR", "pot_check failed error=%d", (int)error);
    JiandanDataManager::getInstance()->setErrorCode(error);
    return false;
  }
  return true;
}

bool JiandanMaker::trayHasEgg()
{
  int distance = CydTof::getInstance()->getHaveDistance();

  // 50~100mm：有蛋或蛋壳
  if (distance >= HAVE_EGG_DISTANCE_MIN && distance <= HAVE_EGG_DISTANCE_MAX)
  {
    MAKER_LOG_INFO("EGG", "tray_detected distance=%d", distance);
    return true;
  }

  // 大于100mm：无蛋（正常空托，不报错）
  if (distance > HAVE_EGG_DISTANCE_MAX)
  {
    MAKER_LOG_INFO("EGG", "tray_empty distance=%d", distance);
    return false;
  }

  // 10 ~ <50mm：玻璃片被污垢遮挡（紧邻有蛋窗口下沿，无中间地带）
  if (distance >= BLOCK_DISTANCE_MIN && distance < HAVE_EGG_DISTANCE_MIN)
  {
    MAKER_LOG_ERROR("SENSOR", "cyd_tof_lens_blocked distance=%d", distance);
    error_code = CYD_TOF_HAVE_BLOCK;
    JiandanDataManager::getInstance()->setErrorCode(error_code);
    return false;
  }

  // 其余（读失败 -1、1~9 等）：传感器自身异常
  MAKER_LOG_ERROR("SENSOR", "cyd_tof_abnormal distance=%d", distance);
  error_code = CYD_TOF_HAVE_ERROR;
  JiandanDataManager::getInstance()->setErrorCode(error_code);
  return false;
}

void JiandanMaker::beatTask(void *params)
{
  MAKER_LOG_INFO("BEAT", "task_start");

  if (isEmergencyStopRequested())
  {
    finishManagedTask();
    return;
  }

  int wait_log_count = 0;
  while (1)
  {
    if (isEmergencyStopRequested())
    {
      finishManagedTask();
      return;
    }
    // 获取互斥锁（阻塞等待直到获取成功）
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) == pdTRUE)
    {
      if (isEmergencyStopRequested())
      {
        xSemaphoreGive(m_mutex);
        finishManagedTask();
        return;
      }
      bool potAEmpty = false;
      bool potBEmpty = false;
      getPotEmptyState(potAEmpty, potBEmpty);
      if (!potBEmpty)
      {
        xSemaphoreGive(m_mutex);
        if ((wait_log_count++ % 10) == 0)
        {
          MAKER_LOG_WARN("BEAT", "wait_potb_empty pot_a_empty=%d", potAEmpty ? 1 : 0);
        }
        vTaskDelay(200);
        continue;
      }

      // 送蛋到B锅上面
      TransMotor::getInstance()->goPotB();
      if (isEmergencyStopRequested())
      {
        xSemaphoreGive(m_mutex);
        finishManagedTask();
        return;
      }

      // 打蛋
      PushMotor::getInstance()->beatEggNoClose();
      if (isEmergencyStopRequested())
      {
        xSemaphoreGive(m_mutex);
        finishManagedTask();
        return;
      }
      // beatEggNoClose返回后，逻辑上认为蛋已经进入B锅。
      markPotBOccupied();
      logPotEmptyState("egg_entered_pot_b");
      if (!EmergencyStopControl::getInstance()->delayMs(500))
      {
        xSemaphoreGive(m_mutex);
        finishManagedTask();
        return;
      }

      // 先从锅位退回home，确保释放锅区后再让出锁。
      TransMotor::getInstance()->backHomeFromPot();
      xSemaphoreGive(m_mutex); // 释放锁
      break;
    }
    else
    {
      // 限频：首次与每 10 次(约 2s)打一次
      if ((wait_log_count++ % 10) == 0)
      {
        MAKER_LOG_WARN("BEAT", "wait_lock count=%d", wait_log_count);
      }
      vTaskDelay(200);
    }
  }

  if (!EmergencyStopControl::getInstance()->delayMs(500))
  {
    finishManagedTask();
    return;
  }
  TransMotor::getInstance()->goDumpFromHome();
  if (isEmergencyStopRequested())
  {
    finishManagedTask();
    return;
  }

  // 抛壳位先高频抖动，把黏在打蛋器上的蛋壳甩落，再回抽释放。
  PushMotor::getInstance()->pushShakeshell(1);
  if (isEmergencyStopRequested())
  {
    finishManagedTask();
    return;
  }

  PushMotor::getInstance()->close();
  if (!EmergencyStopControl::getInstance()->delayMs(500))
  {
    finishManagedTask();
    return;
  }
  TransMotor::getInstance()->homeFromDump();
  if (isEmergencyStopRequested())
  {
    finishManagedTask();
    return;
  }

  // 激光检测开关关闭时：不依赖打蛋位 VL53，跳过蛋壳残留检测。
  if (!JiandanDataManager::getInstance()->getLaserCheckEnabled() ||
      DebugRunControl::getInstance()->shouldAssumeSuccess(DebugCheckType::SHELL_RESIDUE))
  {
    MAKER_LOG_WARN("BEAT", "detection bypassed, skip shell residue check");
    finishManagedTask();
    return;
  }

  // 检测蛋壳是否抛干净
  const bool shell_remains = trayHasEgg();
  if (JiandanDataManager::getInstance()->getErrorCode() != ALL_OK)
  {
    finishManagedTask();
    return;
  }

  if (shell_remains)
  {
    MAKER_LOG_WARN("BEAT", "shell_not_clean");

    // 重试抛壳
    TransMotor::getInstance()->dumpRetry();
    // 最后一次检测
    const bool shell_still_remains = trayHasEgg();
    if (JiandanDataManager::getInstance()->getErrorCode() != ALL_OK)
    {
      finishManagedTask();
      return;
    }

    if (shell_still_remains)
    {
      MAKER_LOG_ERROR("BEAT", "shell_retry_failed");
      error_code = BEAT_SHELL_NOT_CLEAN;
      JiandanDataManager::getInstance()->setErrorCode(error_code);
    }
    else
    {
      MAKER_LOG_INFO("BEAT", "shell_cleaned");
    }
  }

  finishManagedTask();
}

void JiandanMaker::potTurnTask(void *params)
{
  if (isEmergencyStopRequested())
  {
    finishManagedTask();
    return;
  }

  // 任务启动时立即捕获时间快照：makerControlTask 在 END_TIME 到达后会立即为下一颗蛋
  // 覆盖 POTA_START_HEAT_TIME / POTB_TURN_TIME，若循环中持续读取静态变量会被影响
  const unsigned long local_potb_turn = POTB_TURN_TIME;
  const unsigned long local_pota_turn = POTA_TURN_TIME;
  const bool local_second_oil_enabled =
      JiandanDataManager::getInstance()->getSecondOilEnabled();
  // 该任务可能要等上一颗蛋从A锅取出；在任务启动时固定本颗蛋的油量，避免等待中
  // makerControlTask 为下一轮更新 s_cycle_oil_level 后配方串轮。
  const int local_cycle_oil_level = s_cycle_oil_level;

  // 等待A锅为空后再给A锅加油。流水线运行时，上一颗蛋可能仍在A锅煎第二面。
  int pota_oil_wait_count = 0;
  int oil_lock_wait_count = 0;

  // 舵机+油泵属于机械动作，需要与 beatTask/potTurnTask 的 TransMotor/PotXMotor
  // 共用同一把 m_mutex，避免并发抢占导致机械臂冲突。
  while (1)
  {
    if (isEmergencyStopRequested())
    {
      finishManagedTask();
      return;
    }
    bool potAEmpty = false;
    bool potBEmpty = false;
    getPotEmptyState(potAEmpty, potBEmpty);
    if (!potAEmpty)
    {
      if ((pota_oil_wait_count++ % 50) == 0)
      {
        MAKER_LOG_WARN("TURN", "wait_pota_empty_before_oil pot_b_empty=%d",
                       potBEmpty ? 1 : 0);
      }
      vTaskDelay(200);
      continue;
    }

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) == pdTRUE)
    {
      if (isEmergencyStopRequested())
      {
        xSemaphoreGive(m_mutex);
        finishManagedTask();
        return;
      }
      // 获取机械锁后再次确认，关闭“检查空锅”和“执行加油”之间的竞态窗口。
      getPotEmptyState(potAEmpty, potBEmpty);
      if (!potAEmpty)
      {
        xSemaphoreGive(m_mutex);
        if ((pota_oil_wait_count++ % 50) == 0)
        {
          MAKER_LOG_WARN("TURN", "pota_became_occupied_before_oil pot_b_empty=%d",
                         potBEmpty ? 1 : 0);
        }
        vTaskDelay(200);
        continue;
      }

      if (isEmergencyStopRequested())
      {
        xSemaphoreGive(m_mutex);
        finishManagedTask();
        return;
      }

      // 在完整机械锁保护期间移动油管并给A锅加油，避免与打蛋/转运动作抢占机构。
      // 使用本颗蛋开始时的油量快照，保证两面用油量一致。
      if (local_second_oil_enabled && !secondGiveOil(local_cycle_oil_level))
      {
        // 锁仍由当前任务持有，先发布停止令牌并停止危险输出，避免其他任务在解锁
        // 与停机之间抢到机械机构；requestEmergencyStop 不会等待这把机械锁。
        if (!isEmergencyStopRequested())
        {
          requestEmergencyStop();
        }
        xSemaphoreGive(m_mutex);
        finishManagedTask();
        return;
      }
      xSemaphoreGive(m_mutex);
      if (local_second_oil_enabled)
      {
        MAKER_LOG_INFO("TURN", "pota_oil_complete pot_a_empty=1 oil_level=%d",
                       local_cycle_oil_level);
      }
      else
      {
        MAKER_LOG_INFO("TURN", "pota_oil_skipped pot_a_empty=1 reason=setting_disabled");
      }
      break;
    }
    else
    {
      if ((oil_lock_wait_count++ % 10) == 0)
      {
        MAKER_LOG_WARN("TURN", "oil_wait_lock count=%d", oil_lock_wait_count);
      }
      vTaskDelay(200);
    }
  }

  // B锅等待翻锅，翻锅放在循环中，等待锁
  while (millis() < local_potb_turn)
  {
    if (isEmergencyStopRequested())
    {
      finishManagedTask();
      return;
    }
    vTaskDelay(200);
  }
  while (1)
  {
    if (isEmergencyStopRequested())
    {
      finishManagedTask();
      return;
    }
    // 等待A锅是空锅状态，B锅翻出到A锅煎第二面
    bool potAEmpty = false;
    bool potBEmpty = false;
    getPotEmptyState(potAEmpty, potBEmpty);
    if (!potAEmpty)
    {
      static int pota_release_wait_count = 0;
      if ((pota_release_wait_count++ % 10) == 0)
      {
        MAKER_LOG_WARN("TURN", "wait_pota_release count=%d", pota_release_wait_count);
      }
      vTaskDelay(200);
      continue;
    }

    // 获取互斥锁（阻塞等待直到获取成功）
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) == pdTRUE)
    {
      if (isEmergencyStopRequested())
      {
        xSemaphoreGive(m_mutex);
        finishManagedTask();
        return;
      }
      // 获取机械锁后再次检查，关闭“检查状态”和“执行翻锅”之间的竞态窗口。
      getPotEmptyState(potAEmpty, potBEmpty);
      if (!potAEmpty || potBEmpty)
      {
        xSemaphoreGive(m_mutex);
        static int pot_state_wait_count = 0;
        if ((pot_state_wait_count++ % 10) == 0)
        {
          MAKER_LOG_WARN("TURN", "wait_transfer_ready pot_a_empty=%d pot_b_empty=%d",
                         potAEmpty ? 1 : 0,
                         potBEmpty ? 1 : 0);
        }
        vTaskDelay(200);
        continue;
      }

      PotBMotor::getInstance()->turn();
      if (isEmergencyStopRequested())
      {
        xSemaphoreGive(m_mutex);
        finishManagedTask();
        return;
      }
      // B->A锅状态和转移序号必须一次性发布，不能让其他任务观察到半更新状态。
      const uint32_t completedSequence = markPotBTransferredToA();
      MAKER_LOG_INFO("TURN", "potb_transfer_complete sequence=%lu",
                     static_cast<unsigned long>(completedSequence));
      logPotEmptyState("egg_transferred_b_to_a");
      xSemaphoreGive(m_mutex); // 释放锁
      break;
    }
    else
    {
      static int potb_wait_count = 0;
      if ((potb_wait_count++ % 10) == 0)
      {
        MAKER_LOG_WARN("TURN", "potb_wait_lock count=%d", potb_wait_count);
      }
      vTaskDelay(200);
    }
  }
  // B锅翻锅完成

  // A锅等待翻锅，翻锅放在循环中，等待锁
  while (millis() < local_pota_turn)
  {
    if (isEmergencyStopRequested())
    {
      finishManagedTask();
      return;
    }
    vTaskDelay(200);
  }
  while (1)
  {
    if (isEmergencyStopRequested())
    {
      finishManagedTask();
      return;
    }
    // 获取互斥锁（阻塞等待直到获取成功）
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) == pdTRUE)
    {
      if (isEmergencyStopRequested())
      {
        xSemaphoreGive(m_mutex);
        finishManagedTask();
        return;
      }
      bool potAEmpty = false;
      bool potBEmpty = false;
      getPotEmptyState(potAEmpty, potBEmpty);
      if (potAEmpty)
      {
        xSemaphoreGive(m_mutex);
        static int pota_egg_wait_count = 0;
        if ((pota_egg_wait_count++ % 10) == 0)
        {
          MAKER_LOG_WARN("TURN", "wait_pota_occupied pot_b_empty=%d", potBEmpty ? 1 : 0);
        }
        vTaskDelay(200);
        continue;
      }

      PotAMotor::getInstance()->takeOut();
      if (isEmergencyStopRequested())
      {
        xSemaphoreGive(m_mutex);
        finishManagedTask();
        return;
      }
      // takeOut返回后，逻辑上认为蛋已经离开A锅。
      markPotAEmpty();
      logPotEmptyState("egg_left_pot_a");
      xSemaphoreGive(m_mutex); // 释放锁
      break;
    }
    else
    {
      static int pota_wait_count = 0;
      if ((pota_wait_count++ % 10) == 0)
      {
        MAKER_LOG_WARN("TURN", "pota_wait_lock count=%d", pota_wait_count);
      }
      vTaskDelay(200);
    }
  }
  // 所有翻面已经完成，重置相关状态和时间
  if (isEmergencyStopRequested())
  {
    finishManagedTask();
    return;
  }
  JiandanDataManager::getInstance()->setNeedMakeFloor(JiandanDataManager::getInstance()->getNeedMakeFloor() - 0.5f);
  JiandanDataManager::getInstance()->setNeedMakeEggs(JiandanDataManager::getInstance()->getNeedMakeEggs() - 0.5);

  if (JiandanDataManager::getInstance()->getNeedMakeFloor() <= 0.0f &&
      JiandanDataManager::getInstance()->getNeedMakeEggs() <= 0.0f)
  {
    JiandanDataManager::getInstance()->setMakerState(IDLE);
    JiandanDataManager::getInstance()->setTotalMakeTime(0);
    Fan::getInstance()->autoTurnOff();
    JiandanDataManager::getInstance()->setStart(false);
  }

  made_count++;
  finishManagedTask();
}

void JiandanMaker::heatPotATask(void *params)
{
  const uint32_t expectedTransferSequence =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(params));
  MAKER_LOG_INFO("HEAT_A", "task_start expected_transfer_seq=%lu",
                 static_cast<unsigned long>(expectedTransferSequence));
  const unsigned long local_potb_turn = POTB_TURN_TIME;
  const unsigned long local_pota_turn = POTA_TURN_TIME;
  const unsigned long local_pota_heat_start = POTA_PRE_HEAT_TIME;
  const unsigned long local_pota_heat_end = (local_pota_turn > POTA_HEAT_END_OFFSET_MS)
                                                ? (local_pota_turn - POTA_HEAT_END_OFFSET_MS)
                                                : local_pota_turn;
  JiandanDataManager::getInstance()->setPotARemainingHeatTime(0);
  if (isEmergencyStopRequested())
  {
    PotI2c::getInstance()->setPotADW(0, 0);
    finishManagedTask();
    return;
  }

  // 在B锅翻锅之前15s开始加热A锅，增加热冲击，减少粘锅概率
  while (local_potb_turn > POTA_HEAT_LEAD_MS && millis() < local_pota_heat_start)
  {
    if (isEmergencyStopRequested())
    {
      PotI2c::getInstance()->setPotADW(0, 0);
      finishManagedTask();
      return;
    }
    vTaskDelay(500);
  }
  int target_temp_a = getTargetTempA();
  // A锅煎制第二面，需要温度小一些
  int init_dw = JiandanDataManager::getInstance()->getInitDw() - 2;
  // A锅初始档位最高限制在4档
  if (init_dw > 4)
  {
    init_dw = 4;
  }
  int preheatDw = init_dw > 2 ? init_dw - 2 : AUTO_DYNAMIC_DW_MIN;
  // 采样数组较大(200*4B*2)，放静态区避免占用任务栈导致栈溢出
  static int temp_point[POT_TEMP_POINT_CAPACITY] = {0};
  static int dw_point[POT_TEMP_POINT_CAPACITY] = {0};
  int temp_point_count = 0;
  int preheat_hold_log_count = 0;

  // A锅先做空锅预热，等B锅翻锅完成后再切入和B锅一致的持续降档加热。
  while (!isEmergencyStopRequested() &&
         !hasPotBTransferReached(expectedTransferSequence) &&
         millis() < local_pota_heat_end)
  {
    JiandanDataManager::getInstance()->setPotARemainingHeatTime(getRemainingSeconds(local_pota_heat_end));
    int current_temp_a = PotI2c::getInstance()->getTempA();
    int sample_dw = current_temp_a >= target_temp_a ? 0 : preheatDw;
    recordTempDwSample(temp_point, dw_point, temp_point_count, current_temp_a, sample_dw);

    if (current_temp_a >= target_temp_a)
    {
      PotI2c::getInstance()->setPotADW(0, 2000);
      if ((preheat_hold_log_count++ % 10) == 0)
      {
        MAKER_LOG_WARN("HEAT_A", "preheat_hold reason=at_target dw=%d temp=%d target=%d",
                       0, current_temp_a, target_temp_a);
      }
      vTaskDelay(1000);
      continue;
    }

    preheat_hold_log_count = 0;
    PotI2c::getInstance()->setPotADW(preheatDw, 2000);
    // MAKER_LOG_INFO("HEAT_A", "preheat remain=%ds dw=%d temp=%d target=%d",
    //                getRemainingSeconds(local_pota_heat_end), preheatDw, current_temp_a, target_temp_a);

    vTaskDelay(1000);
  }

  if (isEmergencyStopRequested())
  {
    PotI2c::getInstance()->setPotADW(0, 0);
    JiandanDataManager::getInstance()->setPotARemainingHeatTime(0);
    finishManagedTask();
    return;
  }

  if (!hasPotBTransferReached(expectedTransferSequence) || millis() >= local_pota_heat_end)
  {
    PotI2c::getInstance()->setPotADW(0, 0);
    if (local_pota_turn == POTA_TURN_TIME)
    {
      JiandanDataManager::getInstance()->setPotARemainingHeatTime(0);
    }
    logTempPoints("HEAT_A", "final_temp_points=", temp_point, dw_point, temp_point_count);
    finishManagedTask();
    return;
  }

  // B锅翻锅完成，A锅持续降档加热
  const int forceShockTotalTime = getRemainingSeconds(local_pota_heat_end);
  int forceShockTime = forceShockTotalTime;
  int fall_dw_gap = calcDwFallTimeGap(JiandanDataManager::getInstance()->getSecondPowerOffset()) - 2;
  if (fall_dw_gap < 1)
  {
    MAKER_LOG_WARN("DW", "invalid pot_a fall_gap=%d clamped=1", fall_dw_gap);
    fall_dw_gap = 1;
  }
  int minShockDw = init_dw < 1 ? init_dw : 1;
  int postTurnSampleCount = 0;

  while (forceShockTime-- > 0)
  {
    if (isEmergencyStopRequested())
    {
      PotI2c::getInstance()->setPotADW(0, 0);
      JiandanDataManager::getInstance()->setPotARemainingHeatTime(0);
      finishManagedTask();
      return;
    }
    JiandanDataManager::getInstance()->setPotARemainingHeatTime(getRemainingSeconds(local_pota_heat_end));
    int current_temp_a = PotI2c::getInstance()->getTempA();
    int elapsedForceShockTime = postTurnSampleCount;
    int shockDw = init_dw - (elapsedForceShockTime / fall_dw_gap);
    if (shockDw < minShockDw)
    {
      shockDw = minShockDw;
    }
    // 过热软上限：A锅温度超过目标+30℃时本秒关闭加热
    if (current_temp_a >= getTargetTempA() + OVERHEAT_MARGIN_C)
    {
      shockDw = 0;
      MAKER_LOG_WARN("HEAT_A", "overheat_cap heating_off temp=%d target=%d threshold=%d",
                     current_temp_a, getTargetTempA(), getTargetTempA() + OVERHEAT_MARGIN_C);
    }
    recordTempDwSample(temp_point, dw_point, temp_point_count, current_temp_a, shockDw);
    postTurnSampleCount++;

    PotI2c::getInstance()->setPotADW(shockDw, 2000);

    vTaskDelay(1000);
  }

  PotI2c::getInstance()->setPotADW(0, 0);
  if (local_pota_turn == POTA_TURN_TIME)
  {
    JiandanDataManager::getInstance()->setPotARemainingHeatTime(0);
  }
  // logTempPoints("HEAT_A", "final_temp_points=", temp_point, dw_point, temp_point_count);
  finishManagedTask();
}

void JiandanMaker::preheatTask(void *params)
{
#define MAX_DW 4

  MAKER_LOG_INFO("PREHEAT", "task_start");
  if (DebugRunControl::getInstance()->isHeatingDisabled())
  {
    MAKER_LOG_WARN("PREHEAT", "motion_test_no_heat skip=true");
    stopAllHeating();
    preheat_done = true;
    finishManagedTask();
    return;
  }

  int *potb_temp_points = (int *)calloc(POT_TEMP_POINT_CAPACITY, sizeof(int));
  int *potb_dw_points = (int *)calloc(POT_TEMP_POINT_CAPACITY, sizeof(int));
  int potb_temp_point_count = 0;
  int target_temp_a = getTargetTempA();
  int target_temp_b = getTargetTempB();
  int preheat_target_a = target_temp_a - 30;
  int preheat_target_b = target_temp_b - 30;

  if (potb_temp_points == NULL || potb_dw_points == NULL)
  {
    MAKER_LOG_ERROR("PREHEAT", "buffer_alloc_failed capacity=%d", POT_TEMP_POINT_CAPACITY);
    free(potb_temp_points);
    free(potb_dw_points);
    stopAllHeating();
    preheat_done = true;
    finishManagedTask();
    return;
  }

  int c = 20;
  int heat_time = 0;
  while (c-- > 0)
  {
    if (shouldAbortPreheat())
    {
      MAKER_LOG_WARN("PREHEAT", "abort stop_heating=true");
      stopAllHeating();
      logTempPoints("PREHEAT", "potb_temp_points=", potb_temp_points, potb_dw_points, potb_temp_point_count);
      free(potb_temp_points);
      free(potb_dw_points);
      preheat_done = true;
      finishManagedTask();
      return;
    }

    const int current_temp_a = PotI2c::getInstance()->getTempA();
    const int current_temp_b = PotI2c::getInstance()->getTempB();
    int current_dw_a = 0;
    int current_dw_b = 0;

    // 忽略温度，强制预热s
    if (heat_time < 5)
    {
      // current_dw_b = JiandanDataManager::getInstance()->getInitDw();

      PotI2c::getInstance()->setPotBDW(JiandanDataManager::getInstance()->getInitDw(), 2000);
      if (is_first_make)
      {
        current_dw_a = JiandanDataManager::getInstance()->getInitDw() - 2;
        PotI2c::getInstance()->setPotADW(current_dw_a, 2000);
      }
      recordTempDwSample(potb_temp_points, potb_dw_points, potb_temp_point_count, current_temp_b, current_dw_b);
      heat_time++;
      vTaskDelay(1000);
      continue;
    }

    if (current_temp_b > preheat_target_b)
    {
      recordTempDwSample(potb_temp_points, potb_dw_points, potb_temp_point_count, current_temp_b, current_dw_b);
      break;
    }

    if (current_temp_b < preheat_target_b)
    {
      current_dw_b = MAX_DW;
      PotI2c::getInstance()->setPotBDW(current_dw_b, 2000);
    }

    if (current_temp_a < preheat_target_a && is_first_make)
    {
      current_dw_a = JiandanDataManager::getInstance()->getInitDw() - 3;
      PotI2c::getInstance()->setPotADW(current_dw_a, 2000);
    }

    recordTempDwSample(potb_temp_points, potb_dw_points, potb_temp_point_count, current_temp_b, current_dw_b);

    heat_time++;
    vTaskDelay(1000);
  }

  MAKER_LOG_INFO("PREHEAT", "complete elapsed=%ds pota_temp=%d/%d potb_temp=%d/%d",
                 heat_time, PotI2c::getInstance()->getTempA(), preheat_target_a,
                 PotI2c::getInstance()->getTempB(), preheat_target_b);
  logTempPoints("PREHEAT", "potb_temp_points=", potb_temp_points, potb_dw_points, potb_temp_point_count);
  free(potb_temp_points);
  free(potb_dw_points);

  preheat_done = true;

  finishManagedTask();
}

void JiandanMaker::heatPotBTask(void *params)
{
  const uint32_t cycleSequence =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(params));
  MAKER_LOG_INFO("HEAT_B", "task_start cycle=%lu",
                 static_cast<unsigned long>(cycleSequence));
  if (isEmergencyStopRequested())
  {
    PotI2c::getInstance()->setPotBDW(0, 0);
    finishManagedTask();
    return;
  }

  // 记录温度点，用于分析热冲击阶段温度变化情况
  // 采样数组较大(200*4B*2)，放静态区避免占用任务栈导致栈溢出
  static int temp_point[POT_TEMP_POINT_CAPACITY] = {0};
  static int dw_point[POT_TEMP_POINT_CAPACITY] = {0};
  int sample_count = 0;

  const int local_single_side = SINGLE_SIDE_MAKE_TIME;

  int single_heat_time = local_single_side - (int)(POTB_HEAT_END_OFFSET_MS / 1000UL); // 提前5秒结束加热
  if (single_heat_time < 0)
  {
    single_heat_time = 0;
  }
  const unsigned long local_potb_heat_end = START_TIME + (unsigned long)single_heat_time * 1000UL;

  // 阶段加热法
  const int forceShockTotalTime = single_heat_time;

  int forceShockTime = forceShockTotalTime;
  int init_dw = JiandanDataManager::getInstance()->getInitDw();

  // 如果是轻油模式，降低初始档位，避免过热
  if (JiandanDataManager::getInstance()->getOilLevel() <= 1)
  {
    init_dw = init_dw > 1 ? init_dw - 1 : 1;
  }

  // 阶段降档加热
  int fall_dw_gap = calcDwFallTimeGap(JiandanDataManager::getInstance()->getFirstPowerOffset());
  if (fall_dw_gap < 1)
  {
    MAKER_LOG_WARN("DW", "invalid pot_b fall_gap=%d clamped=1", fall_dw_gap);
    fall_dw_gap = 1;
  }
  while (forceShockTime-- > 0)
  {
    if (isEmergencyStopRequested())
    {
      PotI2c::getInstance()->setPotBDW(0, 0);
      JiandanDataManager::getInstance()->setPotBRemainingHeatTime(0);
      finishManagedTask();
      return;
    }
    JiandanDataManager::getInstance()->setPotBRemainingHeatTime(getRemainingSeconds(local_potb_heat_end));
    int current_temp_b = PotI2c::getInstance()->getTempB();
    int elapsedForceShockTime = sample_count;
    int shockDw = init_dw - (elapsedForceShockTime / fall_dw_gap);
    if (shockDw < 3)
    {
      shockDw = 3;
    }
    // 过热软上限：B锅温度超过目标+30℃时本秒关闭加热
    if (current_temp_b >= getTargetTempB() + OVERHEAT_MARGIN_C)
    {
      shockDw = 0;
      MAKER_LOG_WARN("HEAT_B", "overheat_cap heating_off temp=%d target=%d threshold=%d",
                     current_temp_b, getTargetTempB(), getTargetTempB() + OVERHEAT_MARGIN_C);
    }
    recordTempDwSample(temp_point, dw_point, sample_count, current_temp_b, shockDw);

    PotI2c::getInstance()->setPotBDW(shockDw, 2000);

    vTaskDelay(1000);
  }

  // 加热结束，关闭加热
  PotI2c::getInstance()->setPotBDW(0, 0);
  JiandanDataManager::getInstance()->setPotBRemainingHeatTime(0);
  finishManagedTask();
}

bool JiandanMaker::checkFirstMake()
{
  is_first_make = false;
  // 根据锅温度检测是否是首次煎蛋
  if (PotI2c::getInstance()->getTempA() < 80 || PotI2c::getInstance()->getTempB() < 80)
  {
    MAKER_LOG_INFO("CTRL", "first_make detected pota_temp=%d potb_temp=%d", PotI2c::getInstance()->getTempA(), PotI2c::getInstance()->getTempB());
    is_first_make = true;
  }
  return is_first_make;
}

int JiandanMaker::calcOilTime(int oilLevel)
{
  int oil_time = 1000; // 默认加油时间，单位毫秒
  switch (oilLevel)
  {
  case 0: // 轻油
    MAKER_LOG_INFO("OIL", "mode=light base_time=1600");
    oil_time = 1600;
    break;
  case 1: // 适油
    MAKER_LOG_INFO("OIL", "mode=normal base_time=2000");
    oil_time = 2000;
    break;
  case 2: // 多油
    MAKER_LOG_INFO("OIL", "mode=heavy base_time=2600");
    oil_time = 2600;
    break;
  default:
    MAKER_LOG_WARN("OIL", "mode=default_fallback base_time=2000 input=%d", oilLevel);
    oil_time = 2000;
    break;
  }

  if (is_first_make)
  {
    oil_time += 200; // 首次制作加长加油时间，单位毫秒
  }

  // 乘上用户的油量偏移调整加油时间
  int offset_percent = JiandanDataManager::getInstance()->getOilLevelOffsetPercent();
  float offset = JiandanDataManager::getInstance()->getOilLevelOffset();
  oil_time = (int)lroundf((float)oil_time * offset);
  MAKER_LOG_INFO("OIL", "calc level=%d offset_percent=%d offset=%.2f final_time=%d",
                 oilLevel, offset_percent, offset, oil_time);
  return oil_time;
}

int JiandanMaker::calcHeatTotalTime()
{
  float current_temp = (PotI2c::getInstance()->getTempA() + PotI2c::getInstance()->getTempB()) / 2.0;
  float env_temp = CommonDataManager::getInstance()->getLocalMinTemp24h();
  int doneness = JiandanDataManager::getInstance()->getDoneness();

  int base_time = 100; // 默认基础时间，环境温度0度，熟度糖心
  switch (doneness)
  {
  case 0: // 全熟
    base_time = 140;
    break;
  case 1: // 溏心
    base_time = 100;
    break;
  case 2: // 软嫩
    base_time = 120;
    break;
  case 3: // 太阳蛋
    base_time = 85;
    break;
  }
  // 如果是首次制作
  if (is_first_make)
  {
    base_time += 20; // 首次制作加长基础时间，单位秒
    MAKER_LOG_INFO("TIME", "first_make_bonus applied base_time=%d", base_time);
  }
  // 油量也影响加热时间
  int oil_level = JiandanDataManager::getInstance()->getOilLevel();

  switch (oil_level)
  {
  case 0: // 轻油
    base_time += 10;
    break;
  case 1: // 适油
    base_time += 0;
    break;
  case 2: // 多油
    base_time -= 5;
    break;
  }

  // 根据用户设置的熟度偏移调整基础时间
  int offset_percent = JiandanDataManager::getInstance()->getDonenessOffsetPercent();
  float offset = JiandanDataManager::getInstance()->getDonenessOffset();
  float adjusted_base_time = (float)base_time * offset;
  // 最终的加热时间由基础时间、当前锅温和环境温度共同决定，当前锅温越高，环境温度越高，加热时间越短
  int time = (int)lroundf(adjusted_base_time - 0.08f * current_temp - 1.0f * env_temp);
  // 钳位到安全区间，避免极端参数下出现生鸡蛋或过度煎制
  constexpr int HEAT_TIME_MIN = 40;
  constexpr int HEAT_TIME_MAX = 160;
  int clamped_time = time;
  if (clamped_time < HEAT_TIME_MIN)
  {
    clamped_time = HEAT_TIME_MIN;
  }
  else if (clamped_time > HEAT_TIME_MAX)
  {
    clamped_time = HEAT_TIME_MAX;
  }
  if (clamped_time != time)
  {
    MAKER_LOG_WARN("TIME", "heat_time_clamped raw=%d clamped=%d range=%d~%d",
                   time, clamped_time, HEAT_TIME_MIN, HEAT_TIME_MAX);
  }
  MAKER_LOG_INFO("TIME", "heat_time doneness=%d current_temp=%.2f env_temp=%.2f heat_time=%d offset_percent=%d offset=%.2f",
                 doneness, current_temp, env_temp, clamped_time, offset_percent, offset);

  return clamped_time;
}

int JiandanMaker::calcDwFallTimeGap(float powerOffset)
{
  int base_dw_fall_time = JiandanDataManager::getInstance()->getPowerDwFallGap();
  int doneness = JiandanDataManager::getInstance()->getDoneness();
  int dw_fall_time = 0;
  switch (doneness)
  {
  case 0: // 全熟
    dw_fall_time = base_dw_fall_time - 8;
    break;
  case 1: // 溏心
    dw_fall_time = base_dw_fall_time;
    break;
  case 2: // 软嫩，煎制时间更长，需要低功率持续更长时间
    dw_fall_time = base_dw_fall_time - 4;
    break;
  case 3: // 太阳蛋
    dw_fall_time = base_dw_fall_time + 2;
    break;
  default:
    dw_fall_time = base_dw_fall_time;
    break;
  }

  if (is_first_make)
  {
    dw_fall_time += 6; // 首次制作增加降档时间，单位秒
    MAKER_LOG_INFO("DW", "first_make_bonus applied base_dw_fall_time=%d", base_dw_fall_time);
  }

  // 功率调节：按倍率缩放降档间隔时间，倍率越大功率越大（高档保持越久）
  int scaled_dw_fall_time = (int)lroundf((float)dw_fall_time * powerOffset);
  if (scaled_dw_fall_time < 0)
  {
    scaled_dw_fall_time = 0;
  }
  MAKER_LOG_INFO("DW", "power_offset_applied power_offset=%.2f pre=%d scaled=%d",
                 powerOffset, dw_fall_time, scaled_dw_fall_time);
  dw_fall_time = scaled_dw_fall_time;

  // 输出最终的降档时间，单位秒
  MAKER_LOG_INFO("DW", "dw_fall_time doneness=%d base_dw_fall_time=%d final_dw_fall_time=%d",
                 doneness, base_dw_fall_time, dw_fall_time);
  return dw_fall_time;
}
