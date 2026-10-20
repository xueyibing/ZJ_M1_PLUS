#include "SoftWire.h"

// ESP32 的 OPEN_DRAIN 模式：digitalWrite HIGH 时高阻，LOW 时拉低；
// 同时启用内部上拉，避免外部漏接上拉时总线悬空。
// 输入读取始终通过 digitalRead，对 OD 输出引脚同样可读总线电平。

SoftWire::SoftWire(int sdaPin, int sclPin, uint32_t freqHz)
    : _sda(sdaPin), _scl(sclPin), _freq(freqHz), _halfUs(5),
      _stretchUs(1000), _initialized(false), _txLen(0), _txAddr(0),
      _inTx(false), _rxLen(0), _rxIdx(0) {
    recomputeTiming();
}

void SoftWire::recomputeTiming() {
    if (_freq == 0) _freq = 100000;
    uint32_t h = 1000000UL / (2UL * _freq);
    if (h < 1) h = 1;
    _halfUs = h;
}

void SoftWire::setClock(uint32_t freqHz) {
    _freq = freqHz;
    recomputeTiming();
}

bool SoftWire::begin() {
    if (_sda < 0 || _scl < 0) return false;

    // 配置为开漏 + 内部上拉，并把电平先置为高（释放总线）
    pinMode(_sda, OUTPUT_OPEN_DRAIN);
    pinMode(_scl, OUTPUT_OPEN_DRAIN);
    // arduino-esp32 的 OUTPUT_OPEN_DRAIN 不会自动加上拉，这里显式开启
    pinMode(_sda, INPUT_PULLUP);
    pinMode(_scl, INPUT_PULLUP);
    pinMode(_sda, OUTPUT_OPEN_DRAIN);
    pinMode(_scl, OUTPUT_OPEN_DRAIN);
    digitalWrite(_sda, HIGH);
    digitalWrite(_scl, HIGH);

    // 总线复位序列：若 SDA 被从机卡低，最多发 9 个时钟尝试解锁
    halfDelay();
    if (!digitalRead(_sda)) {
        for (int i = 0; i < 9 && !digitalRead(_sda); ++i) {
            sclLow();
            halfDelay();
            sclHigh();
            halfDelay();
        }
        // 然后产生一次 STOP
        sdaLow();
        halfDelay();
        sclHigh();
        halfDelay();
        sdaHigh();
        halfDelay();
    }

    _initialized = true;
    _inTx = false;
    _txLen = 0;
    _rxLen = 0;
    _rxIdx = 0;
    return true;
}

bool SoftWire::begin(int sdaPin, int sclPin, uint32_t freqHz) {
    _sda = sdaPin;
    _scl = sclPin;
    if (freqHz) {
        _freq = freqHz;
        recomputeTiming();
    }
    return begin();
}

void SoftWire::end() {
    if (!_initialized) return;
    pinMode(_sda, INPUT);
    pinMode(_scl, INPUT);
    _initialized = false;
}

inline void SoftWire::halfDelay() {
    delayMicroseconds(_halfUs);
}

inline void SoftWire::sdaHigh() { digitalWrite(_sda, HIGH); }
inline void SoftWire::sdaLow()  { digitalWrite(_sda, LOW);  }
inline void SoftWire::sclLow()  { digitalWrite(_scl, LOW);  }
inline void SoftWire::sclHigh() { digitalWrite(_scl, HIGH); }
inline bool SoftWire::sdaRead() { return digitalRead(_sda) != 0; }

bool SoftWire::waitSclHigh() {
    // 时钟拉伸：等待从机释放 SCL
    sclHigh();
    uint32_t waited = 0;
    while (digitalRead(_scl) == 0) {
        delayMicroseconds(1);
        if (++waited > _stretchUs) return false;
    }
    return true;
}

bool SoftWire::start() {
    sdaHigh();
    sclHigh();
    halfDelay();
    if (!sdaRead()) return false;       // 总线被占用
    if (digitalRead(_scl) == 0) return false;
    sdaLow();
    halfDelay();
    sclLow();
    halfDelay();
    return true;
}

bool SoftWire::repeatedStart() {
    sdaHigh();
    halfDelay();
    if (!waitSclHigh()) return false;
    halfDelay();
    sdaLow();
    halfDelay();
    sclLow();
    halfDelay();
    return true;
}

void SoftWire::stop() {
    sdaLow();
    halfDelay();
    waitSclHigh();
    halfDelay();
    sdaHigh();
    halfDelay();
}

bool SoftWire::writeByteRaw(uint8_t b) {
    for (int i = 0; i < 8; ++i) {
        if (b & 0x80) sdaHigh();
        else          sdaLow();
        halfDelay();
        if (!waitSclHigh()) return false;
        halfDelay();
        sclLow();
        b <<= 1;
    }
    // 读 ACK
    sdaHigh();          // 释放 SDA 让从机驱动
    halfDelay();
    if (!waitSclHigh()) return false;
    bool ack = (digitalRead(_sda) == 0);
    halfDelay();
    sclLow();
    return ack;
}

uint8_t SoftWire::readByteRaw(bool ack) {
    uint8_t v = 0;
    sdaHigh();          // 释放 SDA
    for (int i = 0; i < 8; ++i) {
        v <<= 1;
        halfDelay();
        if (!waitSclHigh()) return 0xFF;
        if (sdaRead()) v |= 1;
        halfDelay();
        sclLow();
    }
    // 发送 ACK / NACK
    if (ack) sdaLow();
    else     sdaHigh();
    halfDelay();
    waitSclHigh();
    halfDelay();
    sclLow();
    sdaHigh();
    return v;
}

void SoftWire::beginTransmission(uint8_t address) {
    _txAddr = address;
    _txLen = 0;
    _inTx = true;
}

size_t SoftWire::write(uint8_t data) {
    if (!_inTx) return 0;
    if (_txLen >= kBufSize) return 0;
    _txBuf[_txLen++] = data;
    return 1;
}

size_t SoftWire::write(const uint8_t *data, size_t n) {
    size_t cnt = 0;
    for (size_t i = 0; i < n; ++i) {
        if (write(data[i]) != 1) break;
        ++cnt;
    }
    return cnt;
}

uint8_t SoftWire::endTransmission(bool sendStop) {
    if (!_initialized) { _inTx = false; return 4; }
    _inTx = false;

    if (!start()) return 4;                              // 4: other error
    if (!writeByteRaw((uint8_t)(_txAddr << 1))) {        // 写地址
        stop();
        return 2;                                        // NACK on address
    }
    for (size_t i = 0; i < _txLen; ++i) {
        if (!writeByteRaw(_txBuf[i])) {
            stop();
            return 3;                                    // NACK on data
        }
    }
    if (sendStop) stop();
    _txLen = 0;
    return 0;
}

uint8_t SoftWire::requestFrom(uint8_t address, uint8_t count, bool sendStop) {
    if (!_initialized) return 0;
    if (count == 0) return 0;
    if (count > kBufSize) count = kBufSize;

    _rxLen = 0;
    _rxIdx = 0;

    if (!start()) return 0;
    if (!writeByteRaw((uint8_t)((address << 1) | 0x01))) {
        stop();
        return 0;
    }
    for (uint8_t i = 0; i < count; ++i) {
        bool ack = (i < (count - 1));    // 最后一字节发 NACK
        _rxBuf[i] = readByteRaw(ack);
    }
    if (sendStop) stop();
    _rxLen = count;
    _rxIdx = 0;
    return count;
}

int SoftWire::available() {
    return (int)(_rxLen - _rxIdx);
}

int SoftWire::read() {
    if (_rxIdx >= _rxLen) return -1;
    return _rxBuf[_rxIdx++];
}

int SoftWire::peek() {
    if (_rxIdx >= _rxLen) return -1;
    return _rxBuf[_rxIdx];
}

void SoftWire::flush() {
    _txLen = 0;
    _rxLen = 0;
    _rxIdx = 0;
}
