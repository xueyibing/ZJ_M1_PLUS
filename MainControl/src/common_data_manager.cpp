#include "common_data_manager.h"

namespace
{
constexpr float ENV_TEMP_DEFAULT_C = 20.0f;
}

CommonDataManager CommonDataManager::instance;

CommonDataManager::CommonDataManager()
    : m_localMinTemp24h(ENV_TEMP_DEFAULT_C),
      m_machineEnvTemp(ENV_TEMP_DEFAULT_C)
{
}

CommonDataManager *CommonDataManager::getInstance()
{
    return &instance;
}

void CommonDataManager::init()
{
    m_localMinTemp24h = ENV_TEMP_DEFAULT_C;
    m_machineEnvTemp = ENV_TEMP_DEFAULT_C;
}

float CommonDataManager::getLocalMinTemp24h()
{
    return m_localMinTemp24h;
}

void CommonDataManager::setLocalMinTemp24h(float temp)
{
    m_localMinTemp24h = temp;
}

float CommonDataManager::getMachineEnvTemp()
{
    return m_machineEnvTemp;
}

void CommonDataManager::setMachineEnvTemp(float temp)
{
    m_machineEnvTemp = temp;
}
