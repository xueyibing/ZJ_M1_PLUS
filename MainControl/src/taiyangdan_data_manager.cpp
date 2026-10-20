#include <Arduino.h>
#include <math.h>

#include "taiyangdan_data_manager.h"
#include "error_log_data_manager.h"

TaiyangdanDataManager TaiyangdanDataManager::instance;

namespace
{
    int clampIntValue(int value, int minValue, int maxValue)
    {
        if (value < minValue)
        {
            return minValue;
        }
        if (value > maxValue)
        {
            return maxValue;
        }
        return value;
    }

    bool isIntegerPreferenceType(PreferenceType type)
    {
        switch (type)
        {
        case PT_I8:
        case PT_U8:
        case PT_I16:
        case PT_U16:
        case PT_I32:
        case PT_U32:
            return true;
        case PT_I64:
        case PT_U64:
        case PT_STR:
        case PT_BLOB:
        case PT_INVALID:
            break;
        }

        return false;
    }

    int normalizePercentValue(float storedValue, int defaultPercent, int minPercent, int maxPercent)
    {
        if (!isfinite(storedValue))
        {
            return defaultPercent;
        }

        // Compatibility with two historical formats:
        // 1. stored as percent: 100 / 120
        // 2. stored as multiplier: 1.0 / 1.2
        float percentValue = storedValue;
        if (storedValue > 0.0f && storedValue <= 3.0f)
        {
            percentValue = storedValue * 100.0f;
        }

        return clampIntValue((int)lroundf(percentValue), minPercent, maxPercent);
    }

    int loadPercentPreference(Preferences *prefs,
                              const char *key,
                              int defaultPercent,
                              int minPercent,
                              int maxPercent,
                              bool *outNeedRewrite)
    {
        PreferenceType type = prefs->getType(key);
        bool needRewrite = false;
        int value = defaultPercent;

        if (type == PT_BLOB)
        {
            value = normalizePercentValue(prefs->getFloat(key, (float)defaultPercent),
                                          defaultPercent,
                                          minPercent,
                                          maxPercent);
            needRewrite = true;
        }
        else if (isIntegerPreferenceType(type))
        {
            int rawValue = prefs->getInt(key, defaultPercent);
            value = clampIntValue(rawValue, minPercent, maxPercent);
            needRewrite = (value != rawValue);
        }
        else if (type != PT_INVALID)
        {
            needRewrite = true;
        }

        if (outNeedRewrite != nullptr)
        {
            *outNeedRewrite = needRewrite;
        }
        return value;
    }

    int loadBoundedIntPreference(Preferences *prefs,
                                 const char *key,
                                 int defaultValue,
                                 int minValue,
                                 int maxValue,
                                 bool *outNeedRewrite)
    {
        PreferenceType type = prefs->getType(key);
        bool needRewrite = false;
        int value = defaultValue;

        if (isIntegerPreferenceType(type))
        {
            int rawValue = prefs->getInt(key, defaultValue);
            value = clampIntValue(rawValue, minValue, maxValue);
            needRewrite = (value != rawValue);
        }
        else if (type != PT_INVALID)
        {
            needRewrite = true;
        }

        if (outNeedRewrite != nullptr)
        {
            *outNeedRewrite = needRewrite;
        }
        return value;
    }

}

TaiyangdanDataManager::TaiyangdanDataManager()
{
}

TaiyangdanDataManager *TaiyangdanDataManager::getInstance()
{
    return &instance;
}

void TaiyangdanDataManager::init()
{
    m_quantity = 0;
    m_remainingTime = 0;
    m_totalMakeTime = 0;
    m_start = false;
    m_oilLevel = 1;
    m_doneness = 1;
    m_potARemainingHeatTime = 0;
    m_potBRemainingHeatTime = 0;
    m_power_offset = POWER_OFFSET_DEFAULT_PERCENT;
    m_maxPotTempLimit = MAX_POT_TEMP_LIMIT_DEFAULT;
    m_maxDwLimit = 4;
    m_errorCode = ALL_OK;
    m_makerState = IDLE;
    m_power_dw_fall_gap = POWER_DW_FALL_GAP_DEFAULT_SECONDS;

    m_prefs = new Preferences();
    m_prefs->begin("taiyangdan");

    uint32_t count = m_prefs->getUInt("count", 0);
    count++;
    Serial.printf("system boot count: %u\n", count);
    m_prefs->putUInt("count", count);

    bool rewriteOilOffset = false;
    bool rewriteDonenessOffset = false;
    bool rewriteTempAdjust = false;
    bool rewritePowerOffset = false;
    bool rewriteOilLevel = false;
    bool rewriteDoneness = false;

    m_oilLevelOffsetPercent = loadPercentPreference(m_prefs,
                                                    "oilOff",
                                                    OIL_LEVEL_OFFSET_DEFAULT_PERCENT,
                                                    OIL_LEVEL_OFFSET_MIN_PERCENT,
                                                    OIL_LEVEL_OFFSET_MAX_PERCENT,
                                                    &rewriteOilOffset);
    m_donenessOffsetPercent = loadPercentPreference(m_prefs,
                                                  "donOff",
                                                  DONENESS_OFFSET_DEFAULT_PERCENT,
                                                  DONENESS_OFFSET_MIN_PERCENT,
                                                  DONENESS_OFFSET_MAX_PERCENT,
                                                  &rewriteDonenessOffset);
    m_maxPotTempLimit = loadBoundedIntPreference(m_prefs,
                                                 "tmpOff",
                                                 MAX_POT_TEMP_LIMIT_DEFAULT,
                                                 MAX_POT_TEMP_LIMIT_MIN,
                                                 MAX_POT_TEMP_LIMIT_MAX,
                                                 &rewriteTempAdjust);
    m_power_offset = loadPercentPreference(m_prefs,
                                           "pwrOff",
                                           POWER_OFFSET_DEFAULT_PERCENT,
                                           POWER_OFFSET_MIN_PERCENT,
                                           POWER_OFFSET_MAX_PERCENT,
                                           &rewritePowerOffset);
    m_oilLevel = loadBoundedIntPreference(m_prefs, "oilLvl", 1, 0, 2, &rewriteOilLevel);
    m_doneness = loadBoundedIntPreference(m_prefs, "donLvl", 1, 0, 3, &rewriteDoneness);
    if (m_prefs->getType("donLvlA") != PT_INVALID)
    {
        m_prefs->remove("donLvlA");
    }
    if (m_prefs->getType("donLvlB") != PT_INVALID)
    {
        m_prefs->remove("donLvlB");
    }
    if (rewriteOilOffset)
    {
        m_prefs->putInt("oilOff", m_oilLevelOffsetPercent);
    }
    if (rewriteDonenessOffset)
    {
        m_prefs->putInt("donOff", m_donenessOffsetPercent);
    }
    if (rewriteTempAdjust)
    {
        m_prefs->putInt("tmpOff", m_maxPotTempLimit);
    }
    if (rewritePowerOffset)
    {
        m_prefs->putInt("pwrOff", m_power_offset);
    }
    if (m_prefs->getType("dwAdj") != PT_INVALID)
    {
        m_prefs->remove("dwAdj");
    }
    if (rewriteOilLevel)
    {
        m_prefs->putInt("oilLvl", m_oilLevel);
    }
    if (rewriteDoneness)
    {
        m_prefs->putInt("donLvl", m_doneness);
    }
    if (m_prefs->getType("empDist") != PT_INVALID)
    {
        m_prefs->remove("empDist");
    }

    Serial.printf("settings: oil=%d%% don=%d%% temp=%d power=%d%%\n",
                  m_oilLevelOffsetPercent, m_donenessOffsetPercent, m_maxPotTempLimit,
                  m_power_offset);
    Serial.printf("prefs: oilLevel=%d doneness=%d\n", m_oilLevel, m_doneness);

    m_prefs->end();
}

uint8_t TaiyangdanDataManager::getQuantity()
{
    return m_quantity;
}

void TaiyangdanDataManager::setQuantity(uint8_t quantity)
{
    m_quantity = quantity > MAX_MAKE_QUANTITY ? MAX_MAKE_QUANTITY : quantity;
}

bool TaiyangdanDataManager::hasNeed()
{
    return m_quantity > 0;
}

void TaiyangdanDataManager::clearNeed()
{
    m_quantity = 0;
}

int TaiyangdanDataManager::getRemainingTime()
{
    return m_remainingTime;
}

int TaiyangdanDataManager::getTotalMakeTime()
{
    return m_totalMakeTime;
}

void TaiyangdanDataManager::setStart(bool start)
{
    m_start = start;
    if (!m_start)
    {
        clearNeed();
        m_remainingTime = 0;
        m_totalMakeTime = 0;
        m_potARemainingHeatTime = 0;
        m_potBRemainingHeatTime = 0;
    }
}

void TaiyangdanDataManager::setOilLevel(int level)
{
    m_oilLevel = clampIntValue(level, 0, 2);
    m_prefs->begin("taiyangdan");
    m_prefs->putInt("oilLvl", m_oilLevel);
    m_prefs->end();
}

void TaiyangdanDataManager::setDoneness(int doneness)
{
    m_doneness = clampIntValue(doneness, 0, 3);
    m_prefs->begin("taiyangdan");
    m_prefs->putInt("donLvl", m_doneness);
    m_prefs->end();
}

void TaiyangdanDataManager::setErrorCode(ErrorCode code)
{
    m_errorCode = code;
    if (m_errorCode != ALL_OK)
    {
        ErrorLogDataManager::getInstance()->record(m_errorCode);
        clearNeed();
        m_remainingTime = 0;
        m_totalMakeTime = 0;
        m_potARemainingHeatTime = 0;
        m_potBRemainingHeatTime = 0;
        m_start = false;
        m_makerState = IDLE;
    }
}

ErrorCode TaiyangdanDataManager::getErrorCode()
{
    return m_errorCode;
}

bool TaiyangdanDataManager::getStart()
{
    return m_start;
}

int TaiyangdanDataManager::getOilLevel()
{
    return m_oilLevel;
}

int TaiyangdanDataManager::getDoneness()
{
    return m_doneness;
}

void TaiyangdanDataManager::setRemainingTime(int seconds)
{
    m_remainingTime = seconds;
}

void TaiyangdanDataManager::setTotalMakeTime(int seconds)
{
    m_totalMakeTime = seconds < 0 ? 0 : seconds;
}

float TaiyangdanDataManager::getOilLevelOffset()
{
    return m_oilLevelOffsetPercent / 100.0f;
}

float TaiyangdanDataManager::getDonenessOffset()
{
    return m_donenessOffsetPercent / 100.0f;
}

float TaiyangdanDataManager::getPowerOffset()
{
    return m_power_offset / 100.0f;
}

int TaiyangdanDataManager::getOilLevelOffsetPercent()
{
    return m_oilLevelOffsetPercent;
}

int TaiyangdanDataManager::getDonenessOffsetPercent()
{
    return m_donenessOffsetPercent;
}

int TaiyangdanDataManager::getPowerOffsetPercent()
{
    return m_power_offset;
}

int TaiyangdanDataManager::getMaxPotTempLimit()
{
    return m_maxPotTempLimit;
}

void TaiyangdanDataManager::setMakerState(MakerState state)
{
    m_makerState = state;
}

MakerState TaiyangdanDataManager::getMakerState()
{
    return m_makerState;
}

void TaiyangdanDataManager::setOilLevelOffsetPercent(int percent)
{
    m_oilLevelOffsetPercent = clampIntValue(percent,
                                            OIL_LEVEL_OFFSET_MIN_PERCENT,
                                            OIL_LEVEL_OFFSET_MAX_PERCENT);
    m_prefs->begin("taiyangdan");
    m_prefs->putInt("oilOff", m_oilLevelOffsetPercent);
    m_prefs->end();
}

void TaiyangdanDataManager::setDonenessOffsetPercent(int percent)
{
    m_donenessOffsetPercent = clampIntValue(percent,
                                            DONENESS_OFFSET_MIN_PERCENT,
                                            DONENESS_OFFSET_MAX_PERCENT);
    m_prefs->begin("taiyangdan");
    m_prefs->putInt("donOff", m_donenessOffsetPercent);
    m_prefs->end();
}

void TaiyangdanDataManager::setPowerOffsetPercent(int percent)
{
    m_power_offset = clampIntValue(percent,
                                   POWER_OFFSET_MIN_PERCENT,
                                   POWER_OFFSET_MAX_PERCENT);
    m_prefs->begin("taiyangdan");
    m_prefs->putInt("pwrOff", m_power_offset);
    m_prefs->end();
}

void TaiyangdanDataManager::setMaxPotTempLimit(int temp)
{
    m_maxPotTempLimit = clampIntValue(temp,
                                      MAX_POT_TEMP_LIMIT_MIN,
                                      MAX_POT_TEMP_LIMIT_MAX);
    m_prefs->begin("taiyangdan");
    m_prefs->putInt("tmpOff", m_maxPotTempLimit);
    m_prefs->end();
}

int TaiyangdanDataManager::getMaxDwLimit()
{
    return m_maxDwLimit;
}

void TaiyangdanDataManager::setMaxDwLimit(int dw)
{
    m_maxDwLimit = dw < 0 ? 0 : dw;
}

int TaiyangdanDataManager::getPowerDwFallGap()
{
    if (m_power_dw_fall_gap < POWER_DW_FALL_GAP_MIN_SECONDS ||
        m_power_dw_fall_gap > POWER_DW_FALL_GAP_MAX_SECONDS)
    {
        m_power_dw_fall_gap = POWER_DW_FALL_GAP_DEFAULT_SECONDS;
    }

    return m_power_dw_fall_gap;
}

void TaiyangdanDataManager::setPowerDwFallGap(int seconds)
{
    if (seconds <= POWER_DW_FALL_GAP_MIN_SECONDS)
    {
        m_power_dw_fall_gap = POWER_DW_FALL_GAP_MIN_SECONDS;
    }
    else if (seconds >= POWER_DW_FALL_GAP_MAX_SECONDS)
    {
        m_power_dw_fall_gap = POWER_DW_FALL_GAP_MAX_SECONDS;
    }
    else
    {
        m_power_dw_fall_gap = seconds;
    }
}

void TaiyangdanDataManager::setPotARemainingHeatTime(int seconds)
{
    m_potARemainingHeatTime = seconds < 0 ? 0 : seconds;
}
void TaiyangdanDataManager::setPotBRemainingHeatTime(int seconds)
{
    m_potBRemainingHeatTime = seconds < 0 ? 0 : seconds;
}
int TaiyangdanDataManager::getPotARemainingHeatTime()
{
    return m_potARemainingHeatTime;
}
int TaiyangdanDataManager::getPotBRemainingHeatTime()
{
    return m_potBRemainingHeatTime;
}
