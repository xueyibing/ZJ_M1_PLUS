#include "pot_i2c.h"
#include "gpio.h"
#include "ntc_ad_temp_map.h"
#include "debug_run_control.h"
#include "emergency_stop_control.h"
#include <Arduino.h>
#include <time.h>

#define POTA_ADDR 0x5c
#define POTB_ADDR 0x54

int PotI2c::potA_temp_ad = 0;
int PotI2c::potA_temp = -100;
int PotI2c::potA_igbt = 0;
int PotI2c::potA_igbt_temp = -100;
int PotI2c::potA_vol = 0;
int PotI2c::potA_cur = 0;
int PotI2c::potA_ppg = 0;
int PotI2c::potA_state = 0;
int PotI2c::potA_dw = 0;
unsigned long PotI2c::potA_end = 0;
POT_STATE PotI2c::POTA_S_CODE = POT_OK;

int PotI2c::potB_temp_ad = 0;
int PotI2c::potB_temp = -100;
int PotI2c::potB_igbt = 0;
int PotI2c::potB_igbt_temp = -100;
int PotI2c::potB_vol = 0;
int PotI2c::potB_cur = 0;
int PotI2c::potB_ppg = 0;
int PotI2c::potB_state = 0;
int PotI2c::potB_dw = 0;
unsigned long PotI2c::potB_end = 0;
POT_STATE PotI2c::POTB_S_CODE = POT_OK;

PotI2c PotI2c::instance;
PotI2c::PotI2c()
{
}

PotI2c *PotI2c::getInstance()
{
    return &instance;
}

void PotI2c::init()
{
    myWire = &Wire;
    myWire->begin(POT_I2C_SDA, POT_I2C_SCL);
    myWire->setTimeOut(2000);

    // 创建总线/状态互斥量
    busMutex = xSemaphoreCreateMutex();
    stateMutex = xSemaphoreCreateMutex();

    // 启用定时器
    timer = new Ticker(taskToExecute, 1000, 0, MILLIS);
    timer->start();
}

bool PotI2c::checkPot()
{
    // 第一步：检测IIC总线连通性，发送零档位命令并尝试读取
    {
        int tmp, igbt, vol, cur, ppg, state;
        lockBus();
        bool sendOk = sendData(POTA_ADDR, 0, false);
        bool readOk = sendOk ? readData(POTA_ADDR, &tmp, &igbt, &vol, &cur, &ppg, &state) : false;
        unlockBus();
        if (!sendOk || !readOk)
        {
            POTA_S_CODE = IIC_CONN_ERROR;
            Serial.printf("checkPot: A锅IIC通信异常, send:%d read:%d\n", sendOk, readOk);
            return false;
        }
    }
    {
        int tmp, igbt, vol, cur, ppg, state;
        lockBus();
        bool sendOk = sendData(POTB_ADDR, 0, false);
        bool readOk = sendOk ? readData(POTB_ADDR, &tmp, &igbt, &vol, &cur, &ppg, &state) : false;
        unlockBus();
        if (!sendOk || !readOk)
        {
            POTB_S_CODE = IIC_CONN_ERROR;
            Serial.printf("checkPot: B锅IIC通信异常, send:%d read:%d\n", sendOk, readOk);
            return false;
        }
    }

    // 第二步：施加低档位，等待锅具上报自检状态（无锅/无线圈）
    setPotADW(4, 5 * 1000);
    setPotBDW(4, 5 * 1000);

    int c = 30;
    while (getPOTA_S_CODE() == POT_OK && getPOTB_S_CODE() == POT_OK && c-- > 0)
    {
        Serial.printf("等待锅连接, POTA_S_CODE:%d, POTB_S_CODE:%d\n", getPOTA_S_CODE(), getPOTB_S_CODE());
        vTaskDelay(100);
    }

    if (getPOTA_S_CODE() != POT_OK || getPOTB_S_CODE() != POT_OK)
    {
        Serial.printf("锅具异常，关闭加热, POTA_S_CODE:%d, POTB_S_CODE:%d\n", getPOTA_S_CODE(), getPOTB_S_CODE());
        setPotADW(0, 0);
        setPotBDW(0, 0);
    }

    // 第三步：逐一判断错误类型，IIC_CONN_ERROR 优先于业务错误
    if (getPOTA_S_CODE() == IIC_CONN_ERROR)
    {
        Serial.printf("checkPot: A锅IIC通信中途断开\n");
        return false;
    }
    if (getPOTB_S_CODE() == IIC_CONN_ERROR)
    {
        Serial.printf("checkPot: B锅IIC通信中途断开\n");
        return false;
    }
    if (getPOTA_S_CODE() == POT_NO)
    {
        Serial.printf("checkPot: A锅未放置, POTA_S_CODE:%d\n", getPOTA_S_CODE());
        return false;
    }
    if (getPOTB_S_CODE() == POT_NO)
    {
        Serial.printf("checkPot: B锅未放置, POTB_S_CODE:%d\n", getPOTB_S_CODE());
        return false;
    }
    if (getPOTA_S_CODE() == POT_NO_COIL)
    {
        Serial.printf("checkPot: A锅线圈未连接, POTA_S_CODE:%d\n", getPOTA_S_CODE());
        return false;
    }
    if (getPOTB_S_CODE() == POT_NO_COIL)
    {
        Serial.printf("checkPot: B锅线圈未连接, POTB_S_CODE:%d\n", getPOTB_S_CODE());
        return false;
    }

    return true;
}

bool PotI2c::checkPot(bool checkPotA, bool checkPotB)
{
    if (!checkPotA && !checkPotB)
    {
        return true;
    }

    int tmp, igbt, vol, cur, ppg, state;
    if (checkPotA)
    {
        lockBus();
        bool sendOk = sendData(POTA_ADDR, 0, false);
        bool readOk = sendOk ? readData(POTA_ADDR, &tmp, &igbt, &vol, &cur, &ppg, &state) : false;
        unlockBus();
        if (!sendOk || !readOk)
        {
            POTA_S_CODE = IIC_CONN_ERROR;
            Serial.printf("checkPot: A IIC communication failed, send:%d read:%d\n", sendOk, readOk);
            return false;
        }
        POTA_S_CODE = POT_OK;
        setPotADW(4, 5 * 1000);
    }

    if (checkPotB)
    {
        lockBus();
        bool sendOk = sendData(POTB_ADDR, 0, false);
        bool readOk = sendOk ? readData(POTB_ADDR, &tmp, &igbt, &vol, &cur, &ppg, &state) : false;
        unlockBus();
        if (!sendOk || !readOk)
        {
            POTB_S_CODE = IIC_CONN_ERROR;
            Serial.printf("checkPot: B IIC communication failed, send:%d read:%d\n", sendOk, readOk);
            if (checkPotA)
            {
                setPotADW(0, 0);
            }
            return false;
        }
        POTB_S_CODE = POT_OK;
        setPotBDW(4, 5 * 1000);
    }

    int waitCount = 30;
    while ((!(checkPotA && getPOTA_S_CODE() != POT_OK)) &&
           (!(checkPotB && getPOTB_S_CODE() != POT_OK)) &&
           waitCount-- > 0)
    {
        vTaskDelay(100);
    }

    const bool potAOk = !checkPotA || getPOTA_S_CODE() == POT_OK;
    const bool potBOk = !checkPotB || getPOTB_S_CODE() == POT_OK;
    if (!potAOk || !potBOk)
    {
        if (checkPotA)
        {
            setPotADW(0, 0);
        }
        if (checkPotB)
        {
            setPotBDW(0, 0);
        }
        return false;
    }

    return true;
}

void PotI2c::setPotADW(int dw, int milliseconds)
{
    if (dw > 0 && EmergencyStopControl::getInstance()->isRequested())
    {
        dw = 0;
        milliseconds = 0;
    }
    if (dw > 0 && DebugRunControl::getInstance()->isHeatingDisabled())
    {
        dw = 0;
        milliseconds = 0;
    }

    if (dw > 0 &&
        DebugRunControl::getInstance()->isBypassEnabled() &&
        !DebugRunControl::getInstance()->canHeatPotA())
    {
        Serial.printf("[DebugRun] A pot heating blocked, state=%d temp=%d\n",
                      (int)getPOTA_S_CODE(), getTempA());
        dw = 0;
        milliseconds = 0;
    }

    if (milliseconds > 0 && dw > 0)
    {
        lockState();
        potA_dw = clampDw(dw);
        potA_end = millis() + milliseconds;
        unlockState();
    }
    else
    {
        lockState();
        potA_dw = 0;
        potA_end = 0;
        unlockState();
    }
}

void PotI2c::setPotBDW(int dw, int milliseconds)
{
    if (dw > 0 && EmergencyStopControl::getInstance()->isRequested())
    {
        dw = 0;
        milliseconds = 0;
    }
    if (dw > 0 && DebugRunControl::getInstance()->isHeatingDisabled())
    {
        dw = 0;
        milliseconds = 0;
    }

    if (dw > 0 &&
        DebugRunControl::getInstance()->isBypassEnabled() &&
        !DebugRunControl::getInstance()->canHeatPotB())
    {
        Serial.printf("[DebugRun] B pot heating blocked, state=%d temp=%d\n",
                      (int)getPOTB_S_CODE(), getTempB());
        dw = 0;
        milliseconds = 0;
    }

    if (milliseconds > 0 && dw > 0)
    {
        lockState();
        potB_dw = clampDw(dw);
        potB_end = millis() + milliseconds;
        unlockState();
    }
    else
    {
        lockState();
        potB_dw = 0;
        potB_end = 0;
        unlockState();
    }
}

void PotI2c::getPotAInfo(int &addr, int &tempAd, int &igbtAd, int &vol, int &cur, int &ppg, int &state)
{
    addr = POTA_ADDR;
    tempAd = potA_temp_ad;
    igbtAd = potA_igbt;
    vol = potA_vol;
    cur = potA_cur;
    ppg = potA_ppg;
    state = potA_state;
}

void PotI2c::getPotBInfo(int &addr, int &tempAd, int &igbtAd, int &vol, int &cur, int &ppg, int &state)
{
    addr = POTB_ADDR;
    tempAd = potB_temp_ad;
    igbtAd = potB_igbt;
    vol = potB_vol;
    cur = potB_cur;
    ppg = potB_ppg;
    state = potB_state;
}

bool PotI2c::sendData(int addr, int dw, bool work)
{
    // 定义要发送的7个字节数据
    // 0.DW 档位
    u_int8_t outDw = work ? (uint8_t)clampDw(dw) : 0x00;
    // 1.电流参数，按档位表同步发送
    u_int8_t cur = work ? dwToCur(outDw) : 0x00;
    // 2.电压，0x7A对应220V
    u_int8_t vol = 0x7A;
    // 3.buz 蜂鸣器
    u_int8_t buz = 0x00;
    // 4.控制标志位
    //    Bit.3=待机/关机，暂时没用，对方不处理
    //    Bit.2=风扇开/关，单独控制
    //    Bit.1=加热/暂停 和bit.0同步，都设置1为加热
    //    Bit.0=工作/停止
    u_int8_t flag = (work == true) ? 0b00001011 : 0b00001000;

    // 5. 预留
    u_int8_t res1 = 0x00;
    // 6. 预留
    u_int8_t res2 = 0x00;
    uint8_t dataToSend[8] = {0x00, outDw, cur, vol, buz, flag, res1, res2};
    // Serial.printf("发送数据: 档位:%x, 电流:%x, 电压:%x, 蜂鸣器:%x, 标志位:%x\n", outDw, cur, vol, buz, flag);

    myWire->beginTransmission(addr); // 启动传输到从设备0x54
    // 发送8个字节数据
    myWire->write(dataToSend, 8); // 第二个参数是要发送的字节数

    byte error = myWire->endTransmission(); // 结束传输并检查错误
    if (error == 0)
    {
        // Serial.println("7 bytes sent successfully");
    }
    else
    {
        Serial.print("Error code: ");
        Serial.println(error);
        return false;
    }

    return true;
}

bool PotI2c::readData(int addr, int *temp, int *igbt, int *vol, int *cur, int *ppg, int *state)
{
    uint8_t buf[DATA_SIZE];
    if (myWire->requestFrom(addr, DATA_SIZE) == DATA_SIZE)
    {
        for (int i = 0; i < DATA_SIZE; i++)
        {
            buf[i] = myWire->read();
        }
        int tempAd = combine_u8_to_u16(buf[1], buf[2]);
        int tempValue = ntc_ad_temp_map::adToTemp5K1Down(tempAd);
        int igbtAd = combine_u8_to_u16(buf[3], buf[4]);
        int igbtTempValue = ntc_ad_temp_map::adToTemp20KUp(igbtAd);
        if (addr == POTA_ADDR)
        {
            potA_temp_ad = tempAd;
            potA_igbt_temp = igbtTempValue;
        }
        else if (addr == POTB_ADDR)
        {
            potB_temp_ad = tempAd;
            potB_igbt_temp = igbtTempValue;
        }
        *temp = tempValue;
        *igbt = igbtAd;
        *vol = buf[5];
        *cur = buf[6];
        *ppg = buf[7];
        *state = buf[8];
        // Serial.printf("电磁炉地址:%x, 温度ad:%d, 温度:%d, igbtAd:%d, igbt温度:%d, 电压Ad:%d, 电流Ad:%d, 状态flag:%x\n", addr, tempAd, *temp, igbtAd, igbtTempValue, *vol, *cur, *state);
    }

    else
    {
        return false;
        // Serial.println("读取失败");
    }
    return true;
}

void PotI2c::taskToExecute()
{
    // 与A锅通信
    PotI2c::getInstance()->lockState();
    bool workA = potA_end > millis() && potA_dw > 0 ? true : false;
    if (workA &&
        (DebugRunControl::getInstance()->isHeatingDisabled() ||
         (DebugRunControl::getInstance()->isBypassEnabled() &&
          !DebugRunControl::getInstance()->canHeatPotA())))
    {
        workA = false;
        potA_end = 0;
    }
    potA_dw = workA ? potA_dw : 0;
    int dwA = potA_dw;
    PotI2c::getInstance()->unlockState();
    PotI2c::getInstance()->lockBus();
    bool iicASendSuccess = PotI2c::getInstance()->sendData(POTA_ADDR, dwA, workA);
    bool iicAReadSuccess = PotI2c::getInstance()->readData(POTA_ADDR, &potA_temp, &potA_igbt, &potA_vol, &potA_cur, &potA_ppg, &potA_state);
    PotI2c::getInstance()->unlockBus();
    if (!iicASendSuccess || !iicAReadSuccess)
    {
        POTA_S_CODE = IIC_CONN_ERROR;
        Serial.printf("A锅IIC通信异常\n");
    }
    else
    {
        if (potA_temp < -90 || potA_temp > 400)
        {
            POTA_S_CODE = NTC_ERROR;
            Serial.printf("A锅NTC温度异常,potA_temp:%d\n", potA_temp);
        }
        else if (potA_state == 0x01)
        {
            POTA_S_CODE = POT_NO;
            Serial.printf("A锅无锅,potA_temp:%d\n", potA_temp);
        }
        else if (potA_state == 0x02)
        {
            POTA_S_CODE = POT_NO_COIL;
            Serial.printf("A锅无线圈,potA_temp:%d\n", potA_temp);
        }
        else
        {
            POTA_S_CODE = POT_OK;
        }
    }
    // 与B锅通信
    PotI2c::getInstance()->lockState();
    bool workB = potB_end > millis() && potB_dw > 0 ? true : false;
    if (workB &&
        (DebugRunControl::getInstance()->isHeatingDisabled() ||
         (DebugRunControl::getInstance()->isBypassEnabled() &&
          !DebugRunControl::getInstance()->canHeatPotB())))
    {
        workB = false;
        potB_end = 0;
    }
    potB_dw = workB ? potB_dw : 0;
    int dwB = potB_dw;
    PotI2c::getInstance()->unlockState();
    PotI2c::getInstance()->lockBus();
    bool iicBSendSuccess = PotI2c::getInstance()->sendData(POTB_ADDR, dwB, workB);
    bool iicBReadSuccess = PotI2c::getInstance()->readData(POTB_ADDR, &potB_temp, &potB_igbt, &potB_vol, &potB_cur, &potB_ppg, &potB_state);
    PotI2c::getInstance()->unlockBus();
    if (!iicBSendSuccess || !iicBReadSuccess)
    {
        POTB_S_CODE = IIC_CONN_ERROR;
        Serial.printf("B锅IIC通信异常\n");
    }
    else
    {
        if (potB_temp < -90 || potB_temp > 400)
        {
            POTB_S_CODE = NTC_ERROR;
            Serial.printf("B锅NTC温度异常,potB_temp:%d\n", potB_temp);
        }
        else if (potB_state == 0x01)
        {
            POTB_S_CODE = POT_NO;
            Serial.printf("B锅无锅,potB_temp:%d\n", potB_temp);
        }
        else if (potB_state == 0x02)
        {
            POTB_S_CODE = POT_NO_COIL;
            Serial.printf("B锅无线圈,potB_temp:%d\n", potB_temp);
        }
        else
        {
            POTB_S_CODE = POT_OK;
        }
    }
    // Serial.printf("[PotI2c][%lus]\n"
    //               "  A锅{addr:0x%02X dw:%d temp:%d tempAd:%d igbtAd:%d igbtTemp:%d state:0x%02X sCode:%d}\n"
    //               "  B锅{addr:0x%02X dw:%d temp:%d tempAd:%d igbtAd:%d igbtTemp:%d state:0x%02X sCode:%d}\n",
    //               millis() / 1000,
    //               POTA_ADDR, potA_dw, potA_temp, potA_temp_ad, potA_igbt, potA_igbt_temp, potA_state, POTA_S_CODE,
    //               POTB_ADDR, potB_dw, potB_temp, potB_temp_ad, potB_igbt, potB_igbt_temp, potB_state, POTB_S_CODE);
}

void PotI2c::update()
{
    timer->update();
}

void PotI2c::lockBus()
{
    if (busMutex != NULL)
    {
        xSemaphoreTake(busMutex, portMAX_DELAY);
    }
}

void PotI2c::unlockBus()
{
    if (busMutex != NULL)
    {
        xSemaphoreGive(busMutex);
    }
}

void PotI2c::lockState()
{
    if (stateMutex != NULL)
    {
        xSemaphoreTake(stateMutex, portMAX_DELAY);
    }
}

void PotI2c::unlockState()
{
    if (stateMutex != NULL)
    {
        xSemaphoreGive(stateMutex);
    }
}

uint16_t PotI2c::combine_u8_to_u16(uint8_t high_byte, uint8_t low_byte)
{
    return ((uint16_t)high_byte << 8) | low_byte;
}

int PotI2c::clampDw(int dw)
{
    if (dw <= 0)
    {
        return 0;
    }
    if (dw > 8)
    {
        return 8;
    }
    return dw;
}

uint8_t PotI2c::dwToCur(int dw)
{
    static const uint8_t DW_TO_CUR[] = {
        0x00, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xa0};

    return DW_TO_CUR[clampDw(dw)];
}
