#ifndef EMERGENCY_STOP_CONTROL_H
#define EMERGENCY_STOP_CONTROL_H

#include <atomic>
#include <stdint.h>

// 跨核共享的立即停止令牌。执行器和制作任务都读取同一状态，
// 避免只停止上层流程、底层复合动作却继续执行后续步骤。
class EmergencyStopControl
{
public:
    static EmergencyStopControl *getInstance();

    void request();
    void clear();
    bool isRequested() const;

    // 可中断延时：返回false表示延时期间收到了立即停止请求。
    bool delayMs(uint32_t durationMs, uint32_t quantumMs = 10) const;

private:
    EmergencyStopControl();
    static EmergencyStopControl instance;
    std::atomic<bool> requested;
};

#endif
