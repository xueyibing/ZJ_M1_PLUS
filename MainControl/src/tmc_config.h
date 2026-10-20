#ifndef TMC_CONFIG_H
#define TMC_CONFIG_H

#include <Arduino.h>


#ifndef TRANS_USE_DRV8825
#define TRANS_USE_DRV8825 0
#endif

// DRV8825 的 M0/M1/M2 由硬件固定，固件仅使用该值换算脉冲步数。
#ifndef TRANS_USE_DRV8825_MIC
#define TRANS_USE_DRV8825_MIC 32
#endif



// 每个电机的标识，用于查询各自的细分配置
enum TmcMotor
{
    TMC_MOTOR_PUSH = 0, // 破蛋推杆
    TMC_MOTOR_TRANS,    // 转托
    TMC_MOTOR_POTB,     // B锅
    TMC_MOTOR_POTA,     // A锅
    TMC_MOTOR_COUNT
};

class TMCConfig
{
private:
    static TMCConfig instance;

public:
    static TMCConfig *getInstance();

public:
    void init();
    // 是否所有电机驱动都就绪（兼容旧接口）。
    bool isReady() const;
    // 指定电机驱动是否就绪。
    bool isReady(TmcMotor motor) const;

    // 运动前调用：若对应电机驱动未就绪(开机 UART 初始化失败等)则加锁重新初始化，
    // 返回该电机最终是否就绪。已就绪时为无锁快速返回。按电机独立判断，
    // 某一块驱动通信失败不会连累其它就绪的电机。
    bool ensureReady(TmcMotor motor);

    // Read GSTAT without clearing its sticky fault bits. Intended for post-motion diagnostics.
    bool readGstat(TmcMotor motor, uint8_t &status);

    // 确保所有电机驱动就绪（对每个电机依次调用 ensureReady(motor)）。
    bool ensureReady();

    // 获取指定电机的细分（计算步数时使用，必须与驱动实际写入的细分保持一致）
    uint16_t getMicrosteps(TmcMotor motor) const;

    // 运行时设置指定电机的细分（有效值为 2 的幂，范围 8~256）。
    void setMicrosteps(TmcMotor motor, uint16_t microsteps);

    // 运行时设置/获取指定电机的运行电流（RMS，单位 mA）。
    void setCurrent(TmcMotor motor, uint16_t currentMa);
    uint16_t getCurrent(TmcMotor motor) const;

private:
    TMCConfig();
    // 每个电机各自的就绪标志（下标与 TmcMotor 一致），实现按电机独立判断。
    bool m_ready[TMC_MOTOR_COUNT];
    SemaphoreHandle_t m_mutex = nullptr;
    uint16_t m_currentMa[TMC_MOTOR_COUNT];
    uint16_t m_microsteps[TMC_MOTOR_COUNT];
};

#endif
