#ifndef OIL_CONTROL_H
#define OIL_CONTROL_H
#include "Ticker.h"

// OilControl 使用独立 PWM 通道，避免和 EggControl 的 0/1 通道冲突
constexpr int OIL_PWM_FREQ = 20000;
constexpr int OIL_PWM_RESOLUTION = 8;
constexpr int OIL_PWM_CHANNEL_IN1 = 4;
constexpr int OIL_PWM_CHANNEL_IN2 = 5;
constexpr int OIL_PWM_RUN_DUTY = 170;  // 24V 母线下等效约 12V 的保持占空比
constexpr int OIL_PWM_KICK_DUTY = 180; // 启动瞬间提高占空比，避免油泵卡滞
constexpr int OIL_PWM_KICK_MS = 20;

class OilControl
{

private:
    static OilControl instance;

public:
    static OilControl *getInstance();
    OilControl();

    void init();
    bool giveOil(int milliseconds = 500, bool requireFeedback = true);
    void backOil();
    void startMotor(bool dir, int mills = 1000);
    void stopImmediately();
    bool runSelfTest();

private:
    void setMotorDuty(bool dir, int duty);
    void stopMotor();
};

#endif
