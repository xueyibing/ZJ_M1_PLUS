#include "tmc_config.h"

#include <TMCStepper.h>
#include "gpio.h"
#include "jiandan_data_manager.h"
#include "taiyangdan_data_manager.h"

TMCConfig TMCConfig::instance;

namespace
{
constexpr uint32_t TMC_UART_BAUD = 115200;
constexpr float TMC_R_SENSE = 0.11f;
constexpr uint16_t TMC_DEFAULT_CURRENT_MA = 1500;
constexpr uint16_t TMC_PUSH_CURRENT_MA = 2000;
constexpr uint16_t TMC_CURRENT_MIN_MA = 100;
constexpr uint16_t TMC_CURRENT_MAX_MA = 2000;
constexpr float TMC_DEFAULT_HOLD_MULTIPLIER = 1.0f;
constexpr uint16_t TMC_MOTOR_CURRENT_MA[TMC_MOTOR_COUNT] = {
    /* TMC_MOTOR_PUSH  */ TMC_PUSH_CURRENT_MA,
    /* TMC_MOTOR_TRANS */ TMC_CURRENT_MAX_MA,
    /* TMC_MOTOR_POTB  */ TMC_DEFAULT_CURRENT_MA,
    /* TMC_MOTOR_POTA  */ TMC_DEFAULT_CURRENT_MA,
};
// 每个电机的细分配置（下标与 TmcMotor 一致）。
// 修改这里即可同时改变驱动写入的细分与各电机计算步数使用的细分。
constexpr uint16_t TMC_MOTOR_MICROSTEPS[TMC_MOTOR_COUNT] = {
    /* TMC_MOTOR_PUSH  */ 8,
#if TRANS_USE_DRV8825
    /* TMC_MOTOR_TRANS */ TRANS_USE_DRV8825_MIC,
#else
    /* TMC_MOTOR_TRANS */ 8,
#endif
    /* TMC_MOTOR_POTB  */ 8,
    /* TMC_MOTOR_POTA  */ 8,
};
constexpr uint8_t TMC_CHOPPER_TOFF = 5;
constexpr uint8_t TMC_CHOPPER_HSTRT = 5;
constexpr uint8_t TMC_CHOPPER_HEND = 2;
constexpr uint8_t TMC_CHOPPER_BLANK_TIME = 24;

constexpr uint16_t TMC_MICROSTEP_MIN = 8;
constexpr uint16_t TMC_MICROSTEP_MAX = 256;

bool isValidMicrostep(uint16_t microsteps)
{
  // 有效值为 2 的幂且在 [8,256] 区间内（8/16/32/64/128/256）。
  return microsteps >= TMC_MICROSTEP_MIN &&
         microsteps <= TMC_MICROSTEP_MAX &&
         (microsteps & (microsteps - 1)) == 0;
}

constexpr uint8_t TMC_ADDR_PUSH = 0b00;
constexpr uint8_t TMC_ADDR_TRANS = 0b01;
constexpr uint8_t TMC_ADDR_POTB = 0b10;
constexpr uint8_t TMC_ADDR_POTA = 0b11;

HardwareSerial tmcSerial(1);
TMC2209Stepper pushDriver(&tmcSerial, TMC_R_SENSE, TMC_ADDR_PUSH);
#if !TRANS_USE_DRV8825
TMC2209Stepper transDriver(&tmcSerial, TMC_R_SENSE, TMC_ADDR_TRANS);
#endif
TMC2209Stepper potBDriver(&tmcSerial, TMC_R_SENSE, TMC_ADDR_POTB);
TMC2209Stepper potADriver(&tmcSerial, TMC_R_SENSE, TMC_ADDR_POTA);

bool usesDrv8825(TmcMotor motor)
{
#if TRANS_USE_DRV8825
  return motor == TMC_MOTOR_TRANS;
#else
  (void)motor;
  return false;
#endif
}

void applyTmcPowerConfig(TMC2209Stepper &driver, uint16_t microsteps, uint16_t currentMa)
{
  driver.pdn_disable(true);
  driver.I_scale_analog(false);
  driver.mstep_reg_select(true);
  driver.toff(TMC_CHOPPER_TOFF);
  driver.hstrt(TMC_CHOPPER_HSTRT);
  driver.hend(TMC_CHOPPER_HEND);
  driver.blank_time(TMC_CHOPPER_BLANK_TIME);
  driver.rms_current(currentMa, TMC_DEFAULT_HOLD_MULTIPLIER);
  driver.microsteps(microsteps);
  driver.intpol(true);
  driver.dedge(false);
  driver.en_spreadCycle(false);
  driver.TPWMTHRS(0);
  driver.TCOOLTHRS(0);
  driver.semin(0);
  driver.semax(0);
  driver.seup(0);
  driver.sedn(0);
  driver.seimin(false);
  driver.pwm_autoscale(true);
  driver.pwm_autograd(true);
  driver.freewheel(0);
}

bool configureDriver(const char *name, TMC2209Stepper &driver, uint8_t address, uint16_t microsteps, uint16_t currentMa)
{
  // 上电初期 TMC UART 可能尚未稳定，重试若干次：每次都重新写入寄存器配置，
  // 直到 test_connection 通过且回读的微步与期望一致。避免因单次失败导致
  // 微步/电流未写入芯片，芯片停留在上电默认微步，从而出现"运动只走一点点"的现象。
  constexpr int TMC_INIT_MAX_ATTEMPTS = 8;
  constexpr uint32_t TMC_INIT_RETRY_DELAY_MS = 50;

  driver.begin();

  bool ok = false;
  uint8_t connectionResult = 0;
  uint16_t readbackMicrosteps = 0;
  for (int attempt = 1; attempt <= TMC_INIT_MAX_ATTEMPTS; ++attempt)
  {
    applyTmcPowerConfig(driver, microsteps, currentMa);
    delay(2); // 等待寄存器写入稳定后再回读校验
    connectionResult = driver.test_connection();
    // 回读 CHOPCONF.MRES 校验微步是否真正写入芯片：仅 test_connection 通过
    // 并不代表细分寄存器写入成功（UART 通得上 ≠ 配置已生效）。
    readbackMicrosteps = driver.microsteps();
    const bool connOk = connectionResult == 0;
    const bool microstepOk = readbackMicrosteps == microsteps;
    ok = connOk && microstepOk;
    Serial.printf("TMC2209 %s addr=%u attempt=%d/%d microsteps=%u readback=%u current=%umA uart:%s",
                  name, address, attempt, TMC_INIT_MAX_ATTEMPTS, microsteps, readbackMicrosteps, currentMa,
                  ok ? "ok" : "failed");
    if (!ok)
    {
      Serial.printf(" conn=%u microstep:%s", connectionResult, microstepOk ? "ok" : "mismatch");
    }
    Serial.println();

    if (ok)
    {
      // 清除 GSTAT(reset/drv_err/uv_cp)标志，便于后续检测运行中发生的掉电复位。
      driver.GSTAT(0b111);
      break;
    }
    delay(TMC_INIT_RETRY_DELAY_MS);
  }
  return ok;
}

TMC2209Stepper *driverForMotor(TmcMotor motor)
{
  switch (motor)
  {
  case TMC_MOTOR_PUSH:
    return &pushDriver;
  case TMC_MOTOR_TRANS:
#if !TRANS_USE_DRV8825
    return &transDriver;
#else
    return nullptr;
#endif
  case TMC_MOTOR_POTB:
    return &potBDriver;
  case TMC_MOTOR_POTA:
    return &potADriver;
  default:
    return nullptr;
  }
}

// 检测指定驱动是否发生过掉电复位或充电泵欠压。
// 场景：24V(VM)掉电但 3.3V(VCC_IO)保持时，TMC 内部逻辑/寄存器会复位（微步退回默认），
// 而 ESP32 不复位、m_ready 仍为 true，导致细分错乱。GSTAT.reset 在芯片 POR 后置1，
// uv_cp 表示 VM/充电泵欠压。走 DRV8825 的电机不经 UART，跳过。
bool motorResetDetected(TmcMotor motor)
{
  if (usesDrv8825(motor))
  {
    return false;
  }
  TMC2209Stepper *driver = driverForMotor(motor);
  if (driver == nullptr)
  {
    return false;
  }
  uint8_t gstat = driver->GSTAT();
  // bit0 reset, bit1 drv_err, bit2 uv_cp；reset 或 uv_cp 说明发生过掉电复位/欠压。
  if (gstat & 0b101)
  {
    Serial.printf("TMC2209 motor=%d GSTAT=0x%02X reset/uv detected, need reconfigure\n",
                  static_cast<int>(motor), gstat);
    return true;
  }
  return false;
}

const char *motorName(TmcMotor motor)
{
  switch (motor)
  {
  case TMC_MOTOR_PUSH:
    return "push";
  case TMC_MOTOR_TRANS:
    return "trans";
  case TMC_MOTOR_POTB:
    return "potB";
  case TMC_MOTOR_POTA:
    return "potA";
  default:
    return "?";
  }
}

uint8_t motorAddress(TmcMotor motor)
{
  switch (motor)
  {
  case TMC_MOTOR_PUSH:
    return TMC_ADDR_PUSH;
  case TMC_MOTOR_TRANS:
    return TMC_ADDR_TRANS;
  case TMC_MOTOR_POTB:
    return TMC_ADDR_POTB;
  case TMC_MOTOR_POTA:
    return TMC_ADDR_POTA;
  default:
    return 0;
  }
}

// 按当前配置写入单个 TMC 驱动，返回该电机是否就绪。init 与 ensureReady 复用。
bool configureOneDriver(TmcMotor motor, const uint16_t *microsteps, const uint16_t *currentMa)
{
  if (usesDrv8825(motor))
  {
    // DRV8825 走硬件固定细分、不经 UART，直接视为就绪。
    Serial.printf("DRV8825 %s microsteps=%u (hardware fixed, TMC UART skipped)\n",
                  motorName(motor), microsteps[motor]);
    return true;
  }
  TMC2209Stepper *driver = driverForMotor(motor);
  if (driver == nullptr)
  {
    return false;
  }
  return configureDriver(motorName(motor), *driver, motorAddress(motor),
                         microsteps[motor], currentMa[motor]);
}

// 按当前配置写入全部 TMC 驱动，把各电机就绪结果写入 readyOut。init 与 ensureReady 复用。
void configureAllDrivers(const uint16_t *microsteps, const uint16_t *currentMa, bool *readyOut)
{
  for (int m = 0; m < TMC_MOTOR_COUNT; ++m)
  {
    readyOut[m] = configureOneDriver(static_cast<TmcMotor>(m), microsteps, currentMa);
  }
}
}

TMCConfig::TMCConfig()
{
  for (int i = 0; i < TMC_MOTOR_COUNT; ++i)
  {
    m_currentMa[i] = TMC_MOTOR_CURRENT_MA[i];
    m_microsteps[i] = TMC_MOTOR_MICROSTEPS[i];
    m_ready[i] = false;
  }
}

TMCConfig *TMCConfig::getInstance()
{
  return &instance;
}

void TMCConfig::init()
{
  Serial.printf("TMC2209 UART init rx=%u tx=%u baud=%lu current=%u mA\n",
                TMC_UART_RX_PIN,
                TMC_UART_TX_PIN,
                static_cast<unsigned long>(TMC_UART_BAUD),
                TMC_DEFAULT_CURRENT_MA);

  if (m_mutex == nullptr)
  {
    m_mutex = xSemaphoreCreateMutex();
  }

  tmcSerial.begin(TMC_UART_BAUD, SERIAL_8N1, TMC_UART_RX_PIN, TMC_UART_TX_PIN);

  // 等待 TMC2209 上电稳定后再进行 UART 配置，避免上电初期通信失败
  // 导致微步/电流寄存器未写入芯片。
  delay(200);

  configureAllDrivers(m_microsteps, m_currentMa, m_ready);
  bool allReady = true;
  for (int m = 0; m < TMC_MOTOR_COUNT; ++m)
  {
    allReady &= m_ready[m];
  }
  Serial.printf("TMC2209 UART all drivers:%s\n", allReady ? "ok" : "partial failed");
}

bool TMCConfig::isReady() const
{
  for (int m = 0; m < TMC_MOTOR_COUNT; ++m)
  {
    if (!m_ready[m])
    {
      return false;
    }
  }
  return true;
}

bool TMCConfig::isReady(TmcMotor motor) const
{
  if (motor < 0 || motor >= TMC_MOTOR_COUNT)
  {
    return false;
  }
  return m_ready[motor];
}

bool TMCConfig::ensureReady()
{
  // 逐个电机确保就绪：某一块驱动失败不影响其它电机各自的判断。
  bool allReady = true;
  for (int m = 0; m < TMC_MOTOR_COUNT; ++m)
  {
    allReady &= ensureReady(static_cast<TmcMotor>(m));
  }
  return allReady;
}

bool TMCConfig::ensureReady(TmcMotor motor)
{
  if (motor < 0 || motor >= TMC_MOTOR_COUNT)
  {
    return false;
  }

  // 已就绪时也要检测该驱动是否在运行中发生过掉电复位（24V掉电但3.3V保持时会发生）：
  // 此时寄存器里的微步已丢失退回默认，而 m_ready[motor] 仍为 true。读 GSTAT 发现
  // 复位/欠压就强制重配这一块。
  if (m_ready[motor])
  {
    bool needReconfig = false;
    if (m_mutex != nullptr)
    {
      xSemaphoreTake(m_mutex, portMAX_DELAY);
    }
    needReconfig = motorResetDetected(motor);
    if (needReconfig)
    {
      m_ready[motor] = false;
    }
    if (m_mutex != nullptr)
    {
      xSemaphoreGive(m_mutex);
    }
    if (!needReconfig)
    {
      return true;
    }
  }

  if (m_mutex != nullptr)
  {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
  }

  // 双重检查：可能在等待锁期间已被其它任务重配成功。
  if (!m_ready[motor])
  {
    Serial.printf("TMC2209 %s not ready, re-initializing driver before motion...\n",
                  motorName(motor));
    m_ready[motor] = configureOneDriver(motor, m_microsteps, m_currentMa);
    Serial.printf("TMC2209 re-init %s:%s\n", motorName(motor),
                  m_ready[motor] ? "ok" : "still failed");
  }

  const bool result = m_ready[motor];
  if (m_mutex != nullptr)
  {
    xSemaphoreGive(m_mutex);
  }

  // 重配后仍失败：把电机驱动异常上报给正在运行的 maker，触发停机与 UI 上报链路。
  if (!result)
  {
    if (JiandanDataManager::getInstance()->getStart() &&
        JiandanDataManager::getInstance()->getErrorCode() == ALL_OK)
    {
      JiandanDataManager::getInstance()->setErrorCode(MOTOR_DRIVER_ERROR);
    }
    if (TaiyangdanDataManager::getInstance()->getStart() &&
        TaiyangdanDataManager::getInstance()->getErrorCode() == ALL_OK)
    {
      TaiyangdanDataManager::getInstance()->setErrorCode(MOTOR_DRIVER_ERROR);
    }
  }
  return result;
}

bool TMCConfig::readGstat(TmcMotor motor, uint8_t &status)
{
  status = 0;
  if (motor < 0 || motor >= TMC_MOTOR_COUNT || usesDrv8825(motor))
  {
    return false;
  }

  TMC2209Stepper *driver = driverForMotor(motor);
  if (driver == nullptr)
  {
    return false;
  }

  if (m_mutex != nullptr)
  {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
  }
  status = driver->GSTAT();
  if (m_mutex != nullptr)
  {
    xSemaphoreGive(m_mutex);
  }
  return true;
}

uint16_t TMCConfig::getMicrosteps(TmcMotor motor) const
{
  if (motor < 0 || motor >= TMC_MOTOR_COUNT)
  {
    return m_microsteps[TMC_MOTOR_PUSH];
  }
  return m_microsteps[motor];
}

void TMCConfig::setMicrosteps(TmcMotor motor, uint16_t microsteps)
{
  if (motor < 0 || motor >= TMC_MOTOR_COUNT)
  {
    return;
  }
  if (!isValidMicrostep(microsteps))
  {
    Serial.printf("TMC2209 set microsteps motor=%d invalid value %u (need 8/16/32/64/128/256)\n",
                  static_cast<int>(motor), microsteps);
    return;
  }

  if (usesDrv8825(motor))
  {
    Serial.printf("DRV8825 trans microsteps are hardware fixed at %u; request %u ignored\n",
                  TRANS_USE_DRV8825_MIC, microsteps);
    return;
  }

  m_microsteps[motor] = microsteps;

  TMC2209Stepper *driver = driverForMotor(motor);
  if (driver != nullptr)
  {
    driver->microsteps(microsteps);
  }
  Serial.printf("TMC2209 set microsteps motor=%d -> %u\n", static_cast<int>(motor), microsteps);
}

void TMCConfig::setCurrent(TmcMotor motor, uint16_t currentMa)
{
  if (motor < 0 || motor >= TMC_MOTOR_COUNT)
  {
    return;
  }
  if (currentMa < TMC_CURRENT_MIN_MA)
  {
    currentMa = TMC_CURRENT_MIN_MA;
  }
  if (currentMa > TMC_CURRENT_MAX_MA)
  {
    currentMa = TMC_CURRENT_MAX_MA;
  }

  if (usesDrv8825(motor))
  {
    Serial.printf("DRV8825 trans current is set by the driver potentiometer; UART request %u mA ignored\n",
                  currentMa);
    return;
  }

  m_currentMa[motor] = currentMa;

  TMC2209Stepper *driver = driverForMotor(motor);
  if (driver != nullptr)
  {
    driver->rms_current(currentMa, TMC_DEFAULT_HOLD_MULTIPLIER);
  }
  Serial.printf("TMC2209 set current motor=%d -> %u mA\n", static_cast<int>(motor), currentMa);
}

uint16_t TMCConfig::getCurrent(TmcMotor motor) const
{
  if (motor < 0 || motor >= TMC_MOTOR_COUNT)
  {
    return TMC_DEFAULT_CURRENT_MA;
  }
  return m_currentMa[motor];
}
