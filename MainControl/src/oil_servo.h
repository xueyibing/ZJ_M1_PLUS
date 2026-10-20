#ifndef OIL_SERVO_H
#define OIL_SERVO_H

#include <stdint.h>

// 舵机默认脉宽参数(参照 TestServo 工程)
constexpr int OIL_SERVO_MIN_US = 500;  // 0 度对应脉宽
constexpr int OIL_SERVO_MAX_US = 2500; // 270 度对应脉宽

// 舵机总行程角度(270 度舵机: 500us~2500us 对应 0~270 度)
constexpr int OIL_SERVO_MAX_ANGLE = 270;

// 油管移动到锅A/锅B 对应的舵机角度(可按实际机构调整)
constexpr int OIL_SERVO_POTA_ANGLE = 65;
constexpr int OIL_SERVO_POTB_ANGLE = 65;
constexpr int OIL_SERVO_ZERO_ANGLE = 0; // 回零位置


constexpr int OIL_SERVO_SETTLE_MS = 350;

// 舵机方向反转：true 时将 0°与180°镜像互换(同一角度往反方向转)
constexpr bool OIL_SERVO_REVERSE = true;

// 分步移动的默认步长/步间延时(可通过函数参数按场景覆盖)
//   step_deg     : 每次跳变的角度(度)
//   step_delay_ms: 两次跳变之间的延时(ms)
// 指令速度 ≈ step_deg / step_delay_ms * 1000 (度/秒)
// 默认 2°/10ms ≈ 200°/秒，必要时调用方可按场景传更大延时降低指令率
constexpr int OIL_SERVO_DEFAULT_STEP_DEG = 2;
constexpr int OIL_SERVO_DEFAULT_STEP_DELAY_MS = 10;

class OilServo
{
private:
    static OilServo instance;
    bool attached;
    int currentAngle;

    OilServo();

    uint32_t angleToDuty(int angle) const;
    // 带调速参数的内部核心。step_deg<=0 或 step_delay_ms<=0 时退化为单次跳变。
    // skip_settle=true 时跳过末尾的 OIL_SERVO_SETTLE_MS 稳定延时(用于抖动等连续动作)。
    void writeAngle(int angle, int step_deg, int step_delay_ms, bool skip_settle = false);


public:
    static OilServo *getInstance();

    void init();

    // 舵机带着油管移动到锅A
    // step_deg/step_delay_ms <0 时使用 OIL_SERVO_DEFAULT_* 默认值
    void goPotA(int step_deg = 5,
                int step_delay_ms = 10);
    // 舵机带着油管移动到锅B
    void goPotB(int step_deg = 3,
                int step_delay_ms = 10);
    // 舵机回零位(PotA→Zero 跨 135°，路径长，用较大步长避免过多延时)
    void goZeroFromA(int step_deg = 5,
                     int step_delay_ms = 10);
    // 舵机回零位(PotB→Zero 跨 45°，路径短，用较小步长保证平稳)
    void goZeroFromB(int step_deg = 3,
                     int step_delay_ms = 10);

    void shakeOil(int step_deg = 10,
                     int step_delay_ms = 10);


    // 立即停止新的角度步进并保持当前PWM位置。
    void stopImmediately();

    int getCurrentAngle() const;
};

#endif
