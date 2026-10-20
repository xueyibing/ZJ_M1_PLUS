#include "oil_servo.h"
#include "gpio.h"
#include <Arduino.h>
#include "emergency_stop_control.h"

// 使用 LEDC 直接产生 50Hz 舵机 PWM。
// 说明: FastAccelStepper 在 ESP32-S3 (arduino-esp32 2.x/IDF4) 上会占用 MCPWM 外设,
//       而 ESP32Servo 在 S3 上默认也走 MCPWM，两者会争用同一外设导致主控异常/死机。
//       改用 LEDC 独立通道后，与步进电机(MCPWM/RMT)完全隔离，稳定可靠。
static constexpr int OIL_SERVO_LEDC_CHANNEL = 7;    // 独立 LEDC 通道，避开常用的 0~3
static constexpr int OIL_SERVO_LEDC_TIMER_BITS = 14; // ESP32-S3 LEDC 最大分辨率为 14bit
static constexpr int OIL_SERVO_LEDC_FREQ_HZ = 50;    // 标准舵机 50Hz
static constexpr uint32_t OIL_SERVO_PERIOD_US = 1000000UL / OIL_SERVO_LEDC_FREQ_HZ; // 20000us

OilServo OilServo::instance;

OilServo::OilServo() : attached(false), currentAngle(OIL_SERVO_POTA_ANGLE)
{
}

OilServo *OilServo::getInstance()
{
    return &instance;
}

uint32_t OilServo::angleToDuty(int angle) const
{
    angle = constrain(angle, 0, OIL_SERVO_MAX_ANGLE);
    if (OIL_SERVO_REVERSE)
    {
        angle = OIL_SERVO_MAX_ANGLE - angle; // 方向反转
    }
    // 角度 -> 脉宽(us)
    uint32_t pulseUs = OIL_SERVO_MIN_US +
                       ((uint32_t)angle * (OIL_SERVO_MAX_US - OIL_SERVO_MIN_US)) / OIL_SERVO_MAX_ANGLE;
    // 脉宽 -> LEDC 占空计数(整个周期对应 2^bits)
    uint64_t duty = ((uint64_t)pulseUs << OIL_SERVO_LEDC_TIMER_BITS) / OIL_SERVO_PERIOD_US;
    return (uint32_t)duty;
}

void OilServo::init()
{
    ledcSetup(OIL_SERVO_LEDC_CHANNEL, OIL_SERVO_LEDC_FREQ_HZ, OIL_SERVO_LEDC_TIMER_BITS);
    ledcAttachPin(OIL_SERVO_PIN, OIL_SERVO_LEDC_CHANNEL);
    attached = true;

    // 上电初始化回零位
    currentAngle = OIL_SERVO_ZERO_ANGLE;
    ledcWrite(OIL_SERVO_LEDC_CHANNEL, angleToDuty(currentAngle));
    Serial.printf("OilServo: init on pin %d via LEDC ch%d, angle=%d\n",
                  OIL_SERVO_PIN, OIL_SERVO_LEDC_CHANNEL, currentAngle);
}

void OilServo::writeAngle(int angle, int step_deg, int step_delay_ms, bool skip_settle)
{
    if (!attached)
    {
        Serial.println("OilServo: not initialized");
        return;
    }

    EmergencyStopControl *stop = EmergencyStopControl::getInstance();
    if (stop->isRequested())
    {
        return;
    }

    angle = constrain(angle, 0, OIL_SERVO_MAX_ANGLE);
    const bool moved = angle != currentAngle;

    // 步长/延时非法时退化为单次直达
    const bool useStep = step_deg > 0 && step_delay_ms > 0;
    int step = useStep ? step_deg : 1;
    int delayMs = useStep ? step_delay_ms : 0;

    if (angle > currentAngle)
    {
        for (int a = currentAngle + step; a < angle; a += step)
        {
            if (stop->isRequested()) return;
            ledcWrite(OIL_SERVO_LEDC_CHANNEL, angleToDuty(a));
            currentAngle = a;
            if (delayMs > 0 && !stop->delayMs(delayMs)) return;
        }
    }
    else if (angle < currentAngle)
    {
        for (int a = currentAngle - step; a > angle; a -= step)
        {
            if (stop->isRequested()) return;
            ledcWrite(OIL_SERVO_LEDC_CHANNEL, angleToDuty(a));
            currentAngle = a;
            if (delayMs > 0 && !stop->delayMs(delayMs)) return;
        }
    }

    currentAngle = angle;
    ledcWrite(OIL_SERVO_LEDC_CHANNEL, angleToDuty(angle));

    // PWM 到达目标值不等于舵机机械结构已经到位。仅在角度发生变化时
    // 留出统一的稳定时间，确保调用方随后启动油泵时油管已停稳。
    // 抖动等连续动作通过 skip_settle=true 跳过此延时以保持节奏。
    if (moved && !skip_settle && OIL_SERVO_SETTLE_MS > 0)
    {
        (void)stop->delayMs(OIL_SERVO_SETTLE_MS);
    }
}

void OilServo::stopImmediately()
{
    if (attached)
    {
        ledcWrite(OIL_SERVO_LEDC_CHANNEL, angleToDuty(currentAngle));
    }
}

void OilServo::goPotA(int step_deg, int step_delay_ms)
{
    Serial.printf("OilServo: goPotA -> angle=%d (step=%d, delay=%dms)\n",
                  OIL_SERVO_POTA_ANGLE, step_deg, step_delay_ms);
    writeAngle(OIL_SERVO_POTA_ANGLE, step_deg, step_delay_ms);
}

void OilServo::goPotB(int step_deg, int step_delay_ms)
{
    Serial.printf("OilServo: goPotB -> angle=%d (step=%d, delay=%dms)\n",
                  OIL_SERVO_POTB_ANGLE, step_deg, step_delay_ms);
    writeAngle(OIL_SERVO_POTB_ANGLE, step_deg, step_delay_ms);
}

void OilServo::goZeroFromA(int step_deg, int step_delay_ms)
{
    shakeOil(); 
    Serial.printf("OilServo: goZeroFromA -> angle=%d (step=%d, delay=%dms)\n",
                  OIL_SERVO_ZERO_ANGLE, step_deg, step_delay_ms);
    writeAngle(OIL_SERVO_ZERO_ANGLE, step_deg, step_delay_ms);
}

void OilServo::goZeroFromB(int step_deg, int step_delay_ms)
{
    shakeOil(); 
    Serial.printf("OilServo: goZeroFromB -> angle=%d (step=%d, delay=%dms)\n",
                  OIL_SERVO_ZERO_ANGLE, step_deg, step_delay_ms);
    writeAngle(OIL_SERVO_ZERO_ANGLE, step_deg, step_delay_ms);
}

void OilServo::shakeOil(int step_deg, int step_delay_ms)
{
    if (!attached)
    {
        Serial.println("OilServo: not initialized");
        return;
    }

    EmergencyStopControl *stop = EmergencyStopControl::getInstance();
    if (stop->isRequested())
    {
        return;
    }

    // 以当前角度为中心，左右各 5° 往返 3 次，最后回中心
    const int base      = currentAngle;
    const int amplitude = 5;
    const int rounds    = 3;

    int left  = constrain(base - amplitude, 0, OIL_SERVO_MAX_ANGLE);
    int right = constrain(base + amplitude, 0, OIL_SERVO_MAX_ANGLE);

    Serial.printf("OilServo: shakeOil base=%d [%d<->%d] x%d (step=%d, delay=%dms)\n",
                  base, left, right, rounds, step_deg, step_delay_ms);

    // 抖动是连续快速来回，跳过每段末尾的 settle(350ms) 否则会非常慢。
    // 单段行程跨度只有 10°，直接一步到目标(step_deg >= 10)即可，避免中间细分带来的额外延时。
    const bool skip_settle = true;
    for (int i = 0; i < rounds; ++i)
    {
        writeAngle(right, step_deg, step_delay_ms, skip_settle); // base -> right
        if (stop->isRequested()) return;
        writeAngle(left, step_deg, step_delay_ms, skip_settle);  // right -> left
        if (stop->isRequested()) return;
    }
    writeAngle(base, step_deg, step_delay_ms, skip_settle);      // 回到基准(末段也跳过 settle，回到基准后无需等待)
}

int OilServo::getCurrentAngle() const
{
    return currentAngle;
}
