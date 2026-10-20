#include "protocol_frame.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

namespace
{
bool nextToken(const String &line, size_t &cursor, String &token)
{
    while (cursor < line.length() && (line[cursor] == ' ' || line[cursor] == '\t'))
    {
        ++cursor;
    }
    if (cursor >= line.length())
    {
        token = "";
        return false;
    }

    const size_t start = cursor;
    while (cursor < line.length() && line[cursor] != ' ' && line[cursor] != '\t')
    {
        ++cursor;
    }
    token = line.substring(start, cursor);
    return true;
}
}

const String *ProtocolRequest::find(const char *key) const
{
    if (key == nullptr)
    {
        return nullptr;
    }
    for (size_t i = 0; i < paramCount; ++i)
    {
        if (params[i].key == key)
        {
            return &params[i].value;
        }
    }
    return nullptr;
}

bool ProtocolRequest::getInt(const char *key, int32_t &value) const
{
    const String *text = find(key);
    return text != nullptr && parseStrictInt32(*text, value);
}

bool ProtocolRequest::getUInt(const char *key, uint32_t &value) const
{
    const String *text = find(key);
    return text != nullptr && parseStrictUInt32(*text, value);
}

bool parseStrictInt32(const String &text, int32_t &value)
{
    if (text.length() == 0)
    {
        return false;
    }
    size_t digit = text[0] == '-' ? 1U : 0U;
    if (digit == text.length())
    {
        return false;
    }
    for (; digit < text.length(); ++digit)
    {
        if (text[digit] < '0' || text[digit] > '9')
        {
            return false;
        }
    }
    char *end = nullptr;
    errno = 0;
    const long parsed = strtol(text.c_str(), &end, 10);
    if (errno == ERANGE || end == text.c_str() || *end != '\0' || parsed < INT32_MIN || parsed > INT32_MAX)
    {
        return false;
    }
    value = static_cast<int32_t>(parsed);
    return true;
}

bool parseStrictUInt32(const String &text, uint32_t &value, bool allowZero)
{
    if (text.length() == 0)
    {
        return false;
    }
    for (size_t digit = 0; digit < text.length(); ++digit)
    {
        if (text[digit] < '0' || text[digit] > '9')
        {
            return false;
        }
    }
    char *end = nullptr;
    errno = 0;
    const unsigned long parsed = strtoul(text.c_str(), &end, 10);
    if (errno == ERANGE || end == text.c_str() || *end != '\0' || parsed > UINT32_MAX || (!allowZero && parsed == 0))
    {
        return false;
    }
    value = static_cast<uint32_t>(parsed);
    return true;
}

ProtocolParseResult parseProtocolRequest(const String &line, ProtocolRequest &request)
{
    request = ProtocolRequest();
    for (size_t i = 0; i < line.length(); ++i)
    {
        const uint8_t character = static_cast<uint8_t>(line[i]);
        if (character > 0x7f || (character < 0x20 && character != '\t'))
        {
            return ProtocolParseResult::INVALID_ARGUMENT;
        }
    }
    size_t cursor = 0;
    String token;
    if (!nextToken(line, cursor, token) || token != "REQ")
    {
        return ProtocolParseResult::INVALID_PREFIX;
    }
    if (!nextToken(line, cursor, token) || !parseStrictUInt32(token, request.id, false))
    {
        return ProtocolParseResult::INVALID_ID;
    }
    if (!nextToken(line, cursor, request.operation) || request.operation.length() == 0 ||
        request.operation.length() > PROTOCOL_MAX_OPERATION_LENGTH)
    {
        return ProtocolParseResult::MISSING_OPERATION;
    }

    while (nextToken(line, cursor, token))
    {
        if (request.paramCount >= PROTOCOL_MAX_PARAMS)
        {
            return ProtocolParseResult::TOO_MANY_ARGUMENTS;
        }
        const int separator = token.indexOf('=');
        if (separator <= 0 || separator == token.length() - 1 || token.indexOf('=', separator + 1) >= 0)
        {
            return ProtocolParseResult::INVALID_ARGUMENT;
        }
        ProtocolParam &param = request.params[request.paramCount++];
        param.key = token.substring(0, separator);
        param.value = token.substring(separator + 1);
        for (size_t i = 0; i + 1 < request.paramCount; ++i)
        {
            if (request.params[i].key == param.key)
            {
                return ProtocolParseResult::INVALID_ARGUMENT;
            }
        }
    }
    return ProtocolParseResult::OK;
}