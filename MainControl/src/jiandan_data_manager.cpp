#include <Arduino.h>
#include <math.h>

#include "jiandan_data_manager.h"
#include "error_log_data_manager.h"

JiandanDataManager JiandanDataManager::instance;

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

JiandanDataManager::JiandanDataManager()
{
}

JiandanDataManager *JiandanDataManager::getInstance()
{
    return &instance;
}

void JiandanDataManager::init()
{
    m_needMakeEggs = 0;
    m_needMakeFloor = 0;
    m_remainingTime = 0;
    m_totalMakeTime = 0;
    m_start = false;
    m_oilLevel = 1;
    m_doneness = 1;
    m_potARemainingHeatTime = 0;
    m_potBRemainingHeatTime = 0;
    m_firstPowerOffsetPercent = POWER_OFFSET_DEFAULT_PERCENT;
    m_secondPowerOffsetPercent = POWER_OFFSET_DEFAULT_PERCENT;
    m_maxPotTempLimit = MAX_POT_TEMP_LIMIT_DEFAULT;
    m_initDw = INIT_DW_DEFAULT;
    m_laserCheckEnabled = true;
    m_secondOilEnabled = true;
    m_errorCode = ALL_OK;
    m_makerState = IDLE;
    m_power_dw_fall_gap = POWER_DW_FALL_GAP_DEFAULT_SECONDS;

    m_prefs = new Preferences();
    m_prefs->begin("mynamespace");

    uint32_t count = m_prefs->getUInt("count", 0);
    count++;
    Serial.printf("system boot count: %u\n", count);
    m_prefs->putUInt("count", count);

    bool rewriteOilOffset = false;
    bool rewriteDonenessOffset = false;
    bool rewriteTempAdjust = false;
    bool rewriteFirstPowerOffset = false;
    bool rewriteSecondPowerOffset = false;
    bool rewriteInitDw = false;
    bool rewriteLaserCheck = false;
    bool rewriteSecondOil = false;
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
    bool rewriteLegacyPowerOffset = false;
    const int legacyPowerOffset = loadPercentPreference(m_prefs,
                                                        "pwrOff",
                                                        POWER_OFFSET_DEFAULT_PERCENT,
                                                        POWER_OFFSET_MIN_PERCENT,
                                                        POWER_OFFSET_MAX_PERCENT,
                                                        &rewriteLegacyPowerOffset);
    m_firstPowerOffsetPercent = loadPercentPreference(m_prefs,
                                                       "pwrOff1",
                                                       legacyPowerOffset,
                                                       POWER_OFFSET_MIN_PERCENT,
                                                       POWER_OFFSET_MAX_PERCENT,
                                                       &rewriteFirstPowerOffset);
    m_secondPowerOffsetPercent = loadPercentPreference(m_prefs,
                                                        "pwrOff2",
                                                        legacyPowerOffset,
                                                        POWER_OFFSET_MIN_PERCENT,
                                                        POWER_OFFSET_MAX_PERCENT,
                                                        &rewriteSecondPowerOffset);
    m_initDw = loadBoundedIntPreference(m_prefs,
                                        "initDw",
                                        INIT_DW_DEFAULT,
                                        INIT_DW_MIN,
                                        INIT_DW_MAX,
                                        &rewriteInitDw);
    m_laserCheckEnabled = loadBoundedIntPreference(m_prefs,
                                                   "laserChk",
                                                   1,
                                                   0,
                                                   1,
                                                   &rewriteLaserCheck) != 0;
    m_secondOilEnabled = loadBoundedIntPreference(m_prefs,
                                                  "secondOil",
                                                  1,
                                                  0,
                                                  1,
                                                  &rewriteSecondOil) != 0;
    // 油量不再开放给用户调整，固定为默认「适油(1)」；仅通过油量偏移调节。setOilLevel 暂时保留。
    m_oilLevel = 1;
    if (m_prefs->getInt("oilLvl", 1) != 1)
    {
        rewriteOilLevel = true;  // 归一化 NVS 中可能残留的旧油量值
    }
    m_doneness = loadBoundedIntPreference(m_prefs, "donLvl", 1, 0, 3, &rewriteDoneness);
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
    if (rewriteLegacyPowerOffset)
    {
        m_prefs->putInt("pwrOff", legacyPowerOffset);
    }
    if (rewriteFirstPowerOffset || m_prefs->getType("pwrOff1") == PT_INVALID)
    {
        m_prefs->putInt("pwrOff1", m_firstPowerOffsetPercent);
    }
    if (rewriteSecondPowerOffset || m_prefs->getType("pwrOff2") == PT_INVALID)
    {
        m_prefs->putInt("pwrOff2", m_secondPowerOffsetPercent);
    }
    if (rewriteInitDw)
    {
        m_prefs->putInt("initDw", m_initDw);
    }
    if (rewriteLaserCheck)
    {
        m_prefs->putInt("laserChk", m_laserCheckEnabled ? 1 : 0);
    }
    if (rewriteSecondOil)
    {
        m_prefs->putInt("secondOil", m_secondOilEnabled ? 1 : 0);
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

    Serial.printf("settings: oil=%d%% don=%d%% temp=%d power_first=%d%% power_second=%d%% initDw=%d\n",
                  m_oilLevelOffsetPercent, m_donenessOffsetPercent, m_maxPotTempLimit,
                  m_firstPowerOffsetPercent, m_secondPowerOffsetPercent, m_initDw);
    Serial.printf("prefs: oilLevel=%d doneness=%d\n", m_oilLevel, m_doneness);

    m_prefs->end();
}

float JiandanDataManager::getNeedMakeEggs()
{
    return m_needMakeEggs;
}

int JiandanDataManager::getRemainingTime()
{
    return m_remainingTime;
}

int JiandanDataManager::getTotalMakeTime()
{
    return m_totalMakeTime;
}

float JiandanDataManager::getNeedMakeFloor()
{
    return m_needMakeFloor;
}

void JiandanDataManager::setNeedMakeEggs(float n)
{
    float clamped = n;
    const float maxQuantity = static_cast<float>(maxMakeQuantityForDoneness(m_doneness));

    if (clamped <= 0.0f)
    {
        clamped = 0.0f;
    }
    else if (clamped > maxQuantity)
    {
        clamped = maxQuantity;
    }

    if (m_start && clamped < m_needMakeFloor)
    {
        clamped = m_needMakeFloor;
    }

    m_needMakeEggs = clamped;
}

void JiandanDataManager::setNeedMakeFloor(float n)
{
    if (n <= 0.0f)
    {
        m_needMakeFloor = 0.0f;
    }
    else
    {
        m_needMakeFloor = n;
    }

    if (m_start && m_needMakeEggs < m_needMakeFloor)
    {
        m_needMakeEggs = m_needMakeFloor;
    }
}

void JiandanDataManager::setStart(bool start)
{
    m_start = start;
    if (!m_start)
    {
        m_needMakeEggs = 0;
        m_needMakeFloor = 0;
        m_remainingTime = 0;
        m_totalMakeTime = 0;
        m_potARemainingHeatTime = 0;
        m_potBRemainingHeatTime = 0;
        // 注意：不在此处清除 m_errorCode。
        // 停机（含流水线收尾时 potTurnTask 调用 setStart(false)）不应抹掉
        // 由 setErrorCode() 刚设置的错误码，否则会出现"停机但 UI 无错误提示"的竞态。
        // 新一轮开始时由 maker.start 协议显式清除错误码，逻辑与太阳蛋一致。
    }
}

void JiandanDataManager::setOilLevel(int level)
{
    m_oilLevel = clampIntValue(level, 0, 2);
    m_prefs->begin("mynamespace");
    m_prefs->putInt("oilLvl", m_oilLevel);
    m_prefs->end();
}

void JiandanDataManager::setDoneness(int doneness)
{
    m_doneness = clampIntValue(doneness, 0, 3);
    if (!m_start && m_needMakeEggs > static_cast<float>(maxMakeQuantityForDoneness(m_doneness)))
    {
        m_needMakeEggs = static_cast<float>(maxMakeQuantityForDoneness(m_doneness));
    }
    m_prefs->begin("mynamespace");
    m_prefs->putInt("donLvl", m_doneness);
    m_prefs->end();
}

void JiandanDataManager::setErrorCode(ErrorCode code)
{
    m_errorCode = code;
    if (m_errorCode != ALL_OK)
    {
        ErrorLogDataManager::getInstance()->record(m_errorCode);
        m_needMakeEggs = 0;
        m_needMakeFloor = 0;
        m_remainingTime = 0;
        m_totalMakeTime = 0;
        m_potARemainingHeatTime = 0;
        m_potBRemainingHeatTime = 0;
        m_start = false;
    }
}

ErrorCode JiandanDataManager::getErrorCode()
{
    return m_errorCode;
}

bool JiandanDataManager::getStart()
{
    return m_start;
}

int JiandanDataManager::getOilLevel()
{
    return m_oilLevel;
}

int JiandanDataManager::getDoneness()
{
    return m_doneness;
}

void JiandanDataManager::setRemainingTime(int seconds)
{
    m_remainingTime = seconds;
}

void JiandanDataManager::setTotalMakeTime(int seconds)
{
    m_totalMakeTime = seconds < 0 ? 0 : seconds;
}

float JiandanDataManager::getOilLevelOffset()
{
    return m_oilLevelOffsetPercent / 100.0f;
}

float JiandanDataManager::getDonenessOffset()
{
    return m_donenessOffsetPercent / 100.0f;
}

float JiandanDataManager::getFirstPowerOffset()
{
    return m_firstPowerOffsetPercent / 100.0f;
}

float JiandanDataManager::getSecondPowerOffset()
{
    return m_secondPowerOffsetPercent / 100.0f;
}

float JiandanDataManager::getPowerOffset()
{
    return getFirstPowerOffset();
}

int JiandanDataManager::getOilLevelOffsetPercent()
{
    return m_oilLevelOffsetPercent;
}

int JiandanDataManager::getDonenessOffsetPercent()
{
    return m_donenessOffsetPercent;
}

int JiandanDataManager::getFirstPowerOffsetPercent()
{
    return m_firstPowerOffsetPercent;
}

int JiandanDataManager::getSecondPowerOffsetPercent()
{
    return m_secondPowerOffsetPercent;
}

int JiandanDataManager::getPowerOffsetPercent()
{
    return getFirstPowerOffsetPercent();
}

int JiandanDataManager::getMaxPotTempLimit()
{
    return m_maxPotTempLimit;
}

void JiandanDataManager::setMakerState(MakerState state)
{
    m_makerState = state;
}

MakerState JiandanDataManager::getMakerState()
{
    return m_makerState;
}

void JiandanDataManager::setOilLevelOffsetPercent(int percent)
{
    m_oilLevelOffsetPercent = clampIntValue(percent,
                                            OIL_LEVEL_OFFSET_MIN_PERCENT,
                                            OIL_LEVEL_OFFSET_MAX_PERCENT);
    m_prefs->begin("mynamespace");
    m_prefs->putInt("oilOff", m_oilLevelOffsetPercent);
    m_prefs->end();
}

void JiandanDataManager::setDonenessOffsetPercent(int percent)
{
    m_donenessOffsetPercent = clampIntValue(percent,
                                            DONENESS_OFFSET_MIN_PERCENT,
                                            DONENESS_OFFSET_MAX_PERCENT);
    m_prefs->begin("mynamespace");
    m_prefs->putInt("donOff", m_donenessOffsetPercent);
    m_prefs->end();
}

void JiandanDataManager::setFirstPowerOffsetPercent(int percent)
{
    m_firstPowerOffsetPercent = clampIntValue(percent,
                                              POWER_OFFSET_MIN_PERCENT,
                                              POWER_OFFSET_MAX_PERCENT);
    m_prefs->begin("mynamespace");
    m_prefs->putInt("pwrOff1", m_firstPowerOffsetPercent);
    m_prefs->putInt("pwrOff", m_firstPowerOffsetPercent);
    m_prefs->end();
}

void JiandanDataManager::setSecondPowerOffsetPercent(int percent)
{
    m_secondPowerOffsetPercent = clampIntValue(percent,
                                               POWER_OFFSET_MIN_PERCENT,
                                               POWER_OFFSET_MAX_PERCENT);
    m_prefs->begin("mynamespace");
    m_prefs->putInt("pwrOff2", m_secondPowerOffsetPercent);
    m_prefs->end();
}

void JiandanDataManager::setPowerOffsetPercent(int percent)
{
    setFirstPowerOffsetPercent(percent);
    setSecondPowerOffsetPercent(percent);
    m_prefs->begin("mynamespace");
    m_prefs->putInt("pwrOff", m_firstPowerOffsetPercent);
    m_prefs->end();
}

void JiandanDataManager::setMaxPotTempLimit(int temp)
{
    m_maxPotTempLimit = clampIntValue(temp,
                                      MAX_POT_TEMP_LIMIT_MIN,
                                      MAX_POT_TEMP_LIMIT_MAX);
    m_prefs->begin("mynamespace");
    m_prefs->putInt("tmpOff", m_maxPotTempLimit);
    m_prefs->end();
}

int JiandanDataManager::getInitDw()
{
    return m_initDw;
}

void JiandanDataManager::setInitDw(int dw)
{
    m_initDw = clampIntValue(dw, INIT_DW_MIN, INIT_DW_MAX);
    m_prefs->begin("mynamespace");
    m_prefs->putInt("initDw", m_initDw);
    m_prefs->end();
}

bool JiandanDataManager::getLaserCheckEnabled()
{
    return m_laserCheckEnabled;
}

void JiandanDataManager::setLaserCheckEnabled(bool enabled)
{
    m_laserCheckEnabled = enabled;
    m_prefs->begin("mynamespace");
    m_prefs->putInt("laserChk", m_laserCheckEnabled ? 1 : 0);
    m_prefs->end();
}

bool JiandanDataManager::getSecondOilEnabled()
{
    return m_secondOilEnabled;
}

void JiandanDataManager::setSecondOilEnabled(bool enabled)
{
    m_secondOilEnabled = enabled;
    m_prefs->begin("mynamespace");
    m_prefs->putInt("secondOil", m_secondOilEnabled ? 1 : 0);
    m_prefs->end();
}

int JiandanDataManager::getPowerDwFallGap()
{
    if (m_power_dw_fall_gap < POWER_DW_FALL_GAP_MIN_SECONDS ||
        m_power_dw_fall_gap > POWER_DW_FALL_GAP_MAX_SECONDS)
    {
        m_power_dw_fall_gap = POWER_DW_FALL_GAP_DEFAULT_SECONDS;
    }

    return m_power_dw_fall_gap;
}

void JiandanDataManager::setPowerDwFallGap(int seconds)
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

void JiandanDataManager::setPotARemainingHeatTime(int seconds)
{
    m_potARemainingHeatTime = seconds < 0 ? 0 : seconds;
}
void JiandanDataManager::setPotBRemainingHeatTime(int seconds)
{
    m_potBRemainingHeatTime = seconds < 0 ? 0 : seconds;
}
int JiandanDataManager::getPotARemainingHeatTime()
{
    return m_potARemainingHeatTime;
}
int JiandanDataManager::getPotBRemainingHeatTime()
{
    return m_potBRemainingHeatTime;
}
