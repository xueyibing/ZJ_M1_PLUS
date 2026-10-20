// 这个电机的作用是把蛋旋转到锅上
// 电流设置为1A
// uart连接方式，成功，
#ifndef TRANS_MOTOR_H
#define TRANS_MOTOR_H

#include <FastAccelStepper.h>
#include <TMCStepper.h>

enum TransMotorState
{
    TRANS_UNINIT = 0,
    TRANS_IN_MAKE,
    TRANS_IN_MIDDLE,
    TRANS_IN_DUMP
};

class TransMotor
{
private:
    static TransMotor instance;

public:
    static TransMotor *getInstance();

public:
    TransMotor();
    void init(FastAccelStepperEngine *engine);
    void home();
    bool homeAfterEmergencyStop();

    // 转运
    void move(float angle, bool block = true, int speed = 2000, int accel = 2000,
              bool allowDuringEmergencyStop = false);
    void goPotB();
    void goPotA();
    void backHomeFromPot(bool fromPotA = false);
    void goDumpFromHome();
    void clearBeat();
    void stop();
    void homeFromDump();
    void relax();
    void enable();
    void dumpRetry();//二次抛壳
    void dumpMock();//调试的时候模拟转运流程
    void shakeSideways(); //左右抖动。让蛋液落下

    static void trans_limit();

    bool runSelfTest();

static volatile TransMotorState s_state;
static volatile bool s_limitTriggered;

private:
    bool homeInternal(bool allowDuringEmergencyStop);
    FastAccelStepper *stepper = NULL;
    
};

#endif
