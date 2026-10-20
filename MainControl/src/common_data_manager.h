#ifndef COMMON_DATA_MANAGER_H
#define COMMON_DATA_MANAGER_H

class CommonDataManager
{
private:
    static CommonDataManager instance;

public:
    static CommonDataManager *getInstance();
    CommonDataManager();

    void init();

    float getLocalMinTemp24h();
    void setLocalMinTemp24h(float temp);
    float getMachineEnvTemp();
    void setMachineEnvTemp(float temp);

private:
    float m_localMinTemp24h;
    float m_machineEnvTemp;
};

#endif
