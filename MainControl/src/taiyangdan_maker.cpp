#include "taiyangdan_maker.h"
#include <Arduino.h>
#include "pot_i2c.h"
#include "oil_control.h"
#include "debug_run_control.h"
#include "egg_control.h"
#include "cyd_tof.h"
#include "trans_motor.h"
#include "push_motor.h"
#include "fan.h"
#include "common_data_manager.h"
#include "jiandan_data_manager.h"
#include <math.h>
#include <stdlib.h>

#define MAKER_LOG_INFO(stage, fmt, ...) Serial.printf("[TaiyangdanMaker][INFO][%s] " fmt "\n", stage, ##__VA_ARGS__)
#define MAKER_LOG_WARN(stage, fmt, ...) Serial.printf("[TaiyangdanMaker][WARN][%s] " fmt "\n", stage, ##__VA_ARGS__)
#define MAKER_LOG_ERROR(stage, fmt, ...) Serial.printf("[TaiyangdanMaker][ERROR][%s] " fmt "\n", stage, ##__VA_ARGS__)

namespace
{
  constexpr uint8_t TAIYANGDAN_POT_MASK_A = 0x01;
  constexpr uint8_t TAIYANGDAN_POT_MASK_B = 0x02;
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
  constexpr int OVERHEAT_MARGIN_C = 30;
  uint8_t getSelectedPotMask(TaiyangdanDataManager *dataMg)
  {
    const uint8_t quantity = dataMg->getQuantity();
    uint8_t mask = quantity >= 1 ? TAIYANGDAN_POT_MASK_B : 0;
    if (quantity >= 2)
    {
      mask |= TAIYANGDAN_POT_MASK_A;
    }
    return mask;
  }

  ErrorCode getPotErrorCode(TaiyangdanPot pot, POT_STATE state)
  {
    const bool isPotA = pot == TaiyangdanPot::A;
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
    // A/B锅均煎制太阳蛋第一面，使用相同的目标温度。
    return TaiyangdanDataManager::getInstance()->getMaxPotTempLimit();
  }

  int getTargetTempB()
  {
    return TaiyangdanDataManager::getInstance()->getMaxPotTempLimit();
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
    TaiyangdanDataManager::getInstance()->setPotARemainingHeatTime(0);
    TaiyangdanDataManager::getInstance()->setPotBRemainingHeatTime(0);
    TaiyangdanDataManager::getInstance()->setTotalMakeTime(0);
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
    TaiyangdanDataManager::getInstance()->setPotARemainingHeatTime(0);
    TaiyangdanDataManager::getInstance()->setPotBRemainingHeatTime(0);
  }

  bool shouldAbortPreheat()
  {
    TaiyangdanDataManager *dataMg = TaiyangdanDataManager::getInstance();
    if (!dataMg->getStart() || dataMg->getErrorCode() != ALL_OK || !dataMg->hasNeed())
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
    const int cycle_index = TaiyangdanMaker::getLogCycleIndex();
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
OIL_CHECK_STATE TaiyangdanMaker::oil_check = OIL_CHECK_IDLE;
EGG_CHECK_STATE TaiyangdanMaker::egg_check = EGG_CHECK_IDLE;
ErrorCode TaiyangdanMaker::error_code = ALL_OK;
int TaiyangdanMaker::made_count = 0;
unsigned long TaiyangdanMaker::POTA_START_TIME = 0;
unsigned long TaiyangdanMaker::POTB_START_TIME = 0;
unsigned long TaiyangdanMaker::POTA_END_TIME = 0;
unsigned long TaiyangdanMaker::POTB_END_TIME = 0;
int TaiyangdanMaker::POTA_MAKE_TIME = 0;
int TaiyangdanMaker::POTB_MAKE_TIME = 0;
bool TaiyangdanMaker::preheat_done = false;
bool TaiyangdanMaker::is_first_make = false;
volatile unsigned long TaiyangdanMaker::pota_beat_release_time_ms = 0;
volatile unsigned long TaiyangdanMaker::potb_beat_release_time_ms = 0;

int TaiyangdanMaker::getLogCycleIndex()
{
  return made_count + 1;
}

void TaiyangdanMaker::clearError()
{
  error_code = ALL_OK;
  pota_beat_release_time_ms = 0;
  potb_beat_release_time_ms = 0;
}

void TaiyangdanMaker::stop()
{
  TaiyangdanDataManager *dataMg = TaiyangdanDataManager::getInstance();
  if (dataMg->getStart())
  {
    stopAllHeating();
  }
  dataMg->setStart(false);
  dataMg->setMakerState(IDLE);
}

void TaiyangdanMaker::makerControlTask(void *pp)
{
  TaiyangdanDataManager *dataMg = TaiyangdanDataManager::getInstance();
  for (;;)
  {
    clearError();
    if (dataMg->hasNeed() && dataMg->getStart())
    {
      const uint8_t selected_pots = getSelectedPotMask(dataMg);
      const uint8_t cycle_quantity = dataMg->getQuantity();
      const bool make_pot_a = (selected_pots & TAIYANGDAN_POT_MASK_A) != 0;
      const bool make_pot_b = (selected_pots & TAIYANGDAN_POT_MASK_B) != 0;
      MAKER_LOG_INFO("CTRL", "cycle_start quantity=%u pot_a=%s pot_b=%s",
                     (unsigned)cycle_quantity,
                     make_pot_a ? "true" : "false",
                     make_pot_b ? "true" : "false");
      Fan::getInstance()->autoTurnOn(FAN_TOGGLE_DUTY);

      is_first_make = checkFirstMake(selected_pots);
      const int cycle_oil_level = dataMg->getOilLevel();
      const int cycle_doneness = dataMg->getDoneness();

      MAKER_LOG_INFO("CTRL", "recipe_snapshot first_make=%s oil=%d doneness=%d",
                     is_first_make ? "true" : "false",
                     cycle_oil_level,
                     cycle_doneness);

      dataMg->setMakerState(CHECKING_CYD_TOF);
      // 激光检测开关关闭时：不依赖打蛋位 VL53 传感器，跳过其自检。
      if (JiandanDataManager::getInstance()->getLaserCheckEnabled() &&
          !DebugRunControl::getInstance()->shouldAssumeSuccess(DebugCheckType::TOF_SELF_TEST))
      {
        if (!checkCydTofSensor())
        {
          dataMg->clearNeed();
          dataMg->setStart(false);
          continue;
        }
        MAKER_LOG_INFO("SENSOR", "cyd_tof_check ok");
      }
      else
      {
        MAKER_LOG_WARN("SENSOR", "detection bypassed, skip cyd_tof self-check");
      }

      dataMg->setMakerState(HOMING);

      if (is_first_make)
      {
        home();
        if (shouldAbortPreheat())
        {
          stopAllHeating();
          continue;
        }
        made_count = 0;
        dataMg->setMakerState(CHECKING_POT);
        if (!checkPot(selected_pots))
        {
          continue;
        }
        if (shouldAbortPreheat())
        {
          stopAllHeating();
          continue;
        }
        MAKER_LOG_INFO("SENSOR", "pot_check ok");
      }

      // 预热线程开启，不阻塞给蛋给油流程
      dataMg->setMakerState(PRE_HEATING);
      preheat_done = false;
      if (xTaskCreatePinnedToCore(preheatTask,
                                  "TaiyangdanPreheatTask",
                                  1024 * 4,
                                  reinterpret_cast<void *>(static_cast<uintptr_t>(selected_pots)),
                                  2,
                                  NULL,
                                  1) != pdPASS)
      {
        MAKER_LOG_ERROR("PREHEAT", "task_create failed");
        dataMg->clearNeed();
        dataMg->setStart(false);
        continue;
      }

      // 给蛋
      // 给油
      while (!preheat_done)
      {
        vTaskDelay(100);
      }
      if (!dataMg->getStart() || dataMg->getErrorCode() != ALL_OK)
      {
        stopAllHeating();
        continue;
      }

      // dataMg->setMakerState(GIVING_OIL);
      // if (!giveOil(cycle_oil_level))
      // {
      //   dataMg->clearNeed();
      //   dataMg->setStart(false);
      //   continue;
      // }
      // MAKER_LOG_INFO("OIL", "load_complete");

      dataMg->setMakerState(GIVING_EGG);
      bool egg_and_beat_ok = true;

      /* One egg always uses pot B. For two eggs, start pot B first and then
       * load pot A while pot B is already cooking. Both pans then cook in
       * parallel without any flip or automatic take-out action. */
      if (make_pot_b)
      {
        if (!giveEgg() || !beatEgg(TaiyangdanPot::B) || shouldAbortPreheat())
        {
          egg_and_beat_ok = false;
        }
        else
        {
          POTB_START_TIME = millis();
          POTB_MAKE_TIME = calcHeatTotalTime(TaiyangdanPot::B);
          POTB_END_TIME = POTB_START_TIME + (unsigned long)POTB_MAKE_TIME * 1000UL;
          if (xTaskCreatePinnedToCore(heatPotBTask, "TaiyangdanHeatBTask", 1024 * 8,
                                      NULL, 2, NULL, 1) != pdPASS)
          {
            MAKER_LOG_ERROR("HEAT_B", "task_create failed");
            egg_and_beat_ok = false;
          }
        }
      }

      if (egg_and_beat_ok && shouldAbortPreheat())
      {
        egg_and_beat_ok = false;
      }

      if (egg_and_beat_ok && make_pot_a)
      {
        dataMg->setMakerState(GIVING_EGG);
        if (!giveEgg() || !beatEgg(TaiyangdanPot::A) || shouldAbortPreheat())
        {
          egg_and_beat_ok = false;
        }
        else
        {
          POTA_START_TIME = millis();
          POTA_MAKE_TIME = calcHeatTotalTime(TaiyangdanPot::A);
          POTA_END_TIME = POTA_START_TIME + (unsigned long)POTA_MAKE_TIME * 1000UL;
          if (xTaskCreatePinnedToCore(heatPotATask, "TaiyangdanHeatATask", 1024 * 8,
                                      NULL, 2, NULL, 1) != pdPASS)
          {
            MAKER_LOG_ERROR("HEAT_A", "task_create failed");
            egg_and_beat_ok = false;
          }
        }
      }

      if (!egg_and_beat_ok)
      {
        stopAllHeating();
        dataMg->clearNeed();
        dataMg->setStart(false);
        continue;
      }

      const unsigned long final_end_time = make_pot_a && make_pot_b
                                               ? max(POTA_END_TIME, POTB_END_TIME)
                                               : (make_pot_a ? POTA_END_TIME : POTB_END_TIME);
      const int total_make_time = make_pot_a && make_pot_b
                                      ? max(POTA_MAKE_TIME, POTB_MAKE_TIME)
                                      : (make_pot_a ? POTA_MAKE_TIME : POTB_MAKE_TIME);
      dataMg->setTotalMakeTime(total_make_time);
      dataMg->setMakerState(MAKING);
      MAKER_LOG_INFO("CTRL", "cook_start pota_time=%d potb_time=%d",
                     make_pot_a ? POTA_MAKE_TIME : 0,
                     make_pot_b ? POTB_MAKE_TIME : 0);

      bool cook_aborted = false;
      while (final_end_time > millis())
      {
        if (!dataMg->getStart() || dataMg->getErrorCode() != ALL_OK)
        {
          stopAllHeating();
          dataMg->setMakerState(IDLE);
          cook_aborted = true;
          break;
        }
        dataMg->setRemainingTime(getRemainingSeconds(final_end_time));
        vTaskDelay(500);
      }      if (cook_aborted)
      {
        continue;
      }
      made_count++;
      dataMg->clearNeed();
      dataMg->setStart(false);
      dataMg->setRemainingTime(0);
      dataMg->setTotalMakeTime(0);
      dataMg->setMakerState(IDLE);

      MAKER_LOG_INFO("CTRL", "cycle_complete pot_a=%s pot_b=%s",
                     make_pot_a ? "true" : "false",
                     make_pot_b ? "true" : "false");
      MAKER_LOG_INFO("CTRL", "manual_take_out_required=true");
    }
    else
    {
      if (!dataMg->hasNeed())
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

void TaiyangdanMaker::home()
{
  TransMotor::getInstance()->home();
  PushMotor::getInstance()->home();
}


bool TaiyangdanMaker::checkCydTofSensor()
{
  const CydTofCheckState state = CydTof::getInstance()->checkSensor();
  if (state != CydTofCheckState::OK)
  {
    const ErrorCode error = getCydTofErrorCode(state);
    MAKER_LOG_ERROR("SENSOR", "cyd_tof_check failed state=%d error=%d", (int)state, (int)error);
    TaiyangdanDataManager::getInstance()->setErrorCode(error);
    return false;
  }

  return true;
}

bool TaiyangdanMaker::giveEgg()
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
    if (TaiyangdanDataManager::getInstance()->getErrorCode() != ALL_OK)
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
  if (bypassAfter)
  {
    MAKER_LOG_WARN("EGG", "after-load detection bypassed");
    egg_check = EGG_CHECK_HAS_EGG;
    return true;
  }

  const bool egg_loaded = trayHasEgg();
  if (TaiyangdanDataManager::getInstance()->getErrorCode() != ALL_OK)
  {
    return false;
  }

  if (!egg_loaded)
  {
    MAKER_LOG_ERROR("EGG", "tray_no_egg_after_load");
    egg_check = EGG_CHECK_NO_EGG;
    TaiyangdanDataManager::getInstance()->setErrorCode(EGG_EMPTY);
    return false;
  }

  egg_check = EGG_CHECK_HAS_EGG;
  return true;
}

bool TaiyangdanMaker::giveOil(int oilLevel)
{
  const bool bypassFeedback =
      DebugRunControl::getInstance()->shouldAssumeSuccess(DebugCheckType::OIL_FEEDBACK);
  bool success = OilControl::getInstance()->giveOil(calcOilTime(oilLevel), !bypassFeedback);
  if (!success)
  {
    MAKER_LOG_ERROR("OIL", "dispense failed");
    oil_check = OIL_CHECK_NO_OIL;
    TaiyangdanDataManager::getInstance()->setErrorCode(OIL_EMPTY);
    return false;
  }
  else
  {
    oil_check = OIL_CHECK_HAS_OIL;
    return true;
  }
}

bool TaiyangdanMaker::checkPot(uint8_t selectedPots)
{
  if (DebugRunControl::getInstance()->shouldAssumeSuccess(DebugCheckType::POT_PREFLIGHT))
  {
    MAKER_LOG_WARN("SENSOR", "process bypass enabled, skip pot preflight");
    return true;
  }

  PotI2c *potI2c = PotI2c::getInstance();
  const bool check_pot_a = (selectedPots & TAIYANGDAN_POT_MASK_A) != 0;
  const bool check_pot_b = (selectedPots & TAIYANGDAN_POT_MASK_B) != 0;
  if (potI2c->checkPot(check_pot_a, check_pot_b))
  {
    return true;
  }

  ErrorCode error = ALL_OK;
  if (check_pot_a && potI2c->getPOTA_S_CODE() != POT_OK)
  {
    error = getPotErrorCode(TaiyangdanPot::A, potI2c->getPOTA_S_CODE());
  }
  else if (check_pot_b && potI2c->getPOTB_S_CODE() != POT_OK)
  {
    error = getPotErrorCode(TaiyangdanPot::B, potI2c->getPOTB_S_CODE());
  }

  if (error == ALL_OK)
  {
    error = check_pot_a ? POTA_IIC_ERROR : POTB_IIC_ERROR;
  }
  MAKER_LOG_ERROR("SENSOR", "pot_check failed error=%d", error);
  TaiyangdanDataManager::getInstance()->setErrorCode(error);
  return false;
}

bool TaiyangdanMaker::trayHasEgg()
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
    TaiyangdanDataManager::getInstance()->setErrorCode(error_code);
    return false;
  }

  // 其余（读失败 -1、1~9 等）：传感器自身异常
  MAKER_LOG_ERROR("SENSOR", "cyd_tof_abnormal distance=%d", distance);
  error_code = CYD_TOF_HAVE_ERROR;
  TaiyangdanDataManager::getInstance()->setErrorCode(error_code);
  return false;
}

bool TaiyangdanMaker::beatEgg(TaiyangdanPot pot)
{
  const bool is_pot_a = pot == TaiyangdanPot::A;

  MAKER_LOG_INFO("BEAT", "task_start pot=%c", is_pot_a ? 'A' : 'B');
  if (is_pot_a)
  {
    TransMotor::getInstance()->goPotA();
  }
  else
  {
    TransMotor::getInstance()->goPotB();
  }

  // 打蛋（不关闭，保持与煎蛋一致）
  PushMotor::getInstance()->beatEggNoClose();
  vTaskDelay(500);
  TransMotor::getInstance()->backHomeFromPot(is_pot_a);
  if (is_pot_a)
  {
    pota_beat_release_time_ms = millis();
  }
  else
  {
    potb_beat_release_time_ms = millis();
  }

  vTaskDelay(500);
  TransMotor::getInstance()->goDumpFromHome();
  // 抛壳位先高频抖动，把黏在打蛋器上的蛋壳甩落，再回抽释放。
  PushMotor::getInstance()->pushShakeshell(1);
  PushMotor::getInstance()->close();
  vTaskDelay(500);
  TransMotor::getInstance()->homeFromDump();

  // 激光检测开关关闭时：不依赖打蛋位 VL53，跳过蛋壳残留检测。
  if (!JiandanDataManager::getInstance()->getLaserCheckEnabled() ||
      DebugRunControl::getInstance()->shouldAssumeSuccess(DebugCheckType::SHELL_RESIDUE))
  {
    MAKER_LOG_WARN("BEAT", "detection bypassed, skip shell residue check");
    return true;
  }

  const bool shell_remains = trayHasEgg();
  if (TaiyangdanDataManager::getInstance()->getErrorCode() != ALL_OK)
  {
    return false;
  }

  if (shell_remains)
  {
    MAKER_LOG_WARN("BEAT", "shell_not_clean pot=%c", is_pot_a ? 'A' : 'B');
    TransMotor::getInstance()->dumpRetry();
    const bool shell_still_remains = trayHasEgg();
    if (TaiyangdanDataManager::getInstance()->getErrorCode() != ALL_OK)
    {
      return false;
    }

    if (shell_still_remains)
    {
      MAKER_LOG_ERROR("BEAT", "shell_retry_failed pot=%c", is_pot_a ? 'A' : 'B');
      error_code = BEAT_SHELL_NOT_CLEAN;
      TaiyangdanDataManager::getInstance()->setErrorCode(error_code);
      return false;
    }
    else
    {
      MAKER_LOG_INFO("BEAT", "shell_cleaned pot=%c", is_pot_a ? 'A' : 'B');
    }
  }

  return true;
}

void TaiyangdanMaker::preheatTask(void *params)
{
  const uint8_t selected_pots = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(params));
  const bool heat_pot_a = (selected_pots & TAIYANGDAN_POT_MASK_A) != 0;
  const bool heat_pot_b = (selected_pots & TAIYANGDAN_POT_MASK_B) != 0;
  MAKER_LOG_INFO("PREHEAT", "task_start");
  if (DebugRunControl::getInstance()->isHeatingDisabled())
  {
    MAKER_LOG_WARN("PREHEAT", "motion_test_no_heat skip=true");
    stopAllHeating();
    preheat_done = true;
    vTaskDelete(NULL);
    return;
  }

  const int target_temp_a = getTargetTempA();
  const int target_temp_b = getTargetTempB();
  const int preheat_target_a = target_temp_a - 30;
  const int preheat_target_b = target_temp_b - 30;
  const int max_dw = TaiyangdanDataManager::getInstance()->getMaxDwLimit();
  const int preheat_dw = max_dw > 4 ? 4 : max_dw;
  int heat_time = 0;

  for (int remaining = 10; remaining-- > 0;)
  {
    if (shouldAbortPreheat())
    {
      MAKER_LOG_WARN("PREHEAT", "abort stop_heating=true");
      stopAllHeating();
      preheat_done = true;
      vTaskDelete(NULL);
      return;
    }

    const int current_temp_a = PotI2c::getInstance()->getTempA();
    const int current_temp_b = PotI2c::getInstance()->getTempB();
    int current_dw_a = 0;
    int current_dw_b = 0;

    if (heat_time < 5)
    {
      current_dw_a = heat_pot_a ? max_dw : 0;
      current_dw_b = heat_pot_b ? max_dw : 0;
    }
    else if ((!heat_pot_a || current_temp_a >= preheat_target_a) &&
             (!heat_pot_b || current_temp_b >= preheat_target_b))
    {
      break;
    }
    else
    {
      current_dw_a = heat_pot_a && current_temp_a < preheat_target_a ? preheat_dw : 0;
      current_dw_b = heat_pot_b && current_temp_b < preheat_target_b ? preheat_dw : 0;
    }

    PotI2c::getInstance()->setPotADW(current_dw_a, 2000);
    PotI2c::getInstance()->setPotBDW(current_dw_b, 2000);
    heat_time++;
    vTaskDelay(1000);
  }

  MAKER_LOG_INFO("PREHEAT", "complete elapsed=%ds pota_temp=%d/%d potb_temp=%d/%d",
                 heat_time,
                 PotI2c::getInstance()->getTempA(), preheat_target_a,
                 PotI2c::getInstance()->getTempB(), preheat_target_b);
  preheat_done = true;
  vTaskDelete(NULL);
}

void TaiyangdanMaker::heatPotATask(void *params)
{
  MAKER_LOG_INFO("HEAT_A", "task_start");

  // 采样数组较大(200*4B*2)，放静态区避免占用任务栈导致栈溢出
  static int temp_point[POT_TEMP_POINT_CAPACITY] = {0};
  static int dw_point[POT_TEMP_POINT_CAPACITY] = {0};
  int sample_count = 0;
  const int single_heat_time = POTA_MAKE_TIME;
  const unsigned long heat_end = POTA_END_TIME;
  const int init_dw = TaiyangdanDataManager::getInstance()->getMaxDwLimit();
  int fall_dw_gap = calcDwFallTimeGap(TaiyangdanPot::A);
  if (fall_dw_gap < 1)
  {
    fall_dw_gap = 1;
  }
  TaiyangdanDataManager::getInstance()->setPotARemainingHeatTime(0);

  for (int remaining = single_heat_time; remaining-- > 0;)
  {
    if (!TaiyangdanDataManager::getInstance()->getStart() ||
        TaiyangdanDataManager::getInstance()->getErrorCode() != ALL_OK)
    {
      stopAllHeating();
      vTaskDelete(NULL);
      return;
    }
    TaiyangdanDataManager::getInstance()->setPotARemainingHeatTime(getRemainingSeconds(heat_end));
    const int current_temp_a = PotI2c::getInstance()->getTempA();
    int shock_dw = init_dw - (sample_count / fall_dw_gap);
    if (shock_dw < 1)
    {
      shock_dw = 1;
    }
    // 过热软上限：A锅温度超过目标+30℃时本秒关闭加热
    if (current_temp_a >= getTargetTempA() + OVERHEAT_MARGIN_C)
    {
      shock_dw = 0;
      MAKER_LOG_WARN("HEAT_A", "overheat_cap heating_off temp=%d target=%d threshold=%d",
                     current_temp_a, getTargetTempA(), getTargetTempA() + OVERHEAT_MARGIN_C);
    }
    recordTempDwSample(temp_point, dw_point, sample_count, current_temp_a, shock_dw);

    PotI2c::getInstance()->setPotADW(shock_dw, 2000);
    vTaskDelay(1000);
  }

  PotI2c::getInstance()->setPotADW(0, 0);
  TaiyangdanDataManager::getInstance()->setPotARemainingHeatTime(0);
  vTaskDelete(NULL);
}

void TaiyangdanMaker::heatPotBTask(void *params)
{
  MAKER_LOG_INFO("HEAT_B", "task_start");

  // 记录温度点，用于分析热冲击阶段温度变化情况
  // 采样数组较大(200*4B*2)，放静态区避免占用任务栈导致栈溢出
  static int temp_point[POT_TEMP_POINT_CAPACITY] = {0};
  static int dw_point[POT_TEMP_POINT_CAPACITY] = {0};
  int sample_count = 0;

  const int local_single_side = POTB_MAKE_TIME;
  const int single_heat_time = local_single_side;
  const unsigned long local_potb_heat_end = POTB_END_TIME;

  // 阶段加热法
  const int forceShockTotalTime = single_heat_time;

  int forceShockTime = forceShockTotalTime;
  int init_dw = TaiyangdanDataManager::getInstance()->getMaxDwLimit();

  // 阶段降档加热
  int fall_dw_gap = calcDwFallTimeGap(TaiyangdanPot::B);
  if (fall_dw_gap < 1)
  {
    MAKER_LOG_WARN("DW", "invalid pot_b fall_gap=%d clamped=1", fall_dw_gap);
    fall_dw_gap = 1;
  }
  while (forceShockTime-- > 0)
  {
    if (!TaiyangdanDataManager::getInstance()->getStart() ||
        TaiyangdanDataManager::getInstance()->getErrorCode() != ALL_OK)
    {
      stopAllHeating();
      vTaskDelete(NULL);
      return;
    }
    TaiyangdanDataManager::getInstance()->setPotBRemainingHeatTime(getRemainingSeconds(local_potb_heat_end));
    int current_temp_b = PotI2c::getInstance()->getTempB();
    int elapsedForceShockTime = sample_count;
    int shockDw = init_dw - (elapsedForceShockTime / fall_dw_gap);
    if (shockDw <= 1)
    {
      shockDw = 1;
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
    // MAKER_LOG_INFO("HEAT_B", "force_shock remain=%ds dw=%d temp=%d",
    //                forceShockTime, shockDw, current_temp_b);

    vTaskDelay(1000);
  }

  // 加热结束，关闭加热
  PotI2c::getInstance()->setPotBDW(0, 0);
  TaiyangdanDataManager::getInstance()->setPotBRemainingHeatTime(0);
  logTempPoints("HEAT_B", "final_temp_points=", temp_point, dw_point, sample_count);
  vTaskDelete(NULL);
}

bool TaiyangdanMaker::checkFirstMake(uint8_t selectedPots)
{
  is_first_make = false;
  const bool check_pot_a = (selectedPots & TAIYANGDAN_POT_MASK_A) != 0;
  const bool check_pot_b = (selectedPots & TAIYANGDAN_POT_MASK_B) != 0;
  // 只根据本次实际使用的锅温判断，单个太阳蛋不会被闲置的 A 锅影响。
  if ((check_pot_a && PotI2c::getInstance()->getTempA() < 80) ||
      (check_pot_b && PotI2c::getInstance()->getTempB() < 80))
  {
    MAKER_LOG_INFO("CTRL", "first_make detected pota_temp=%d potb_temp=%d", PotI2c::getInstance()->getTempA(), PotI2c::getInstance()->getTempB());
    is_first_make = true;
  }
  return is_first_make;
}

int TaiyangdanMaker::calcOilTime(int oilLevel)
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
  // 乘上用户的油量偏移调整加油时间
  int offset_percent = TaiyangdanDataManager::getInstance()->getOilLevelOffsetPercent();
  float offset = TaiyangdanDataManager::getInstance()->getOilLevelOffset();
  oil_time = (int)lroundf((float)oil_time * offset);
  MAKER_LOG_INFO("OIL", "calc level=%d offset_percent=%d offset=%.2f final_time=%d",
                 oilLevel, offset_percent, offset, oil_time);
  return oil_time;
}

int TaiyangdanMaker::calcHeatTotalTime(TaiyangdanPot pot)
{
  const bool is_pot_a = pot == TaiyangdanPot::A;
  const float current_temp = is_pot_a ? (float)PotI2c::getInstance()->getTempA()
                                      : (float)PotI2c::getInstance()->getTempB();
  const float env_temp = CommonDataManager::getInstance()->getLocalMinTemp24h();
  const int doneness = TaiyangdanDataManager::getInstance()->getDoneness();

  int base_time = 100;
  switch (doneness)
  {
  case 0:
    base_time = 120;
    break;
  case 1:
    base_time = 150;
    break;
  case 2:
    base_time = 180;
    break;
  case 3:
    base_time = 200;
    break;
  }

  if (is_first_make)
  {
    base_time += 15;
  }

  const int offset_percent = TaiyangdanDataManager::getInstance()->getDonenessOffsetPercent();
  const float offset = TaiyangdanDataManager::getInstance()->getDonenessOffset();
  const int raw_time = (int)lroundf((float)base_time * offset - 0.08f * current_temp - env_temp);
  constexpr int HEAT_TIME_MIN = 100;
  constexpr int HEAT_TIME_MAX = 300;
  int heat_time = raw_time;
  if (heat_time < HEAT_TIME_MIN)
  {
    heat_time = HEAT_TIME_MIN;
  }
  else if (heat_time > HEAT_TIME_MAX)
  {
    heat_time = HEAT_TIME_MAX;
  }

  MAKER_LOG_INFO("TIME", "heat_time pot=%c doneness=%d temp=%.2f env_temp=%.2f heat_time=%d offset_percent=%d",
                 is_pot_a ? 'A' : 'B', doneness, current_temp, env_temp, heat_time, offset_percent);
  return heat_time;
}
int TaiyangdanMaker::calcDwFallTimeGap(TaiyangdanPot pot)
{
  int base_dw_fall_time = TaiyangdanDataManager::getInstance()->getPowerDwFallGap();
  int doneness = TaiyangdanDataManager::getInstance()->getDoneness();
  int dw_fall_time = 0;
  switch (doneness)
  {
  case 0: // 流心
    dw_fall_time = base_dw_fall_time + 2;
    break;
  case 1: // 溏心
    dw_fall_time = base_dw_fall_time;
    break;
  case 2: // 软黄,煎制的时间更长，所以需要低功率持续更长时间，增加降档时间
    dw_fall_time = base_dw_fall_time - 4;
    break;
  case 3: // 全熟
    dw_fall_time = base_dw_fall_time - 8;
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
  float power_offset = TaiyangdanDataManager::getInstance()->getPowerOffset();
  int scaled_dw_fall_time = (int)lroundf((float)dw_fall_time * power_offset);
  if (scaled_dw_fall_time < 0)
  {
    scaled_dw_fall_time = 0;
  }
  MAKER_LOG_INFO("DW", "power_offset_applied power_offset=%.2f pre=%d scaled=%d",
                 power_offset, dw_fall_time, scaled_dw_fall_time);
  dw_fall_time = scaled_dw_fall_time;

  // 输出最终的降档时间，单位秒
  MAKER_LOG_INFO("DW", "dw_fall_time doneness=%d base_dw_fall_time=%d final_dw_fall_time=%d",
                 doneness, base_dw_fall_time, dw_fall_time);
  return dw_fall_time;
}
