#ifndef ENVIRONMENT_TEMP_H
#define ENVIRONMENT_TEMP_H

#include <stdint.h>

#include "Ticker.h"

#define ENV_TEMP_INTERVAL_MS 10000
#define ENV_TEMP_MIN -20.0f
#define ENV_TEMP_MAX 40.0f
#define ENV_TEMP_DEFAULT 20.0f
#define ENV_TEMP_HEAT_COOLDOWN_MS (30UL * 60UL * 1000UL)
#define ENV_TEMP_BUCKET_MS (60UL * 60UL * 1000UL)
#define ENV_TEMP_24H_BUCKET_COUNT 24U
#define ENV_TEMP_RISE_CONFIRM_DELTA 3.0f
#define ENV_TEMP_RISE_CONFIRM_COUNT 12U
#define ENV_TEMP_RISE_STEP 1.0f

class EnvironmentTemp
{
private:
    static EnvironmentTemp instance;

public:
    static EnvironmentTemp *getInstance();
    EnvironmentTemp();

    void init();
    void update();

private:
    static void taskToExecute();

    Ticker *timer;
    static float m_localMin24hTemp;
    static bool m_hasLocalMin24hValue;
    static float m_machineEnvTemp;
    static bool m_hasMachineEnvValue;
    static uint8_t m_machineRiseCount;
    static bool m_hasHeatingHistory;
    static uint32_t m_lastHeatingMs;
    static bool m_bucketValid[ENV_TEMP_24H_BUCKET_COUNT];
    static uint32_t m_bucketHour[ENV_TEMP_24H_BUCKET_COUNT];
    static float m_bucketMinTemp[ENV_TEMP_24H_BUCKET_COUNT];
};

#endif
