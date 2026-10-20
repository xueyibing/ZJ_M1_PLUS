#ifndef ERROR_LOG_DATA_MANAGER_H
#define ERROR_LOG_DATA_MANAGER_H

#include <stdint.h>
#include <Preferences.h>

#include "gpio.h"

// 单条错误日志记录。
struct ErrorLogEntry
{
    uint8_t code;      // ErrorCode 值
    uint16_t bootId;   // 记录时的开机计数，用于区分不同上电周期
    uint32_t uptimeSec; // 记录时本次开机的运行秒数(millis/1000)
    uint32_t seq;      // 全局递增序号
};

// 错误日志管理器：环形缓冲保存最近若干条错误，掉电后可从 NVS 恢复。
class ErrorLogDataManager
{
private:
    static ErrorLogDataManager instance;

public:
    // 最多保存的历史错误条数。
    static constexpr int MAX_ENTRIES = 20;

    static ErrorLogDataManager *getInstance();
    ErrorLogDataManager();

    void init();

    // 记录一次错误码。ALL_OK 会被忽略；与上一条相同的错误码会被去重(不重复记录)。
    void record(ErrorCode code);

    // 清空全部历史记录。
    void clear();

    // 当前已保存的记录条数(<= MAX_ENTRIES)。
    int getCount();

    // 按时间倒序读取第 index 条记录(0 为最近一条)。
    // 成功返回 true 并填充 out；越界返回 false。
    bool getEntry(int index, ErrorLogEntry &out);

    // 打印全部记录到串口，方便调试。
    void dump();

private:
    void load();
    void persist();

    Preferences *m_prefs;
    ErrorLogEntry m_entries[MAX_ENTRIES];
    uint16_t m_count;   // 已记录条数(封顶 MAX_ENTRIES)
    uint16_t m_head;    // 下一个写入位置
    uint32_t m_nextSeq; // 递增序号
    uint16_t m_bootId;  // 本次开机计数
};

#endif
