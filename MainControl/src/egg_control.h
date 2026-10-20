#ifndef EGG_CONTROL_H
#define EGG_CONTROL_H
#include "Ticker.h"

class EggControl
{

private:
    static EggControl instance;
public:
    static EggControl *getInstance();
    EggControl();

    void init();
    void home();
    void open();
    void close();
    void give();
    void stopImmediately();
    void homeAfterCancel();

    bool runSelfTest();

private:
    void closeInternal(bool allowDuringCancel);
};

#endif
