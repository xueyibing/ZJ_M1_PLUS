#ifndef TAIYANGDAN_DATA_MANAGER_H
#define TAIYANGDAN_DATA_MANAGER_H

#include <driver/timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <Preferences.h>

#include "gpio.h"

class TaiyangdanDataManager
{
private:
    static TaiyangdanDataManager instance;

public:
    static TaiyangdanDataManager *getInstance();

    static constexpr uint8_t MAX_MAKE_QUANTITY = 2;

    static constexpr int OIL_LEVEL_OFFSET_MIN_PERCENT = 60;
    static constexpr int OIL_LEVEL_OFFSET_MAX_PERCENT = 200;
    static constexpr int OIL_LEVEL_OFFSET_STEP_PERCENT = 10;
    static constexpr int OIL_LEVEL_OFFSET_DEFAULT_PERCENT = 100;

    static constexpr int DONENESS_OFFSET_MIN_PERCENT = 60;
    static constexpr int DONENESS_OFFSET_MAX_PERCENT = 200;
    static constexpr int DONENESS_OFFSET_STEP_PERCENT = 10;
    static constexpr int DONENESS_OFFSET_DEFAULT_PERCENT = 100;

    static constexpr int MAX_POT_TEMP_LIMIT_MIN = 180;
    static constexpr int MAX_POT_TEMP_LIMIT_MAX = 240;
    static constexpr int MAX_POT_TEMP_LIMIT_STEP = 5;
    static constexpr int MAX_POT_TEMP_LIMIT_DEFAULT = 180;

    static constexpr int POWER_OFFSET_MIN_PERCENT = 50;
    static constexpr int POWER_OFFSET_MAX_PERCENT = 500;
    static constexpr int POWER_OFFSET_STEP_PERCENT = 10;
    static constexpr int POWER_OFFSET_DEFAULT_PERCENT = 100;

    static constexpr int POWER_DW_FALL_GAP_MIN_SECONDS = 5;
    static constexpr int POWER_DW_FALL_GAP_MAX_SECONDS = 20;
    static constexpr int POWER_DW_FALL_GAP_DEFAULT_SECONDS = 10;

    TaiyangdanDataManager();
    void init();

    uint8_t getQuantity();
    void setQuantity(uint8_t quantity);
    bool hasNeed();
    void clearNeed();
    int getRemainingTime();
    int getTotalMakeTime();
    void setRemainingTime(int seconds);
    void setTotalMakeTime(int seconds);

    void setErrorCode(ErrorCode code);
    ErrorCode getErrorCode();

    void setStart(bool start);
    bool getStart();
    int getOilLevel();
    int getDoneness();
    void setOilLevel(int level);
    void setDoneness(int doneness);

    float getOilLevelOffset();
    float getDonenessOffset();
    float getPowerOffset();
    int getOilLevelOffsetPercent();
    int getDonenessOffsetPercent();
    int getPowerOffsetPercent();

    void setOilLevelOffsetPercent(int percent);
    void setDonenessOffsetPercent(int percent);
    void setPowerOffsetPercent(int percent);
    void setPotARemainingHeatTime(int seconds);
    void setPotBRemainingHeatTime(int seconds);

    int getPotARemainingHeatTime();
    int getPotBRemainingHeatTime();

    int getMaxPotTempLimit();
    void setMaxPotTempLimit(int temp);

    int getMaxDwLimit();
    void setMaxDwLimit(int dw);

    void setPowerDwFallGap(int seconds);
    int getPowerDwFallGap();

    void setMakerState(MakerState state);

    MakerState getMakerState();

private:
    Preferences *m_prefs;
    uint8_t m_quantity;
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
    int m_maxDwLimit;

    //功率档位下降间隔时间，单位秒，初始默认值为10s，调节范围5-20s
    int m_power_dw_fall_gap;

    //功率调节值，单位百分比，初始默认值为100%，调节范围50%-200%，步进值10%
    int m_power_offset;

    ErrorCode m_errorCode;
    MakerState m_makerState;
};

#endif
