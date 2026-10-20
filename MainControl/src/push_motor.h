// 这个电机的作用是把蛋旋转到锅上
// 电流设置为1A
// uart连接方式，成功，
#ifndef PUSH_MOTOR_H
#define PUSH_MOTOR_H

#include <FastAccelStepper.h>
#include <TMCStepper.h>

//推杆电机导程(电机每转一圈, 推杆前进的直线距离, 单位 mm)
#define PUSH_T 4
//直线行程(mm) 换算为电机转角(度): angle = 360 * 行程(mm) / PUSH_T
constexpr int calcAngleByDist(double distanceMm)
{
    return static_cast<int>(360 * distanceMm / PUSH_T);
}

class PushMotor
{
private:
    static PushMotor instance;

public:
    static PushMotor *getInstance();

public:
    PushMotor();
    void init(FastAccelStepperEngine *engine);
    void home();
    bool homeAfterEmergencyStop();

    // 破蛋
    void move(int angle, bool block = true, int speed = 2000, int accel = 2000,
              bool allowDuringEmergencyStop = false, bool stopAtLimit = false);
    void stop();
    void beatEgg();

    void beatEggNoClose();

    bool runSelfTest();
    void pushForDump();
    void stepOpenForDebug();

    //抖落蛋壳
    void pushShakeshell(int times );

    //轻推蛋托
    void pushTray();

    //推开打蛋器，方便清洗
    void openForClear();

    //关闭打蛋器，回到初始位置
    void close();

    void pushTest(int forward, float c);

    void enable();
    void relax();

public:
    static volatile uint8_t s_clearOpenDistanceMm;

private:
    bool homeInternal(bool allowDuringEmergencyStop);
    FastAccelStepper *stepper = NULL;

    int pushSpeed = 2000;
};

#endif
