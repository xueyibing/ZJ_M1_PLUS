// 软件模拟 I2C（位翻转），API 与 Arduino TwoWire 兼容子集
// 仅包含 Adafruit_VL53L0X 实际使用的方法：
//   begin / beginTransmission / write / endTransmission / requestFrom /
//   read / available / setClock
//
// 设计要点（稳妥优先）：
//   - SDA/SCL 采用 OPEN_DRAIN + 内部上拉，仅靠拉低驱动总线
//   - 释放时高阻，由上拉拉高，并轮询确认（支持时钟拉伸）
//   - 所有时序使用 esp_rom_delay_us（delayMicroseconds）实现
//   - 默认 100kHz；如需更稳可调用 setClock(50000) 等
//   - 单字节、单事务级别加超时，失败立即返回错误码
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

class SoftWire {
public:
    SoftWire(int sdaPin = -1, int sclPin = -1, uint32_t freqHz = 100000);

    // 与 TwoWire 兼容
    bool begin();                                           // 使用构造时的引脚
    bool begin(int sdaPin, int sclPin, uint32_t freqHz = 0);
    void end();
    void setClock(uint32_t freqHz);

    void beginTransmission(uint8_t address);
    size_t write(uint8_t data);
    size_t write(const uint8_t *data, size_t n);
    uint8_t endTransmission(bool sendStop = true);

    uint8_t requestFrom(uint8_t address, uint8_t count, bool sendStop = true);
    int available();
    int read();
    int peek();
    void flush();

private:
    int _sda;
    int _scl;
    uint32_t _freq;
    uint32_t _halfUs;     // 半周期延时（微秒），至少 1
    uint32_t _stretchUs;  // 时钟拉伸最大等待
    bool _initialized;

    static constexpr size_t kBufSize = 64;  // 满足 VL53L0X 最大 64 字节传输
    uint8_t _txBuf[kBufSize];
    size_t  _txLen;
    uint8_t _txAddr;
    bool    _inTx;

    uint8_t _rxBuf[kBufSize];
    size_t  _rxLen;
    size_t  _rxIdx;

    // 底层 GPIO 操作
    inline void sdaHigh();
    inline void sdaLow();
    inline void sclHigh();
    inline void sclLow();
    inline bool sdaRead();
    inline void halfDelay();
    bool waitSclHigh();      // 时钟拉伸等待，超时返回 false

    // I2C 原语
    bool start();
    bool repeatedStart();
    void stop();
    bool writeByteRaw(uint8_t b); // true = ACK
    uint8_t readByteRaw(bool ack);

    void recomputeTiming();
};
