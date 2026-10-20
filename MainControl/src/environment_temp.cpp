#include "environment_temp.h"

#include <Arduino.h>

#include "pot_i2c.h"
#include "common_data_manager.h"

EnvironmentTemp EnvironmentTemp::instance;

float EnvironmentTemp::m_localMin24hTemp = 0.0f;
bool EnvironmentTemp::m_hasLocalMin24hValue = false;
float EnvironmentTemp::m_machineEnvTemp = 0.0f;
bool EnvironmentTemp::m_hasMachineEnvValue = false;
uint8_t EnvironmentTemp::m_machineRiseCount = 0;
bool EnvironmentTemp::m_hasHeatingHistory = false;
uint32_t EnvironmentTemp::m_lastHeatingMs = 0;
bool EnvironmentTemp::m_bucketValid[ENV_TEMP_24H_BUCKET_COUNT] = {false};
uint32_t EnvironmentTemp::m_bucketHour[ENV_TEMP_24H_BUCKET_COUNT] = {0};
float EnvironmentTemp::m_bucketMinTemp[ENV_TEMP_24H_BUCKET_COUNT] = {0.0f};

EnvironmentTemp::EnvironmentTemp()
{
    timer = nullptr;
}

EnvironmentTemp *EnvironmentTemp::getInstance()
{
    return &instance;
}

void EnvironmentTemp::init()
{
    m_localMin24hTemp = ENV_TEMP_DEFAULT;
    m_hasLocalMin24hValue = false;
    m_machineEnvTemp = ENV_TEMP_DEFAULT;
    m_hasMachineEnvValue = false;
    m_machineRiseCount = 0;
    m_hasHeatingHistory = false;
    m_lastHeatingMs = 0;
    for (uint8_t i = 0; i < ENV_TEMP_24H_BUCKET_COUNT; i++)
    {
        m_bucketValid[i] = false;
        m_bucketHour[i] = 0;
        m_bucketMinTemp[i] = 0.0f;
    }

    CommonDataManager::getInstance()->setLocalMinTemp24h(ENV_TEMP_DEFAULT);
    CommonDataManager::getInstance()->setMachineEnvTemp(ENV_TEMP_DEFAULT);
    Serial.printf("EnvTemp reset, default=%.1f, tracking local 24h minimum and machine environment\n",
                  ENV_TEMP_DEFAULT);

    timer = new Ticker(taskToExecute, ENV_TEMP_INTERVAL_MS, 0, MILLIS);
    timer->start();
}

void EnvironmentTemp::update()
{
    if (timer != nullptr)
    {
        timer->update();
    }
}

void EnvironmentTemp::taskToExecute()
{
    const uint32_t now = millis();
    PotI2c *pot = PotI2c::getInstance();
    const int potADw = pot->getPotADW();
    const int potBDw = pot->getPotBDW();
    const float tempA = (float)pot->getTempA();
    const float tempB = (float)pot->getTempB();
    const float avg = (tempA + tempB) / 2.0f;

    if (potADw > 0 || potBDw > 0)
    {
        m_hasHeatingHistory = true;
        m_lastHeatingMs = now;
        return;
    }

    if (m_hasHeatingHistory && (uint32_t)(now - m_lastHeatingMs) < ENV_TEMP_HEAT_COOLDOWN_MS)
    {
        return;
    }

    if (tempA == 0.0f && tempB == 0.0f)
    {
        Serial.println("EnvTemp bootstrap zero sample, skip");
        return;
    }

    if (avg < ENV_TEMP_MIN || avg > ENV_TEMP_MAX)
    {
        return;
    }

    const uint32_t currentHour = now / ENV_TEMP_BUCKET_MS;
    const uint8_t bucketIndex = (uint8_t)(currentHour % ENV_TEMP_24H_BUCKET_COUNT);
    if (!m_bucketValid[bucketIndex] || m_bucketHour[bucketIndex] != currentHour)
    {
        m_bucketValid[bucketIndex] = true;
        m_bucketHour[bucketIndex] = currentHour;
        m_bucketMinTemp[bucketIndex] = avg;
    }
    else if (avg < m_bucketMinTemp[bucketIndex])
    {
        m_bucketMinTemp[bucketIndex] = avg;
    }

    bool hasLocalMin = false;
    float localMin = ENV_TEMP_DEFAULT;
    for (uint8_t i = 0; i < ENV_TEMP_24H_BUCKET_COUNT; i++)
    {
        if (!m_bucketValid[i])
        {
            continue;
        }

        const uint32_t ageHours = currentHour - m_bucketHour[i];
        if (ageHours >= ENV_TEMP_24H_BUCKET_COUNT)
        {
            continue;
        }

        if (!hasLocalMin || m_bucketMinTemp[i] < localMin)
        {
            localMin = m_bucketMinTemp[i];
            hasLocalMin = true;
        }
    }

    if (!hasLocalMin)
    {
        return;
    }

    if (!m_hasLocalMin24hValue || localMin != m_localMin24hTemp)
    {
        Serial.printf("EnvTemp updated 24h local minimum: %.1f\n", localMin);
    }
    m_localMin24hTemp = localMin;
    m_hasLocalMin24hValue = true;
    CommonDataManager::getInstance()->setLocalMinTemp24h(m_localMin24hTemp);

    if (!m_hasMachineEnvValue || avg < m_machineEnvTemp)
    {
        m_machineEnvTemp = avg;
        m_hasMachineEnvValue = true;
        m_machineRiseCount = 0;
        CommonDataManager::getInstance()->setMachineEnvTemp(m_machineEnvTemp);
        Serial.printf("EnvTemp updated machine environment low: %.1f\n", m_machineEnvTemp);
        return;
    }

    if (avg >= m_machineEnvTemp + ENV_TEMP_RISE_CONFIRM_DELTA)
    {
        if (m_machineRiseCount < ENV_TEMP_RISE_CONFIRM_COUNT)
        {
            m_machineRiseCount++;
        }

        if (m_machineRiseCount >= ENV_TEMP_RISE_CONFIRM_COUNT)
        {
            float nextTemp = m_machineEnvTemp + ENV_TEMP_RISE_STEP;
            if (nextTemp > avg)
            {
                nextTemp = avg;
            }

            if (nextTemp != m_machineEnvTemp)
            {
                m_machineEnvTemp = nextTemp;
                CommonDataManager::getInstance()->setMachineEnvTemp(m_machineEnvTemp);
                Serial.printf("EnvTemp machine environment recovered: %.1f, sample=%.1f\n",
                              m_machineEnvTemp, avg);
            }
            m_machineRiseCount = 0;
        }
    }
    else
    {
        m_machineRiseCount = 0;
    }
}
