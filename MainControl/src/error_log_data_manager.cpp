#include "error_log_data_manager.h"

#include <Arduino.h>

namespace
{
constexpr const char *NVS_NAMESPACE = "errlog";
constexpr const char *KEY_ENTRIES = "entries"; // 环形缓冲区数据(blob)
constexpr const char *KEY_COUNT = "count";     // 已记录条数
constexpr const char *KEY_HEAD = "head";       // 写入位置
constexpr const char *KEY_SEQ = "seq";         // 下一个序号
constexpr const char *KEY_BOOT = "boot";       // 开机计数
}

ErrorLogDataManager ErrorLogDataManager::instance;

ErrorLogDataManager::ErrorLogDataManager()
    : m_prefs(nullptr),
      m_count(0),
      m_head(0),
      m_nextSeq(0),
      m_bootId(0)
{
    for (int i = 0; i < MAX_ENTRIES; ++i)
    {
        m_entries[i] = ErrorLogEntry{};
    }
}

ErrorLogDataManager *ErrorLogDataManager::getInstance()
{
    return &instance;
}

void ErrorLogDataManager::init()
{
    if (m_prefs == nullptr)
    {
        m_prefs = new Preferences();
    }
    load();

    // 每次开机递增开机计数，用于区分不同上电周期的记录。
    m_bootId = (uint16_t)(m_bootId + 1);
    m_prefs->begin(NVS_NAMESPACE);
    m_prefs->putUShort(KEY_BOOT, m_bootId);
    m_prefs->end();

    Serial.printf("[ErrorLog] init done, count=%u bootId=%u nextSeq=%u\n",
                  (unsigned)m_count, (unsigned)m_bootId, (unsigned)m_nextSeq);
}

void ErrorLogDataManager::load()
{
    m_prefs->begin(NVS_NAMESPACE);

    m_count = m_prefs->getUShort(KEY_COUNT, 0);
    m_head = m_prefs->getUShort(KEY_HEAD, 0);
    m_nextSeq = m_prefs->getUInt(KEY_SEQ, 0);
    m_bootId = m_prefs->getUShort(KEY_BOOT, 0);

    if (m_count > MAX_ENTRIES)
    {
        m_count = MAX_ENTRIES;
    }
    if (m_head >= MAX_ENTRIES)
    {
        m_head = 0;
    }

    size_t expected = sizeof(m_entries);
    size_t got = m_prefs->getBytes(KEY_ENTRIES, m_entries, expected);
    if (got != expected)
    {
        // 数据不存在或损坏，重置为空。
        for (int i = 0; i < MAX_ENTRIES; ++i)
        {
            m_entries[i] = ErrorLogEntry{};
        }
        m_count = 0;
        m_head = 0;
    }

    m_prefs->end();
}

void ErrorLogDataManager::persist()
{
    m_prefs->begin(NVS_NAMESPACE);
    m_prefs->putBytes(KEY_ENTRIES, m_entries, sizeof(m_entries));
    m_prefs->putUShort(KEY_COUNT, m_count);
    m_prefs->putUShort(KEY_HEAD, m_head);
    m_prefs->putUInt(KEY_SEQ, m_nextSeq);
    m_prefs->end();
}

void ErrorLogDataManager::record(ErrorCode code)
{
    if (code == ALL_OK)
    {
        return;
    }

    // 去重：与最近一条相同的错误码不重复记录。
    if (m_count > 0)
    {
        uint16_t lastIdx = (uint16_t)((m_head + MAX_ENTRIES - 1) % MAX_ENTRIES);
        if (m_entries[lastIdx].code == (uint8_t)code)
        {
            return;
        }
    }

    ErrorLogEntry &slot = m_entries[m_head];
    slot.code = (uint8_t)code;
    slot.bootId = m_bootId;
    slot.uptimeSec = (uint32_t)(millis() / 1000UL);
    slot.seq = ++m_nextSeq;

    m_head = (uint16_t)((m_head + 1) % MAX_ENTRIES);
    if (m_count < MAX_ENTRIES)
    {
        m_count++;
    }

    persist();

    Serial.printf("[ErrorLog] record E%02u seq=%u bootId=%u uptime=%us\n",
                  (unsigned)slot.code, (unsigned)slot.seq,
                  (unsigned)slot.bootId, (unsigned)slot.uptimeSec);
}

void ErrorLogDataManager::clear()
{
    for (int i = 0; i < MAX_ENTRIES; ++i)
    {
        m_entries[i] = ErrorLogEntry{};
    }
    m_count = 0;
    m_head = 0;
    persist();

    Serial.printf("[ErrorLog] cleared\n");
}

int ErrorLogDataManager::getCount()
{
    return (int)m_count;
}

bool ErrorLogDataManager::getEntry(int index, ErrorLogEntry &out)
{
    if (index < 0 || index >= (int)m_count)
    {
        return false;
    }

    // index=0 对应最近一条，向前回溯环形缓冲区。
    int idx = ((int)m_head + MAX_ENTRIES - 1 - index) % MAX_ENTRIES;
    out = m_entries[idx];
    return true;
}

void ErrorLogDataManager::dump()
{
    Serial.printf("[ErrorLog] ==== %u entries ====\n", (unsigned)m_count);
    for (int i = 0; i < (int)m_count; ++i)
    {
        ErrorLogEntry e;
        if (getEntry(i, e))
        {
            Serial.printf("  #%u E%02u boot=%u uptime=%us\n",
                          (unsigned)e.seq, (unsigned)e.code,
                          (unsigned)e.bootId, (unsigned)e.uptimeSec);
        }
    }
}
