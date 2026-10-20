#ifndef GPIO_H
#define GPIO_H

#define ONE_CIRCLE_STEPS 200

// 破蛋步进电机step
#define PUSH_MOTOR_STEP 15
#define PUSH_MOTOR_DIR 7
#define PUSH_MOTOR_EN 16
//推杆状态
#define PUSH_POS_SENSOR 8

// 转托步进电机step
#define TRANS_MOTOR_STEP 20
#define TRANS_MOTOR_DIR 19
#define TRANS_MOTOR_EN 9

// A锅
#define POTA_MOTOR_STEP 38
#define POTA_MOTOR_DIR 37
#define POTA_MOTOR_EN 39

// B锅
#define POTB_MOTOR_STEP 42
#define POTB_MOTOR_DIR 41
#define POTB_MOTOR_EN 2


// 打蛋器位置开关，位置状态
#define TRANS_POS_SENSOR 4


// 电磁炉iic
#define POT_I2C_SDA 6
#define POT_I2C_SCL 5

// 油量控制
#define GIVE_OIL_A 10
#define GIVE_OIL_B 11
//油位状态
#define OIL_CHECK 12

// 落蛋电机
#define GIVE_EGG_A 13
#define GIVE_EGG_B 14

// 风扇控制
#define FAN_CTR 48

// UI 通信
#define UI_TX_PIN 21 // 自定义TX引脚
#define UI_RX_PIN 47 // 自定义RX引脚

// CYD3S1612 tray distance sensor
#define CYD_TOF_SCL_PIN 17
#define CYD_TOF_SDA_PIN 18

#define TMC_UART_RX_PIN 35
#define TMC_UART_TX_PIN 36

#define OIL_SERVO_PIN 40

enum ErrorCode
{
    // 0: 正常
    ALL_OK = 0,

    // 1~18: 业务/硬件错误码
    // 注意:
    // 1. 这些编号同时被 UI 端和 maker_mock 的模拟选码逻辑使用，修改时请保持编号稳定。
    // 2. maker_mock 约定:
    //    - 轻油 + 数字1~6  => E01~E06
    //    - 适油 + 数字1~6  => E07~E12
    //    - 多油 + 数字1~6  => E13~E18
    //    - 数字7~9         => 不注入故障，走正常 mock 流程
    OIL_EMPTY = 1,
    EGG_EMPTY = 2,
    BEAT_NOT_EMPTY = 3,
    POTA_TMP_SENSOR_ERROR = 4,
    POTB_TMP_SENSOR_ERROR = 5,
    POTA_NO = 6,
    POTB_NO = 7,
    POTA_NO_COIL = 8,
    POTB_NO_COIL = 9,
    BEAT_SHELL_NOT_CLEAN = 10,
    POTA_OUT_EGG_FAIL = 11,
    POTA_IIC_ERROR = 12,
    POTB_IIC_ERROR = 13,
    CYD_TOF_HAVE_ERROR = 14,
    CYD_TOF_HAVE_BLOCK = 15, // 打蛋检测传感器有障碍物
    CYD_TOF_POTA_ERROR = 16,
    CYD_TOF_POTA_BLOCK = 17, // 锅A检测传感器有障碍物
    POTB_OVERHEAT = 18, // 锅B过热
    POTA_OVERHEAT = 19, // 锅A过热
    UNKNOWN_ERROR = 20,
    MOTOR_DRIVER_ERROR = 21, // 电机驱动异常（TMC 初始化失败）
    PUSH_HOME_FAIL = 22      // 推杆回零失败（限位未触发）
};
//机器运行状态
enum MakerState
{
    IDLE = 0,
    CHECKING_TMP_SENSOR, //温度传感器检测
    CHECKING_CYD_TOF,  //测距传感器检测
    CHECKING_POT,   //电磁炉检测
    HOMING,         //打蛋器复位中
    /* 协议值 5 为已删除的旧标定状态；后续状态编号保持不变。 */
    PRE_HEATING = 6,                 //预热中
    GIVING_EGG,                       //给蛋
    GIVING_OIL,                      //给油
    MAKING,                           // 煎蛋中
    STOPPING,                         // 正在立即停止并等待任务退出
    STOPPED_NEEDS_RECOVERY,           // 已停止，等待用户清锅并确认复位
};


enum EGG_CHECK_STATE
{
    EGG_CHECK_IDLE = 0,
    EGG_CHECK_NO_EGG,
    EGG_CHECK_HAS_EGG
};

enum OIL_CHECK_STATE
{
    OIL_CHECK_IDLE = 0,
    OIL_CHECK_NO_OIL,
    OIL_CHECK_HAS_OIL
};
#endif
