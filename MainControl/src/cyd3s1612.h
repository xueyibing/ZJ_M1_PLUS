#ifndef CYD3S1612_H
#define CYD3S1612_H

#include <Arduino.h>
#include <Wire.h>

#define CYD3S1612_DEFAULT_ADDR 0x30
#define CYD3S1612_DISTANCE_MIN_MM 20
#define CYD3S1612_DISTANCE_MAX_MM 5000
#define CYD3S1612_DISTANCE_INVALID 0xFFFF

#define CYD3S1612_REG_VERSION 0x00
#define CYD3S1612_REG_DISTANCE_H 0x01
#define CYD3S1612_REG_DISTANCE_L 0x02
#define CYD3S1612_REG_CALIB_FLAG 0x05
#define CYD3S1612_REG_CROSSTALK_STATUS 0x06
#define CYD3S1612_REG_OFFSET_STATUS 0x07

#define CYD3S1612_CALIB_DONE 0xC2
#define CYD3S1612_CALIB_START 0xC1
#define CYD3S1612_CROSSTALK_OK 0x00
#define CYD3S1612_CROSSTALK_TOO_HIGH 0x10
#define CYD3S1612_OFFSET_OK 0x00
#define CYD3S1612_OFFSET_TOO_LARGE 0x20

#define CYD3S1612_DEFAULT_I2C_HZ 400000
#define CYD3S1612_POWER_ON_DELAY_MS 1200

enum CYD3S1612MeasurementStatus
{
  CYD3S1612_MEASUREMENT_OK = 0,
  CYD3S1612_MEASUREMENT_ZERO,
  CYD3S1612_MEASUREMENT_INVALID_VALUE,
  CYD3S1612_MEASUREMENT_BELOW_RANGE,
  CYD3S1612_MEASUREMENT_ABOVE_RANGE,
};

struct CYD3S1612Config
{
  uint8_t address = CYD3S1612_DEFAULT_ADDR;
  int sdaPin = SDA;
  int sclPin = SCL;
  uint32_t i2cHz = CYD3S1612_DEFAULT_I2C_HZ;
  uint32_t powerOnDelayMs = CYD3S1612_POWER_ON_DELAY_MS;
  TwoWire *wire = &Wire;
  bool initWire = true;
};

struct CYD3S1612Measurement
{
  uint16_t distanceMm = 0;
  uint8_t rawHigh = 0;
  uint8_t rawLow = 0;
  bool valid = false;
  CYD3S1612MeasurementStatus status = CYD3S1612_MEASUREMENT_INVALID_VALUE;
};

struct CYD3S1612Status
{
  uint8_t version = 0;
  uint8_t calibFlag = 0;
  uint8_t crosstalkStatus = 0;
  uint8_t offsetStatus = 0;
};

class CYD3S1612
{
public:
  CYD3S1612();

  bool begin(const CYD3S1612Config &config = CYD3S1612Config());
  bool begin(uint8_t address, int sdaPin, int sclPin, uint32_t i2cHz = CYD3S1612_DEFAULT_I2C_HZ);
  bool probe();

  bool readRegister(uint8_t reg, uint8_t &value);
  bool readDistance(uint16_t &distanceMm);
  bool read(CYD3S1612Measurement &measurement);
  bool readStatus(CYD3S1612Status &status);
  bool startCalibration();

  bool isOnline() const;
  uint8_t address() const;

  static CYD3S1612MeasurementStatus getMeasurementStatus(uint16_t distanceMm);
  static bool isDistanceValid(uint16_t distanceMm);

private:
  bool readRegisters(uint8_t startReg, uint8_t *buffer, size_t length);
  bool writeRegister(uint8_t reg, const uint8_t *buffer, size_t length);

  CYD3S1612Config m_config;
  bool m_online = false;
};

#endif
