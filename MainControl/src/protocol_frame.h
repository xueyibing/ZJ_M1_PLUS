#ifndef PROTOCOL_FRAME_H
#define PROTOCOL_FRAME_H

#include <Arduino.h>
#include <stdint.h>

static constexpr size_t PROTOCOL_MAX_LINE_LENGTH = 384;
static constexpr size_t PROTOCOL_MAX_PARAMS = 16;
static constexpr size_t PROTOCOL_MAX_OPERATION_LENGTH = 63;

enum class ProtocolParseResult
{
    OK,
    INVALID_PREFIX,
    INVALID_ID,
    MISSING_OPERATION,
    INVALID_ARGUMENT,
    TOO_MANY_ARGUMENTS
};

struct ProtocolParam
{
    String key;
    String value;
};

struct ProtocolRequest
{
    uint32_t id = 0;
    String operation;
    ProtocolParam params[PROTOCOL_MAX_PARAMS];
    size_t paramCount = 0;

    const String *find(const char *key) const;
    bool getInt(const char *key, int32_t &value) const;
    bool getUInt(const char *key, uint32_t &value) const;
};

ProtocolParseResult parseProtocolRequest(const String &line, ProtocolRequest &request);
bool parseStrictInt32(const String &text, int32_t &value);
bool parseStrictUInt32(const String &text, uint32_t &value, bool allowZero = true);

#endif