#ifndef CYD_TOF_H
#define CYD_TOF_H

#include "gpio.h"
#include "cyd3s1612.h"

enum class CydTofCheckState : uint8_t
{
    OK = 0,
    SENSOR_ERROR,
    BLOCKED
};

class CydTof
{
private:
    static CydTof instance;

public:
    static CydTof *getInstance();
    CydTof();
    void init();
    void initHave();
    CydTofCheckState checkSensor();
    int getHaveDistance(bool verbose = false);

private:
    CYD3S1612 m_haveSensor;
    bool m_haveReady = false;
};

#endif
