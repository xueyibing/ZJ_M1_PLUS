#include "fan.h"
#include "gpio.h"
#include <Arduino.h>

Fan Fan::instance;
Fan::Fan() : currentDuty(0), manualOverride(false)
{
}

Fan *Fan::getInstance()
{
    return &instance;
}

void Fan::init()
{
    pinMode(FAN_CTR, OUTPUT);
    digitalWrite(FAN_CTR, LOW);
    currentDuty = 0;
    manualOverride = false;
}

void Fan::applyDuty(int duty)
{
    if (duty <= 0)
    {
        pinMode(FAN_CTR, OUTPUT);
        digitalWrite(FAN_CTR, LOW);
        currentDuty = 0;
        return;
    }

    // Current board wiring exposes the fan as a simple enable line.
    currentDuty = duty;
    pinMode(FAN_CTR, OUTPUT);
    digitalWrite(FAN_CTR, HIGH);
}

void Fan::turnOn(int speed)
{
    manualOverride = true;
    applyDuty(constrain(speed, 0, 255));
}

void Fan::turnOff()
{
    manualOverride = false;
    applyDuty(0);
}

void Fan::autoTurnOn(int speed)
{
    if (manualOverride)
    {
        return;
    }

    applyDuty(constrain(speed, 0, 255));
}

void Fan::autoTurnOff()
{
    if (manualOverride)
    {
        return;
    }

    applyDuty(0);
}

bool Fan::isOn() const
{
    return currentDuty > 0;
}

bool Fan::isManualOverrideEnabled() const
{
    return manualOverride;
}

bool Fan::toggle(int speed)
{
    if (manualOverride)
    {
        turnOff();
    }
    else
    {
        turnOn(speed);
    }

    return isOn();
}
