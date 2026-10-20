#ifndef TAIYANGDAN_MAKER_H
#define TAIYANGDAN_MAKER_H
#include <stdint.h>
#include "Ticker.h"
#include "taiyangdan_data_manager.h"
#include "gpio.h"

enum class TaiyangdanPot : uint8_t
{
    A = 0,
    B = 1
};

class TaiyangdanMaker
{
private:
    TaiyangdanMaker() = delete;

public:
    static void makerControlTask(void *params);
    static void stop();
    static void home();
    static bool giveEgg();
    static bool giveOil(int oilLevel);
    static bool checkCydTofSensor();
    static bool checkPot(uint8_t selectedPots);

    // 预热线程，不阻塞给蛋给油流程
    static void preheatTask(void *params);

    // 同步破蛋并清理蛋壳；返回 false 表示清壳失败。
    static bool beatEgg(TaiyangdanPot pot);

    // B锅加热线程，控制加热时机和加热档位
    static void heatPotBTask(void *params);

    // A锅加热线程，控制加热时机和加热档位
    static void heatPotATask(void *params);

    // 判断是不是首次煎蛋
    static bool checkFirstMake(uint8_t selectedPots);


    // 根据油量计算所需的加油时间
    static int calcOilTime(int oilLevel);

    // 根据熟度计算所需的加热时间
    static int calcHeatTotalTime(TaiyangdanPot pot);

    //计算降档时间
    static int calcDwFallTimeGap(TaiyangdanPot pot);

    // 当前日志对应的煎蛋轮次（从 1 开始）
    static int getLogCycleIndex();

    static void clearError();
    
    static bool trayHasEgg(); // 蛋托是否有蛋

private:
    static ErrorCode error_code;
    static OIL_CHECK_STATE oil_check;
    static EGG_CHECK_STATE egg_check;

    static bool is_first_make; // 是否首次煎蛋，首次煎蛋时锅温较低，需要特殊处理

    static bool preheat_done;
    static int made_count;                // 已完成的煎蛋数量
    static unsigned long POTA_START_TIME;
    static unsigned long POTB_START_TIME;
    static unsigned long POTA_END_TIME;
    static unsigned long POTB_END_TIME;
    static int POTA_MAKE_TIME;
    static int POTB_MAKE_TIME;


    //打蛋完成归位时间
    static volatile unsigned long pota_beat_release_time_ms;
    static volatile unsigned long potb_beat_release_time_ms;

};

#endif
