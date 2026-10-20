// 这个电机的作用是把蛋旋转到锅上
// 电流设置为1A
// uart连接方式，成功，
#ifndef POT_B_MOTOR_H
#define POT_B_MOTOR_H

#include <FastAccelStepper.h>
#include <TMCStepper.h>

enum PotBMotorState
{
    POTB_UNINIT = 0
};

class PotBMotor
{
private:
    static PotBMotor instance;

public:
    static PotBMotor *getInstance();

public:
    PotBMotor();
    void init(FastAccelStepperEngine *engine);
    void home();
    void move(int angle, bool block = true, int speed = 1000, int accel = 1000);
    void turn();
    void stop();
    void enable();
    void relax();
    
    void shakeTurn();
    static void potB_limit();

    bool runSelfTest();

    static PotBMotorState s_state;

private:
    FastAccelStepperEngine *engine = NULL;
    FastAccelStepper *potBStepper = NULL;
};

#endif
