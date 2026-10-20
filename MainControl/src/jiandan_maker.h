#ifndef JIANDAN_MAKER_H
#define JIANDAN_MAKER_H
#include "Ticker.h"
#include "jiandan_data_manager.h"
#include <freertos/semphr.h>
#include "gpio.h"

class JiandanMaker
{
private:
    JiandanMaker() = delete;

public:
    static void makerControlTask(void *params);
    static void home();
    static bool giveEgg();
    
    //第一面给油
    static bool firstGiveOil(int oilLevel);
    // 第二面给油：仅在 potTurnTask 持有机械互斥锁时调用。
    static bool secondGiveOil(int oilLevel);

    static bool checkCydTofSensor();
    static bool checkPot();

    // 预热线程，不阻塞给蛋给油流程
    static void preheatTask(void *params);

    // 打蛋线程，控制打蛋时机和打蛋动作
    static void beatTask(void *params);

    // 翻锅线程，控制B锅和A锅的翻锅时机
    static void potTurnTask(void *params);

    // B锅加热线程，控制加热时机和加热档位
    static void heatPotBTask(void *params);

    // A锅加热线程,控制加热时机和加热档位
    static void heatPotATask(void *params);

    // 判断是不是首次煎蛋
    static bool checkFirstMake();

    // 根据油量计算所需的加油时间
    static int calcOilTime(int oilLevel);

    // 根据熟度计算所需的加热时间
    static int calcHeatTotalTime();

    // 计算降档时间
    static int calcDwFallTimeGap(float powerOffset);

    // 当前日志对应的煎蛋轮次（从 1 开始）
    static int getLogCycleIndex();

    static void clearError();

    // “取消”采用立即停止语义：先停危险输出，再等待任务协作退出。
    static void requestEmergencyStop(bool returnEggHome = false);
    static bool confirmEmergencyRecovery();
    static bool isEmergencyStopRequested();
    static uint32_t getActiveOperationCount();

    static bool trayHasEgg(); // 蛋托是否有蛋

private:
    // 锅内蛋状态只能通过以下接口访问。B锅翻入A锅时两个标志位必须原子更新，
    // 防止不同FreeRTOS任务读到不可能的中间状态。
    static void getPotEmptyState(bool &potAEmpty, bool &potBEmpty);
    static uint32_t getPotBTransferSequence();
    static bool hasPotBTransferReached(uint32_t expectedSequence);
    static void markPotBOccupied();
    static uint32_t markPotBTransferredToA();
    static void markPotAEmpty();
    static void resetPotEmptyState();
    static void logPotEmptyState(const char *event);

    static bool createManagedTask(TaskFunction_t task,
                                  const char *name,
                                  uint32_t stackDepth,
                                  void *params);
    static bool beginActiveOperation();
    static void endActiveOperation();
    static void finishManagedTask();
    static void updateStoppedStateIfQuiescent();
    static void performStopCleanupIfReady();
    static bool isStopCleanupPending();
    static void requestSafetyStop(bool returnEggHome,
                                  bool returnMotionHome,
                                  const char *reason);

    static SemaphoreHandle_t m_mutex;
    static ErrorCode error_code;
    static OIL_CHECK_STATE oil_check;
    static EGG_CHECK_STATE egg_check;

    //flag 标志

    static bool is_first_make; // 是否首次煎蛋，首次煎蛋时锅温较低，需要特殊处理
    static bool preheat_done;
    // B->A转移的累计完成次数。只允许通过临界区保护的接口读写。
    static uint32_t potb_transfer_sequence;
    static bool potb_is_empty;
    static bool pota_is_empty;
    static uint32_t active_operation_count;
    static bool cancel_egg_home_required;
    static bool cancel_egg_home_in_progress;
    static bool fault_motion_home_required;
    static bool fault_motion_home_in_progress;
    static bool motion_home_required;


    static int made_count;                // 已完成的煎蛋数量
    static unsigned long START_TIME;      // 制作开始时间
    static unsigned long SINGLE_END_TIME; // 单面制作结束时间

    static int SINGLE_SIDE_MAKE_TIME; // 煎蛋单面煎制时间

    static unsigned long POTB_TURN_TIME; // B锅翻锅时间
    static unsigned long POTA_TURN_TIME; // A锅翻锅时间

    static unsigned long POTA_PRE_HEAT_TIME; // A锅加热开始时间

    // 本颗蛋循环起点的配方快照，避免制作中途修改设置影响当前周期。
    static int s_cycle_oil_level;
    static int s_cycle_doneness;
};

#endif
