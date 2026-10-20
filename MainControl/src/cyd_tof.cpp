#include "cyd_tof.h"

#include <Arduino.h>
#include <Wire.h>

namespace
{
constexpr int CYD_INIT_RETRY_COUNT = 5;
constexpr int CYD_SAMPLE_COUNT = 5;
// Use a conservative rate while validating the vendor-required repeated START.
// The module datasheet lists 500 kHz as typical and permits lower rates.
constexpr uint32_t CYD_I2C_HZ = 100000;

// 蛋托测距使用独立的第二路 I2C（Wire1），与锅具 I2C 的总线 0 互不干扰。
TwoWire &cydI2C = Wire1;

void recoverI2CBus()
{
  pinMode(CYD_TOF_SCL_PIN, OUTPUT);
  pinMode(CYD_TOF_SDA_PIN, INPUT_PULLUP);
  if (digitalRead(CYD_TOF_SDA_PIN) != LOW)
  {
    return;
  }

  Serial.printf("CYD3S1612(Have) I2C bus locked, recovering SDA=%d SCL=%d\n", CYD_TOF_SDA_PIN, CYD_TOF_SCL_PIN);
  for (int i = 0; i < 9; i++)
  {
    digitalWrite(CYD_TOF_SCL_PIN, HIGH);
    delayMicroseconds(5);
    digitalWrite(CYD_TOF_SCL_PIN, LOW);
    delayMicroseconds(5);
    if (digitalRead(CYD_TOF_SDA_PIN) == HIGH)
    {
      break;
    }
  }

  pinMode(CYD_TOF_SDA_PIN, OUTPUT);
  digitalWrite(CYD_TOF_SDA_PIN, LOW);
  delayMicroseconds(5);
  digitalWrite(CYD_TOF_SCL_PIN, HIGH);
  delayMicroseconds(5);
  digitalWrite(CYD_TOF_SDA_PIN, HIGH);
  delayMicroseconds(5);
  pinMode(CYD_TOF_SDA_PIN, INPUT_PULLUP);
  pinMode(CYD_TOF_SCL_PIN, INPUT_PULLUP);
  Serial.printf("CYD3S1612(Have) I2C bus recovery finished, SDA=%d\n", digitalRead(CYD_TOF_SDA_PIN));
}

const char *measurementStatusName(CYD3S1612MeasurementStatus status)
{
  switch (status)
  {
  case CYD3S1612_MEASUREMENT_OK:
    return "ok";
  case CYD3S1612_MEASUREMENT_ZERO:
    return "zero";
  case CYD3S1612_MEASUREMENT_INVALID_VALUE:
    return "invalid";
  case CYD3S1612_MEASUREMENT_BELOW_RANGE:
    return "below_range";
  case CYD3S1612_MEASUREMENT_ABOVE_RANGE:
    return "above_range";
  default:
    return "unknown";
  }
}
}

CydTof CydTof::instance;

CydTof::CydTof()
{
}

CydTof *CydTof::getInstance()
{
  return &instance;
}

void CydTof::init()
{
  initHave();
}

void CydTof::initHave()
{
  CYD3S1612Config config;
  config.address = CYD3S1612_DEFAULT_ADDR;
  config.sdaPin = CYD_TOF_SDA_PIN;
  config.sclPin = CYD_TOF_SCL_PIN;
  config.i2cHz = CYD_I2C_HZ;
  config.powerOnDelayMs = CYD3S1612_POWER_ON_DELAY_MS;
  config.wire = &cydI2C;
  config.initWire = false; // 在此手动 end()+begin() 确保 Wire1 干净初始化

  Serial.printf("CYD3S1612(Have) init SDA=%d SCL=%d addr=0x%02X speed=%luHz\n",
                config.sdaPin,
                config.sclPin,
                config.address,
                static_cast<unsigned long>(config.i2cHz));

  m_haveReady = false;

  // 先 end() 再 begin()：arduino-esp32 的 begin() 对已初始化总线不会重新配置引脚，
  // 必须先反初始化才能保证引脚与速率真正生效。
  cydI2C.end();
  recoverI2CBus();
  cydI2C.begin(CYD_TOF_SDA_PIN, CYD_TOF_SCL_PIN, CYD_I2C_HZ);
  cydI2C.setTimeOut(2000);

  for (int c = 0; c < CYD_INIT_RETRY_COUNT; c++)
  {
    if (m_haveSensor.begin(config))
    {
      m_haveReady = true;
      break;
    }
    Serial.printf("CYD3S1612(Have) init failed, retry=%d/%d\n", c + 1, CYD_INIT_RETRY_COUNT);
    vTaskDelay(100);
  }

  if (!m_haveReady)
  {
    Serial.println("CYD3S1612(Have) init failed too many times");
    return;
  }

  CYD3S1612Status status;
  if (m_haveSensor.readStatus(status))
  {
    Serial.printf("CYD3S1612(Have) status version=0x%02X calib=0x%02X crosstalk=0x%02X offset=0x%02X\n",
                  status.version,
                  status.calibFlag,
                  status.crosstalkStatus,
                  status.offsetStatus);
  }
  else
  {
    Serial.println("CYD3S1612(Have) status read failed");
  }

  CYD3S1612Measurement startupMeasurement;
  if (m_haveSensor.read(startupMeasurement))
  {
    Serial.printf("CYD3S1612(Have) startup raw=%02X %02X distance=%u status=%s valid=%d\n",
                  startupMeasurement.rawHigh,
                  startupMeasurement.rawLow,
                  startupMeasurement.distanceMm,
                  measurementStatusName(startupMeasurement.status),
                  startupMeasurement.valid ? 1 : 0);
  }
  else
  {
    Serial.println("CYD3S1612(Have) startup measurement read failed");
  }
}

CydTofCheckState CydTof::checkSensor()
{
  int haveDistance = getHaveDistance();
  if (haveDistance < 0)
  {
    Serial.println("CYD3S1612(Have) sensor self-check failed");
    return CydTofCheckState::SENSOR_ERROR;
  }
  if (haveDistance < 30)
  {
    Serial.printf("CYD3S1612(Have) lens may be blocked, distance=%d\n", haveDistance);
    return CydTofCheckState::BLOCKED;
  }

  return CydTofCheckState::OK;
}

int CydTof::getHaveDistance(bool verbose)
{
  if (!m_haveReady)
  {
    initHave();
    if (!m_haveReady)
    {
      Serial.println("CYD3S1612(Have) init failed too many times, returning -1");
      return -1;
    }
  }

  int samples[CYD_SAMPLE_COUNT];
  int validCount = 0;
  int apiErrorCount = 0;
  // 记录成功读到但超程(>量程上限)的最大距离：这类多为“打到5m外/空旷=无目标”，
  // 用于在无有效样本时把“空托超程”与“真读失败”区分开。
  int maxAboveRangeDistance = -1;
  CYD3S1612MeasurementStatus lastStatus = CYD3S1612_MEASUREMENT_INVALID_VALUE;
  uint16_t lastDistance = 0;

  // 记录每次采样明细，便于在无有效样本时区分“恒 0 遮挡”与“偶发 0”。
  bool sampleApiOk[CYD_SAMPLE_COUNT];
  uint16_t sampleDistance[CYD_SAMPLE_COUNT];
  uint8_t sampleRawHigh[CYD_SAMPLE_COUNT];
  uint8_t sampleRawLow[CYD_SAMPLE_COUNT];
  CYD3S1612MeasurementStatus sampleStatus[CYD_SAMPLE_COUNT];

  if (verbose)
  {
    CYD3S1612Status status;
    if (m_haveSensor.readStatus(status))
    {
      Serial.printf("CYD3S1612(Have) debug status version=0x%02X calib=0x%02X crosstalk=0x%02X offset=0x%02X\n",
                    status.version,
                    status.calibFlag,
                    status.crosstalkStatus,
                    status.offsetStatus);
    }
    else
    {
      Serial.println("CYD3S1612(Have) debug status read failed");
    }
  }

  for (int i = 0; i < CYD_SAMPLE_COUNT; i++)
  {
    CYD3S1612Measurement measurement;
    if (!m_haveSensor.read(measurement))
    {
      apiErrorCount++;
      sampleApiOk[i] = false;
      sampleDistance[i] = 0;
      sampleRawHigh[i] = 0;
      sampleRawLow[i] = 0;
      sampleStatus[i] = CYD3S1612_MEASUREMENT_INVALID_VALUE;
    }
    else
    {
      lastStatus = measurement.status;
      lastDistance = measurement.distanceMm;
      sampleApiOk[i] = true;
      sampleDistance[i] = measurement.distanceMm;
      sampleRawHigh[i] = measurement.rawHigh;
      sampleRawLow[i] = measurement.rawLow;
      sampleStatus[i] = measurement.status;
      if (measurement.valid)
      {
        samples[validCount++] = measurement.distanceMm;
      }
      else if (measurement.status == CYD3S1612_MEASUREMENT_ABOVE_RANGE &&
               static_cast<int>(measurement.distanceMm) > maxAboveRangeDistance)
      {
        // 超程读数：稳定测到很远说明托盘上没有目标(空托)，保留最大值上报。
        maxAboveRangeDistance = static_cast<int>(measurement.distanceMm);
      }
    }
    if (verbose)
    {
      if (sampleApiOk[i])
      {
        Serial.printf("CYD3S1612(Have) debug sample[%d] raw=%02X %02X distance=%u status=%s valid=%d\n",
                      i,
                      sampleRawHigh[i],
                      sampleRawLow[i],
                      sampleDistance[i],
                      measurementStatusName(sampleStatus[i]),
                      sampleStatus[i] == CYD3S1612_MEASUREMENT_OK ? 1 : 0);
      }
      else
      {
        Serial.printf("CYD3S1612(Have) debug sample[%d] read failed\n", i);
      }
    }
    vTaskDelay(10);
  }

  if (validCount == 0)
  {
    if (apiErrorCount == CYD_SAMPLE_COUNT)
    {
      m_haveReady = false;
      return -1;
    }

    // 打印本轮全部采样明细，判断是恒 0 还是偶发 0。
    for (int i = 0; i < CYD_SAMPLE_COUNT; i++)
    {
      if (sampleApiOk[i])
      {
        Serial.printf("CYD3S1612(Have) sample[%d] dist=%u status=%s\n",
                      i,
                      sampleDistance[i],
                      measurementStatusName(sampleStatus[i]));
      }
      else
      {
        Serial.printf("CYD3S1612(Have) sample[%d] read failed\n", i);
      }
    }

    // 读取模块标定/串扰/偏移状态，辅助定位未标定或光学串扰问题。
    CYD3S1612Status diag;
    if (m_haveSensor.readStatus(diag))
    {
      Serial.printf("CYD3S1612(Have) diag version=0x%02X calib=0x%02X crosstalk=0x%02X offset=0x%02X\n",
                    diag.version,
                    diag.calibFlag,
                    diag.crosstalkStatus,
                    diag.offsetStatus);
    }
    else
    {
      Serial.println("CYD3S1612(Have) diag status read failed");
    }

    Serial.printf("CYD3S1612(Have) no valid sample, last=%u status=%s apiError=%d/%d\n",
                  lastDistance,
                  measurementStatusName(lastStatus),
                  apiErrorCount,
                  CYD_SAMPLE_COUNT);

    // 读得到但全部超程(打到5m外/空旷)时，视为“无目标/空托”，直接返回该大距离，
    // 让上层按“>100mm 无蛋”处理，避免把正常空托误报成传感器异常。
    if (maxAboveRangeDistance > 0)
    {
      Serial.printf("CYD3S1612(Have) all samples above range, treat as no-target dist=%d\n",
                    maxAboveRangeDistance);
      return maxAboveRangeDistance;
    }

    // The last raw value is useful for diagnostics, but it is not a valid
    // measurement. Returning it would turn a failed read (often 0 mm) into a
    // false "lens blocked" or "no egg" result in the making flow.
    return -1;
  }

  for (int i = 0; i < validCount - 1; i++)
  {
    for (int j = 0; j < validCount - i - 1; j++)
    {
      if (samples[j] > samples[j + 1])
      {
        int tmp = samples[j];
        samples[j] = samples[j + 1];
        samples[j + 1] = tmp;
      }
    }
  }

  const int result = samples[validCount / 2];
  if (verbose)
  {
    Serial.printf("CYD3S1612(Have) debug summary valid=%d/%d api_error=%d result=%d\n",
                  validCount,
                  CYD_SAMPLE_COUNT,
                  apiErrorCount,
                  result);
  }
  return result;
}
