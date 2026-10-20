#ifndef JIANDAN_DATA_MANAGER_H
#define JIANDAN_DATA_MANAGER_H

#include <driver/timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <Preferences.h>

#include "gpio.h"

class JiandanDataManager
{
private:
    static JiandanDataManager instance;

public:
    static JiandanDataManager *getInstance();

    static constexpr int MAX_MAKE_QUANTITY = 6;
    static constexpr int SUNNY_DONENESS = 3;
    static constexpr int SUNNY_MAX_MAKE_QUANTITY = 2;

    static constexpr int maxMakeQuantityForDoneness(int doneness)
    {
        return doneness == SUNNY_DONENESS ? SUNNY_MAX_MAKE_QUANTITY : MAX_MAKE_QUANTITY;
    }

    static constexpr int OIL_LEVEL_OFFSET_MIN_PERCENT = 60;
    static constexpr int OIL_LEVEL_OFFSET_MAX_PERCENT = 200;
    static constexpr int OIL_LEVEL_OFFSET_STEP_PERCENT = 10;
    static constexpr int OIL_LEVEL_OFFSET_DEFAULT_PERCENT = 100;

    static constexpr int DONENESS_OFFSET_MIN_PERCENT = 60;
    static constexpr int DONENESS_OFFSET_MAX_PERCENT = 200;
    static constexpr int DONENESS_OFFSET_STEP_PERCENT = 10;
    static constexpr int DONENESS_OFFSET_DEFAULT_PERCENT = 100;

    static constexpr int MAX_POT_TEMP_LIMIT_MIN = 180;
    static constexpr int MAX_POT_TEMP_LIMIT_MAX = 300;
    static constexpr int MAX_POT_TEMP_LIMIT_STEP = 5;
    static constexpr int MAX_POT_TEMP_LIMIT_DEFAULT = 180;

    static constexpr int POWER_OFFSET_MIN_PERCENT = 50;
    static constexpr int POWER_OFFSET_MAX_PERCENT = 500;
    static constexpr int POWER_OFFSET_STEP_PERCENT = 10;
    static constexpr int POWER_OFFSET_DEFAULT_PERCENT = 100;

    static constexpr int INIT_DW_MIN = 4;
    static constexpr int INIT_DW_MAX = 8;
    static constexpr int INIT_DW_STEP = 1;
    static constexpr int INIT_DW_DEFAULT = 7;

    static constexpr int POWER_DW_FALL_GAP_MIN_SECONDS = 5;
    static constexpr int POWER_DW_FALL_GAP_MAX_SECONDS = 20;
    static constexpr int POWER_DW_FALL_GAP_DEFAULT_SECONDS = 10;

    JiandanDataManager();
    void init();

    float getNeedMakeEggs();
    int getRemainingTime();
    int getTotalMakeTime();
    void setRemainingTime(int seconds);
    void setTotalMakeTime(int seconds);
    float getNeedMakeFloor();
    void setNeedMakeFloor(float n);

    void setErrorCode(ErrorCode code);
    ErrorCode getErrorCode();

    void setStart(bool start);
    bool getStart();
    void setNeedMakeEggs(float n);
    int getOilLevel();
    int getDoneness();
    void setOilLevel(int level);
    void setDoneness(int doneness);

    float getOilLevelOffset();
    float getDonenessOffset();
    float getFirstPowerOffset();
    float getSecondPowerOffset();
    // Legacy accessors: the shared setter applies the value to both faces.
    float getPowerOffset();
    int getOilLevelOffsetPercent();
    int getDonenessOffsetPercent();
    int getFirstPowerOffsetPercent();
    int getSecondPowerOffsetPercent();
    int getPowerOffsetPercent();

    void setOilLevelOffsetPercent(int percent);
    void setDonenessOffsetPercent(int percent);
    void setFirstPowerOffsetPercent(int percent);
    void setSecondPowerOffsetPercent(int percent);
    void setPowerOffsetPercent(int percent);
    void setPotARemainingHeatTime(int seconds);
    void setPotBRemainingHeatTime(int seconds);

    int getPotARemainingHeatTime();
    int getPotBRemainingHeatTime();

    int getMaxPotTempLimit();
    void setMaxPotTempLimit(int temp);

    int getInitDw();
    void setInitDw(int dw);

    // 激光检测开关：是否在给蛋步骤执行 ToF 验蛋(有蛋/无蛋)检测
    bool getLaserCheckEnabled();
    void setLaserCheckEnabled(bool enabled);
    // 双面煎蛋第二面开始前，是否给A锅补充一次油。
    bool getSecondOilEnabled();
    void setSecondOilEnabled(bool enabled);

    void setPowerDwFallGap(int seconds);
    int getPowerDwFallGap();

    void setMakerState(MakerState state);

    MakerState getMakerState();

private:
    Preferences *m_prefs;
    float m_needMakeEggs;
    float m_needMakeFloor;
    int m_remainingTime;
    int m_totalMakeTime;
    bool m_start;
    int m_oilLevel;
    int m_doneness;

    // Raw UI/heartbeat values are stored as integer percentages.
    int m_oilLevelOffsetPercent;
    int m_donenessOffsetPercent;
    int m_potARemainingHeatTime;
    int m_potBRemainingHeatTime;

    int m_maxPotTempLimit;
    int m_initDw;
    bool m_laserCheckEnabled;
    bool m_secondOilEnabled;

    //功率档位下降间隔时间，单位秒，初始默认值为10s，调节范围5-20s
    int m_power_dw_fall_gap;

    // 第一面(B锅)/第二面(A锅)功率调节值，单位百分比。
    int m_firstPowerOffsetPercent;
    int m_secondPowerOffsetPercent;

    ErrorCode m_errorCode;
    MakerState m_makerState;
};

#endif
