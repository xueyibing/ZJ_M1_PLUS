#include <FastAccelStepper.h>

#include "cyd_tof.h"
#include "debug_run_control.h"
#include "egg_control.h"
#include "environment_temp.h"
#include "error_log_data_manager.h"
#include "fan.h"
#include "gpio.h"
#include "jiandan_data_manager.h"
#include "jiandan_maker.h"
#include "oil_control.h"
#include "oil_servo.h"
#include "common_data_manager.h"
#include "pot_a_motor.h"
#include "pot_b_motor.h"
#include "pot_i2c.h"
#include "push_motor.h"
#include "taiyangdan_maker.h"
#include "taiyangdan_data_manager.h"
#include "tmc_config.h"
#include "trans_motor.h"
#include "uart_connection.h"

PushMotor *pushMotor = PushMotor::getInstance();
TransMotor *transMotor = TransMotor::getInstance();
PotAMotor *potAMotor = PotAMotor::getInstance();
PotBMotor *potBMotor = PotBMotor::getInstance();
OilControl *Oil = OilControl::getInstance();
OilServo *oilServo = OilServo::getInstance();
Fan *fan = Fan::getInstance();
EggControl *eggControl = EggControl::getInstance();
PotI2c *potI2c = PotI2c::getInstance();
UartConnection *uart = UartConnection::getInstance();
JiandanDataManager *jiandanData = JiandanDataManager::getInstance();
TaiyangdanDataManager *taiyangdanData = TaiyangdanDataManager::getInstance();
CommonDataManager *commonData = CommonDataManager::getInstance();
ErrorLogDataManager *errorLog = ErrorLogDataManager::getInstance();
FastAccelStepperEngine *stepperEngine;
CydTof *cydTof = CydTof::getInstance();
EnvironmentTemp *envTemp = EnvironmentTemp::getInstance();
TMCConfig *tmcConfig = TMCConfig::getInstance();

static void initializeHardware();

void setup()
{
  Serial.begin(115200);
  Serial.printf("s------t------a------r------t\n\n");
  initializeHardware();
  xTaskCreatePinnedToCore(JiandanMaker::makerControlTask, "JiandanMakerControl", 1024 * 4, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(TaiyangdanMaker::makerControlTask, "TaiyangdanMakerControl", 1024 * 4, NULL, 2, NULL, 1);
  Serial.printf("i---n---i---t---e---d\n\n");
}

static void initializeHardware()
{
  stepperEngine = new FastAccelStepperEngine();

  uart->init();
  jiandanData->init();
  taiyangdanData->init();
  commonData->init();
  errorLog->init();
  DebugRunControl::getInstance()->init();
  tmcConfig->init();
  stepperEngine->init();
  pushMotor->init(stepperEngine);
  transMotor->init(stepperEngine);
  potAMotor->init(stepperEngine);
  potBMotor->init(stepperEngine);
  Oil->init();
  oilServo->init();
  eggControl->init();
  fan->init();
  potI2c->init();
  cydTof->init();
  envTemp->init();
}

void loop()
{
  potI2c->update();
  uart->update();
  envTemp->update();
  delay(10);
}
