#ifndef POT_I2C_H
#define POT_I2C_H
#include <Arduino.h>
#include <Wire.h>
#include "Ticker.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define DATA_SIZE 9
#define TICK_TIME 200
// A锅 靠近用户的锅
// B锅 远离用户的锅


// 档位与电流参数对应表，第三列仅为旧设备参考值
/*
档位	电流参数(cur)	旧参考(W)
0       0x00	    0
1       0x30	    190
2       0x40	    250
3       0x50	    310
4       0x60	    370
5       0x70	    430
6       0x80	    490
7       0x90	    550
8       0xa0	    610
*/


enum POT_STATE
{
    POT_OK = 0,
    NTC_ERROR = 1,
    IIC_CONN_ERROR = 2,
    POT_NO,
    POT_NO_COIL

};

class PotI2c
{

private:
    static PotI2c instance;

public:
    static PotI2c *getInstance();
    PotI2c();
    void init();
    bool checkPot();
    bool checkPot(bool checkPotA, bool checkPotB);
    // dw <= 0 stops immediately. milliseconds is only used for active DW 1..8.
    void setPotADW(int dw, int milliseconds);
    void setPotBDW(int dw, int milliseconds);
    int getTempA() { return potA_temp; }
    int getTempB() { return potB_temp; }
    int getIgbtTempA() { return potA_igbt_temp; }
    int getIgbtTempB() { return potB_igbt_temp; }

    //debug用接口，获取原始数据
    void getPotAInfo(int &addr, int &tempAd, int &igbtAd, int &vol, int &cur, int &ppg, int &state);
    void getPotBInfo(int &addr, int &tempAd, int &igbtAd, int &vol, int &cur, int &ppg, int &state);

    int getStateA() { return potA_state; }
    int getStateB() { return potB_state; }
    int getPotADW() { return potA_dw; }
    int getPotBDW() { return potB_dw; }
    POT_STATE getPOTA_S_CODE() { return POTA_S_CODE; }
    POT_STATE getPOTB_S_CODE() { return POTB_S_CODE; }

    void update();

private:
    bool sendData(int addr, int dw, bool work);
    bool readData(int addr, int *temp, int *igbt, int *vol, int *cur, int *ppg, int *state);
    static void taskToExecute();
    int clampDw(int dw);
    uint8_t dwToCur(int dw);
    // 总线复合事务锁：保证单锅“发送+读取”作为一次原子交换，防止跨任务交织
    void lockBus();
    void unlockBus();
    // 状态锁：保护 {dw,end} 配对，避免周期任务与设置接口竞态
    void lockState();
    void unlockState();

    uint16_t combine_u8_to_u16(uint8_t, uint8_t);

public:
    static int potA_temp_ad;
    static int potA_temp;
    static int potA_igbt;
    static int potA_igbt_temp;
    static int potA_vol;
    static int potA_cur;
    static int potA_ppg;
    static int potA_state; // 0x01：无锅，ox02：未接线盘
    static int potA_dw;
    static unsigned long potA_end;
    static POT_STATE POTA_S_CODE;

    static int potB_temp_ad;
    static int potB_temp;
    static int potB_igbt;
    static int potB_igbt_temp;
    static int potB_vol;
    static int potB_cur;
    static int potB_ppg;
    static int potB_state; // 0x01：无锅，ox02：未接线盘
    static int potB_dw;
    static unsigned long potB_end;
    static POT_STATE POTB_S_CODE;

private:
    Ticker *timer;
    TwoWire *myWire = NULL;
    SemaphoreHandle_t busMutex = NULL;
    SemaphoreHandle_t stateMutex = NULL;
};

#endif
