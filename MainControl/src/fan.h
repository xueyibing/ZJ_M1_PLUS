#ifndef FAN_H
#define FAN_H

constexpr int FAN_TOGGLE_DUTY = 224;

class Fan
{

private:
    static Fan instance;
    int currentDuty;
    bool manualOverride;

    void applyDuty(int duty);

public:
    static Fan *getInstance();
    Fan();

    void init();
    void turnOn(int speed = 255);
    void turnOff();
    void autoTurnOn(int speed = 255);
    void autoTurnOff();
    bool isOn() const;
    bool isManualOverrideEnabled() const;
    bool toggle(int speed = FAN_TOGGLE_DUTY);
};

#endif
