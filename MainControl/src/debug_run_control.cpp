#include "debug_run_control.h"

#include <Arduino.h>

#include "pot_i2c.h"
#include "jiandan_data_manager.h"
#include "taiyangdan_data_manager.h"

namespace
{
constexpr int DEBUG_MIN_VALID_TEMP_C = -40;
constexpr int DEBUG_TEMP_SAFETY_MARGIN_C = 15;
constexpr const char *DEBUG_PREFS_NAMESPACE = "debug_run";
constexpr const char *DEBUG_PREFS_BYPASS_MASK = "bypass";
constexpr const char *DEBUG_PREFS_NO_HEAT = "no_heat";
}

DebugRunControl DebugRunControl::instance;

DebugRunControl::DebugRunControl()
        : m_prefs(nullptr),
            m_initialized(false),
            m_bypassMask(0),
      m_heatingDisabled(false)
{
}

DebugRunControl *DebugRunControl::getInstance()
{
    return &instance;
}

void DebugRunControl::init()
{
    if (m_initialized)
    {
        return;
    }

    m_prefs = new Preferences();
    if (!m_prefs->begin(DEBUG_PREFS_NAMESPACE, false))
    {
        delete m_prefs;
        m_prefs = nullptr;
        Serial.println("[DebugRun] preferences init failed");
        return;
    }

    m_bypassMask = m_prefs->getUChar(DEBUG_PREFS_BYPASS_MASK, 0) & ALL_BYPASS_MASK;
    m_heatingDisabled = m_prefs->getBool(DEBUG_PREFS_NO_HEAT, false);
    m_initialized = true;
    Serial.printf("[DebugRun] restored bypass mask=0x%02X no-heat=%d\n",
                  m_bypassMask,
                  m_heatingDisabled ? 1 : 0);
}

bool DebugRunControl::isBypassEnabled() const
{
    return m_bypassMask != 0;
}

void DebugRunControl::setBypassEnabled(bool enabled)
{
    setBypassMask(enabled ? ALL_BYPASS_MASK : 0);
}

uint8_t DebugRunControl::getBypassMask() const
{
    return m_bypassMask;
}

void DebugRunControl::setBypassMask(uint8_t mask)
{
    const uint8_t normalizedMask = mask & ALL_BYPASS_MASK;
    if (m_bypassMask == normalizedMask)
    {
        return;
    }

    m_bypassMask = normalizedMask;
    if (m_initialized)
    {
        m_prefs->putUChar(DEBUG_PREFS_BYPASS_MASK, normalizedMask);
    }
    Serial.printf("[DebugRun] process bypass mask=0x%02X\n", normalizedMask);
}

bool DebugRunControl::isCheckBypassed(DebugCheckType check) const
{
    return (m_bypassMask & checkMask(check)) != 0;
}

void DebugRunControl::setCheckBypassed(DebugCheckType check, bool enabled)
{
    uint8_t mask = getBypassMask();
    const uint8_t bit = checkMask(check);
    setBypassMask(enabled ? (mask | bit) : (mask & ~bit));
}

bool DebugRunControl::isHeatingDisabled() const
{
    return m_heatingDisabled;
}

void DebugRunControl::setHeatingDisabled(bool disabled)
{
    if (m_heatingDisabled == disabled)
    {
        return;
    }

    m_heatingDisabled = disabled;
    if (m_initialized)
    {
        m_prefs->putBool(DEBUG_PREFS_NO_HEAT, disabled);
    }
    Serial.printf("[DebugRun] motion test no-heat %s\n", disabled ? "enabled" : "disabled");

    if (disabled)
    {
        PotI2c::getInstance()->setPotADW(0, 0);
        PotI2c::getInstance()->setPotBDW(0, 0);
    }
}

bool DebugRunControl::shouldAssumeSuccess(DebugCheckType check) const
{
    return isCheckBypassed(check);
}

uint8_t DebugRunControl::checkMask(DebugCheckType check)
{
    const uint8_t index = static_cast<uint8_t>(check);
    return index < 6U ? static_cast<uint8_t>(1U << index) : 0;
}

bool DebugRunControl::canHeatPotA() const
{
    return canHeatPot(true);
}

bool DebugRunControl::canHeatPotB() const
{
    return canHeatPot(false);
}

bool DebugRunControl::canHeatPot(bool potA) const
{
    if (!isBypassEnabled())
    {
        return true;
    }

    PotI2c *pot = PotI2c::getInstance();
    const POT_STATE state = potA ? pot->getPOTA_S_CODE() : pot->getPOTB_S_CODE();
    const int temp = potA ? pot->getTempA() : pot->getTempB();

    if (state != POT_OK || temp < DEBUG_MIN_VALID_TEMP_C)
    {
        return false;
    }

    int maxTemp = JiandanDataManager::getInstance()->getMaxPotTempLimit();
    if (TaiyangdanDataManager::getInstance()->getStart())
    {
        maxTemp = TaiyangdanDataManager::getInstance()->getMaxPotTempLimit();
    }

    return temp < maxTemp + DEBUG_TEMP_SAFETY_MARGIN_C;
}
