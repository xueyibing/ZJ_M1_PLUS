#include "emergency_stop_control.h"

#include <Arduino.h>

EmergencyStopControl EmergencyStopControl::instance;

EmergencyStopControl::EmergencyStopControl() : requested(false)
{
}

EmergencyStopControl *EmergencyStopControl::getInstance()
{
    return &instance;
}

void EmergencyStopControl::request()
{
    requested.store(true, std::memory_order_release);
}

void EmergencyStopControl::clear()
{
    requested.store(false, std::memory_order_release);
}

bool EmergencyStopControl::isRequested() const
{
    return requested.load(std::memory_order_acquire);
}

bool EmergencyStopControl::delayMs(uint32_t durationMs, uint32_t quantumMs) const
{
    if (quantumMs == 0)
    {
        quantumMs = 1;
    }

    uint32_t remaining = durationMs;
    while (remaining > 0)
    {
        if (isRequested())
        {
            return false;
        }

        const uint32_t slice = remaining < quantumMs ? remaining : quantumMs;
        vTaskDelay(pdMS_TO_TICKS(slice));
        remaining -= slice;
    }

    return !isRequested();
}
