#include "cyd3s1612.h"

CYD3S1612::CYD3S1612()
{
}

bool CYD3S1612::begin(const CYD3S1612Config &config)
{
  m_config = config;
  m_online = false;

  if (m_config.address == 0)
  {
    m_config.address = CYD3S1612_DEFAULT_ADDR;
  }

  if (m_config.i2cHz == 0)
  {
    m_config.i2cHz = CYD3S1612_DEFAULT_I2C_HZ;
  }

  if (m_config.powerOnDelayMs == 0)
  {
    m_config.powerOnDelayMs = CYD3S1612_POWER_ON_DELAY_MS;
  }

  if (m_config.wire == nullptr)
  {
    m_config.wire = &Wire;
  }

  if (m_config.initWire)
  {
    m_config.wire->begin(m_config.sdaPin, m_config.sclPin);
  }
  m_config.wire->setClock(m_config.i2cHz);
  m_config.wire->setTimeOut(2000);

  delay(m_config.powerOnDelayMs);
  return probe();
}

bool CYD3S1612::begin(uint8_t address, int sdaPin, int sclPin, uint32_t i2cHz)
{
  CYD3S1612Config config;
  config.address = address;
  config.sdaPin = sdaPin;
  config.sclPin = sclPin;
  config.i2cHz = i2cHz;
  return begin(config);
}

bool CYD3S1612::probe()
{
  uint8_t frame[3] = {0, 0, 0};
  if (!readRegisters(CYD3S1612_REG_VERSION, frame, sizeof(frame)))
  {
    return false;
  }

  // Reject an ACK-only or floating bus response as an online sensor.
  return frame[0] != 0x00 && frame[0] != 0xFF;
}

bool CYD3S1612::readRegister(uint8_t reg, uint8_t &value)
{
  return readRegisters(reg, &value, 1);
}

bool CYD3S1612::readDistance(uint16_t &distanceMm)
{
  uint8_t frame[3] = {0, 0, 0};

  // Match the vendor example: read version, distance high and distance low
  // sequentially from register 0x00.
  if (!readRegisters(CYD3S1612_REG_VERSION, frame, sizeof(frame)))
  {
    return false;
  }

  if (frame[0] == 0x00 || frame[0] == 0xFF)
  {
    m_online = false;
    return false;
  }

  distanceMm = (static_cast<uint16_t>(frame[1]) << 8) | frame[2];
  return true;
}

bool CYD3S1612::read(CYD3S1612Measurement &measurement)
{
  uint8_t frame[3] = {0, 0, 0};

  measurement.distanceMm = 0;
  measurement.rawHigh = 0;
  measurement.rawLow = 0;
  measurement.valid = false;
  measurement.status = CYD3S1612_MEASUREMENT_INVALID_VALUE;

  if (!readRegisters(CYD3S1612_REG_VERSION, frame, sizeof(frame)))
  {
    return false;
  }

  if (frame[0] == 0x00 || frame[0] == 0xFF)
  {
    m_online = false;
    return false;
  }

  measurement.rawHigh = frame[1];
  measurement.rawLow = frame[2];
  measurement.distanceMm = (static_cast<uint16_t>(frame[1]) << 8) | frame[2];
  measurement.status = getMeasurementStatus(measurement.distanceMm);
  measurement.valid = measurement.status == CYD3S1612_MEASUREMENT_OK;
  return true;
}

bool CYD3S1612::readStatus(CYD3S1612Status &status)
{
  uint8_t registers[CYD3S1612_REG_OFFSET_STATUS + 1] = {0};
  if (!readRegisters(CYD3S1612_REG_VERSION, registers, sizeof(registers)))
  {
    return false;
  }

  status.version = registers[CYD3S1612_REG_VERSION];
  status.calibFlag = registers[CYD3S1612_REG_CALIB_FLAG];
  status.crosstalkStatus = registers[CYD3S1612_REG_CROSSTALK_STATUS];
  status.offsetStatus = registers[CYD3S1612_REG_OFFSET_STATUS];
  return status.version != 0x00 && status.version != 0xFF;
}

bool CYD3S1612::startCalibration()
{
  const uint8_t command = CYD3S1612_CALIB_START;
  return writeRegister(CYD3S1612_REG_CALIB_FLAG, &command, 1);
}

bool CYD3S1612::isOnline() const
{
  return m_online;
}

uint8_t CYD3S1612::address() const
{
  return m_config.address;
}

CYD3S1612MeasurementStatus CYD3S1612::getMeasurementStatus(uint16_t distanceMm)
{
  if (distanceMm == 0)
  {
    return CYD3S1612_MEASUREMENT_ZERO;
  }

  if (distanceMm == CYD3S1612_DISTANCE_INVALID)
  {
    return CYD3S1612_MEASUREMENT_INVALID_VALUE;
  }

  if (distanceMm < CYD3S1612_DISTANCE_MIN_MM)
  {
    return CYD3S1612_MEASUREMENT_BELOW_RANGE;
  }

  if (distanceMm > CYD3S1612_DISTANCE_MAX_MM)
  {
    return CYD3S1612_MEASUREMENT_ABOVE_RANGE;
  }

  return CYD3S1612_MEASUREMENT_OK;
}

bool CYD3S1612::isDistanceValid(uint16_t distanceMm)
{
  return getMeasurementStatus(distanceMm) == CYD3S1612_MEASUREMENT_OK;
}

bool CYD3S1612::readRegisters(uint8_t startReg, uint8_t *buffer, size_t length)
{
  if (buffer == nullptr || length == 0 || length > 255 || m_config.wire == nullptr)
  {
    return false;
  }

  m_config.wire->beginTransmission(m_config.address);
  m_config.wire->write(startReg);

  // The vendor transaction uses a repeated START after selecting the start
  // register. Sending STOP here can leave the module on stale/default data.
  uint8_t writeResult = m_config.wire->endTransmission(false);
  if (writeResult != 0)
  {
    Serial.printf("CYD3S1612 I2C register select failed reg=0x%02X error=%u\n",
                  startReg,
                  writeResult);
    m_online = false;
    return false;
  }

  uint8_t readCount = m_config.wire->requestFrom(static_cast<int>(m_config.address),
                                                 static_cast<int>(length),
                                                 static_cast<int>(true));
  if (readCount != length)
  {
    Serial.printf("CYD3S1612 I2C short read reg=0x%02X expected=%u actual=%u\n",
                  startReg,
                  static_cast<unsigned int>(length),
                  readCount);
    while (m_config.wire->available())
    {
      m_config.wire->read();
    }
    m_online = false;
    return false;
  }

  for (size_t index = 0; index < length; ++index)
  {
    buffer[index] = static_cast<uint8_t>(m_config.wire->read());
  }

  m_online = true;
  return true;
}

bool CYD3S1612::writeRegister(uint8_t reg, const uint8_t *buffer, size_t length)
{
  if ((buffer == nullptr && length > 0) || length > 255 || m_config.wire == nullptr)
  {
    return false;
  }

  m_config.wire->beginTransmission(m_config.address);
  m_config.wire->write(reg);
  for (size_t index = 0; index < length; ++index)
  {
    m_config.wire->write(buffer[index]);
  }

  uint8_t writeResult = m_config.wire->endTransmission(true);
  if (writeResult != 0)
  {
    m_online = false;
    return false;
  }

  m_online = true;
  return true;
}
