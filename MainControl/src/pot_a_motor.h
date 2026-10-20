// 这个电机的作用是把蛋旋转到锅上
// 电流设置为1A
// uart连接方式，成功，
#ifndef POT_A_MOTOR_H
#define POT_A_MOTOR_H

#include <FastAccelStepper.h>
#include <TMCStepper.h>

class PotAMotor
{
private:
    static PotAMotor instance;

public:
    static PotAMotor* getInstance(); 

public:
    PotAMotor();
    void init(FastAccelStepperEngine * engine);
    void home();

    //出蛋
    void move(int angle, bool block = true, int speed = 2000, int accel = 2000);
    //蛋出锅
    void takeOut();
    void turnOut();
    void turnBack();
    void bumpTurn(); //通过自由落体磕一下

    void shakeTurn();
    void potAStop();
    void enable();
    void relax();

    bool runSelfTest();

    static void potA_limit();

private:
    FastAccelStepperEngine * engine = NULL;
    FastAccelStepper *potAStepper = NULL;

};



#endif
