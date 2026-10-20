#include "uart_connection.h"

#include "cyd_tof.h"
#include "pot_a_motor.h"
#include "pot_b_motor.h"
#include "pot_i2c.h"
#include "push_motor.h"
#include "trans_motor.h"
#include "common_data_manager.h"
#include "debug_run_control.h"
#include "egg_control.h"
#include "error_log_data_manager.h"
#include "fan.h"
#include "gpio.h"
#include "jiandan_data_manager.h"
#include "jiandan_maker.h"
#include "oil_control.h"
#include "oil_servo.h"
#include "taiyangdan_data_manager.h"
#include "taiyangdan_maker.h"
#include "tmc_config.h"
#include "reservation_manager.h"

#include <Arduino.h>
#include <initializer_list>
#include <math.h>
#include <string.h>

namespace
{
static constexpr size_t RESPONSE_CACHE_SIZE = 16;
static constexpr size_t RESPONSE_LINE_SIZE = 768;

struct ResponseCacheEntry
{
    bool valid;
    uint32_t id;
    char operation[PROTOCOL_MAX_OPERATION_LENGTH + 1];
    char response[RESPONSE_LINE_SIZE];
};

ResponseCacheEntry responseCache[RESPONSE_CACHE_SIZE] = {};
size_t responseCacheNext = 0;
uint32_t eventSequence = 1;

ResponseCacheEntry *findCachedResponse(uint32_t id)
{
    for (size_t i = 0; i < RESPONSE_CACHE_SIZE; ++i)
    {
        if (responseCache[i].valid && responseCache[i].id == id)
        {
            return &responseCache[i];
        }
    }
    return nullptr;
}

void cacheResponse(const ProtocolRequest &request, const String &response)
{
    ResponseCacheEntry &entry = responseCache[responseCacheNext];
    entry.valid = true;
    entry.id = request.id;
    strlcpy(entry.operation, request.operation.c_str(), sizeof(entry.operation));
    strlcpy(entry.response, response.c_str(), sizeof(entry.response));
    responseCacheNext = (responseCacheNext + 1U) % RESPONSE_CACHE_SIZE;
}

bool hasExactParams(const ProtocolRequest &request, std::initializer_list<const char *> keys)
{
    if (request.paramCount != keys.size())
    {
        return false;
    }
    for (const char *key : keys)
    {
        if (request.find(key) == nullptr)
        {
            return false;
        }
    }
    return true;
}

bool inRange(int32_t value, int32_t minimum, int32_t maximum)
{
    return value >= minimum && value <= maximum;
}

bool isBooleanValue(int32_t value)
{
    return value == 0 || value == 1;
}

bool parseOptionalInt(const ProtocolRequest &request, const char *key, int32_t &value, bool &present)
{
    const String *text = request.find(key);
    present = text != nullptr;
    return !present || parseStrictInt32(*text, value);
}

bool parseMotor(const String &name, TmcMotor &motor)
{
    if (name == "PUSH")
    {
        motor = TMC_MOTOR_PUSH;
    }
    else if (name == "TRANS")
    {
        motor = TMC_MOTOR_TRANS;
    }
    else if (name == "POTB")
    {
        motor = TMC_MOTOR_POTB;
    }
    else if (name == "POTA")
    {
        motor = TMC_MOTOR_POTA;
    }
    else
    {
        return false;
    }
    return true;
}

void writeUncachedResponse(HardwareSerial *serial,
                           uint32_t id,
                           const String &operation,
                           const char *result,
                           const char *code)
{
    if (serial == nullptr)
    {
        return;
    }
    serial->printf("RSP %u %s result=%s code=%s\r\n",
                   static_cast<unsigned>(id), operation.c_str(), result, code);
}
}

void UartConnection::processLine(const String &line)
{
    ProtocolRequest request;
    const ProtocolParseResult parsed = parseProtocolRequest(line, request);
    if (parsed != ProtocolParseResult::OK)
    {
        const String operation = request.operation.length() > 0 ? request.operation : "protocol";
        writeUncachedResponse(m_serial, request.id, operation, "rejected", "invalid_argument");
        return;
    }

    ResponseCacheEntry *cached = findCachedResponse(request.id);
    if (cached != nullptr)
    {
        if (request.operation == cached->operation)
        {
            m_serial->print(cached->response);
            m_serial->print("\r\n");
        }
        else
        {
            writeUncachedResponse(m_serial, request.id, request.operation, "rejected", "id_conflict");
        }
        return;
    }
    dispatchRequest(request);
}

void UartConnection::sendResponse(const ProtocolRequest &request,
                            const char *result,
                            const char *code,
                            const String &fields)
{
    String response = "RSP ";
    response += String(request.id);
    response += " ";
    response += request.operation;
    response += " result=";
    response += result;
    response += " code=";
    response += code;
    if (fields.length() > 0)
    {
        response += " ";
        response += fields;
    }
    if (response.length() >= RESPONSE_LINE_SIZE)
    {
        response = "RSP " + String(request.id) + " " + request.operation +
                   " result=failed code=response_too_large";
    }
    cacheResponse(request, response);
    m_serial->print(response);
    m_serial->print("\r\n");
}

void UartConnection::sendEvent(const char *event, const String &fields)
{
    if (m_serial == nullptr || event == nullptr)
    {
        return;
    }
    const uint32_t sequence = eventSequence++;
    if (eventSequence == 0)
    {
        eventSequence = 1;
    }
    m_serial->printf("EVT %u %s", static_cast<unsigned>(sequence), event);
    if (fields.length() > 0)
    {
        m_serial->print(" ");
        m_serial->print(fields);
    }
    m_serial->print("\r\n");
}

void UartConnection::dispatchRequest(const ProtocolRequest &request)
{
    JiandanDataManager *jiandan = JiandanDataManager::getInstance();
    TaiyangdanDataManager *sun = TaiyangdanDataManager::getInstance();
    PotI2c *pot = PotI2c::getInstance();

    if (request.operation == "state.get")
    {
        ReservationManager *reservation = ReservationManager::getInstance();
        uint32_t syncedEpoch = 0;
        const bool withoutClock = hasExactParams(request, {});
        const bool withClock = hasExactParams(request, {"epoch"}) &&
                               request.getUInt("epoch", syncedEpoch);
        if (!withoutClock && !withClock)
        {
            sendResponse(request, "rejected", "invalid_argument");
            return;
        }
        if (withClock && syncedEpoch > 0)
        {
            reservation->syncClock(syncedEpoch);
        }
        String fields;
        fields.reserve(480);
        const bool useSunState = sun->getStart() || sun->hasNeed() ||
                                 (jiandan->getDoneness() == JiandanDataManager::SUNNY_DONENESS &&
                                  !jiandan->getStart());
        fields = "start=" + String(useSunState ? (sun->getStart() ? 1 : 0)
                                                : (jiandan->getStart() ? 1 : 0));
        fields += " err=" + String(useSunState ? static_cast<int>(sun->getErrorCode())
                                                : static_cast<int>(jiandan->getErrorCode()));
        fields += " need=" + String(useSunState ? static_cast<int>(sun->getQuantity())
                                                 : static_cast<int>(ceilf(jiandan->getNeedMakeEggs())));
        fields += " remaining_s=" + String(useSunState ? sun->getRemainingTime()
                                                        : jiandan->getRemainingTime());
        fields += " total_make_s=" + String(useSunState ? sun->getTotalMakeTime()
                                                         : jiandan->getTotalMakeTime());
        fields += " maker_state=" + String(useSunState ? static_cast<int>(sun->getMakerState())
                                                        : static_cast<int>(jiandan->getMakerState()));
        fields += " pot_b_temp_c=" + String(pot->getTempB());
        fields += " pot_a_temp_c=" + String(pot->getTempA());
        fields += " pot_b_igbt_c=" + String(pot->getIgbtTempB());
        fields += " pot_a_igbt_c=" + String(pot->getIgbtTempA());
        fields += " pot_b_dw=" + String(pot->getPotBDW());
        fields += " pot_a_dw=" + String(pot->getPotADW());
        fields += " pot_b_heat_remaining_s=" + String(useSunState ? sun->getPotBRemainingHeatTime()
                                                                   : jiandan->getPotBRemainingHeatTime());
        fields += " pot_a_heat_remaining_s=" + String(useSunState ? sun->getPotARemainingHeatTime()
                                                                   : jiandan->getPotARemainingHeatTime());
        fields += " debug_bypass_mask=" + String(DebugRunControl::getInstance()->getBypassMask());
        fields += " motion_no_heat=" + String(DebugRunControl::getInstance()->isHeatingDisabled() ? 1 : 0);
        fields += " reservation_active=" + String(reservation->isActive() ? 1 : 0);
        fields += " reservation_epoch=" + String(reservation->getEpoch());
        fields += " reservation_qty=" + String(reservation->getQuantity());
        fields += " reservation_status=" + String(static_cast<uint8_t>(reservation->getStatus()));
        fields += " clock_valid=" + String(reservation->hasValidClock() ? 1 : 0);
        sendResponse(request, "ok", "none", fields);
        return;
    }

    if (request.operation == "reservation.set")
    {
        ReservationManager *reservation = ReservationManager::getInstance();
        uint32_t requestedEpoch;
        int32_t quantity, oil, doneness;
        if (!hasExactParams(request, {"epoch", "qty", "oil", "doneness"}) ||
            !request.getUInt("epoch", requestedEpoch) || !request.getInt("qty", quantity) ||
            !request.getInt("oil", oil) || !request.getInt("doneness", doneness) ||
            !inRange(quantity, 1, JiandanDataManager::MAX_MAKE_QUANTITY) ||
            !inRange(oil, 0, 2) || !inRange(doneness, 0, 3))
        {
            sendResponse(request, "rejected", "invalid_argument");
            return;
        }

        const char *errorCode = "none";
        if (!reservation->schedule(requestedEpoch, static_cast<uint8_t>(quantity),
                                   static_cast<uint8_t>(oil), static_cast<uint8_t>(doneness),
                                   errorCode))
        {
            sendResponse(request, "rejected", errorCode);
            return;
        }
        sendResponse(request, "ok", "none",
                     "epoch=" + String(requestedEpoch) + " qty=" + String(quantity));
        sendEvent("reservation.scheduled",
                  "epoch=" + String(requestedEpoch) + " qty=" + String(quantity));
        return;
    }

    if (request.operation == "reservation.cancel")
    {
        if (!hasExactParams(request, {}))
        {
            sendResponse(request, "rejected", "invalid_argument");
            return;
        }
        ReservationManager::getInstance()->cancel();
        sendResponse(request, "ok", "none");
        sendEvent("reservation.cancelled");
        return;
    }

    if (request.operation == "config.get")
    {
        if (!hasExactParams(request, {}))
        {
            sendResponse(request, "rejected", "invalid_argument");
            return;
        }
        String fields = "oil=" + String(jiandan->getOilLevel());
        fields += " doneness=" + String(jiandan->getDoneness());
        fields += " oil_offset=" + String(jiandan->getOilLevelOffsetPercent());
        fields += " doneness_offset=" + String(jiandan->getDonenessOffsetPercent());
        fields += " max_temp_c=" + String(jiandan->getMaxPotTempLimit());
        fields += " power_offset=" + String(jiandan->getFirstPowerOffsetPercent());
        fields += " power_offset_first=" + String(jiandan->getFirstPowerOffsetPercent());
        fields += " power_offset_second=" + String(jiandan->getSecondPowerOffsetPercent());
        fields += " init_dw=" + String(jiandan->getInitDw());
        fields += " laser_check=" + String(jiandan->getLaserCheckEnabled() ? 1 : 0);
        fields += " second_oil=" + String(jiandan->getSecondOilEnabled() ? 1 : 0);
        fields += " debug_bypass_mask=" + String(DebugRunControl::getInstance()->getBypassMask());
        fields += " motion_no_heat=" + String(DebugRunControl::getInstance()->isHeatingDisabled() ? 1 : 0);
        sendResponse(request, "ok", "none", fields);
        return;
    }

    if (request.operation == "maker.start" || request.operation == "maker.stop" ||
        request.operation == "maker.recover")
    {
        if (!hasExactParams(request, {}))
        {
            sendResponse(request, "rejected", "invalid_argument");
            return;
        }
        if (request.operation == "maker.stop")
        {
            JiandanMaker::requestEmergencyStop(true);
            sendResponse(request, "accepted", "none");
            return;
        }
        if (request.operation == "maker.recover")
        {
            if (!JiandanMaker::confirmEmergencyRecovery())
            {
                sendResponse(request, "rejected", "stopping");
                return;
            }
            sendResponse(request, "ok", "none");
            return;
        }
        if (sun->getStart() || sun->hasNeed())
        {
            sendResponse(request, "rejected", "busy_sun");
            return;
        }
        if (JiandanMaker::isEmergencyStopRequested())
        {
            sendResponse(request, "rejected", "recovery_required");
            return;
        }
        jiandan->setErrorCode(ALL_OK);
        jiandan->setStart(true);
        sendResponse(request, "accepted", "none");
        return;
    }

    if (request.operation == "error.clear")
    {
        if (!hasExactParams(request, {}))
        {
            sendResponse(request, "rejected", "invalid_argument");
            return;
        }
        jiandan->setErrorCode(ALL_OK);
        sendResponse(request, "ok", "none");
        return;
    }

    if (request.operation == "quantity.set")
    {
        int32_t value;
        if (!hasExactParams(request, {"value"}) || !request.getInt("value", value) ||
            !inRange(value, 0, JiandanDataManager::MAX_MAKE_QUANTITY))
        {
            sendResponse(request, "rejected", "invalid_argument");
            return;
        }
        if (jiandan->getStart())
        {
            sendResponse(request, "rejected", "running");
            return;
        }
        if (value > JiandanDataManager::maxMakeQuantityForDoneness(jiandan->getDoneness()))
        {
            sendResponse(request, "rejected", "quantity_limit");
            return;
        }
        jiandan->setNeedMakeEggs(static_cast<float>(value));
        sendResponse(request, "ok", "none", "need=" + String(value));
        return;
    }

    if (request.operation == "quantity.add" || request.operation == "quantity.sub")
    {
        int32_t value;
        if (!hasExactParams(request, {"value"}) || !request.getInt("value", value) || value <= 0)
        {
            sendResponse(request, "rejected", "invalid_argument");
            return;
        }
        const float delta = request.operation == "quantity.add" ? static_cast<float>(value) : -static_cast<float>(value);
        jiandan->setNeedMakeEggs(jiandan->getNeedMakeEggs() + delta);
        sendResponse(request, "ok", "none", "need=" + String(static_cast<int>(ceilf(jiandan->getNeedMakeEggs()))));
        return;
    }

    if (request.operation == "config.set")
    {
        int32_t value;
        const String *key = request.find("key");
        if (!hasExactParams(request, {"key", "value"}) || key == nullptr || !request.getInt("value", value))
        {
            sendResponse(request, "rejected", "invalid_argument");
            return;
        }
        bool valid = true;
        if (*key == "oil" && inRange(value, 0, 2)) jiandan->setOilLevel(value);
        else if (*key == "doneness" && inRange(value, 0, 3)) jiandan->setDoneness(value);
        else if (*key == "oil_offset" && inRange(value, JiandanDataManager::OIL_LEVEL_OFFSET_MIN_PERCENT, JiandanDataManager::OIL_LEVEL_OFFSET_MAX_PERCENT)) jiandan->setOilLevelOffsetPercent(value);
        else if (*key == "doneness_offset" && inRange(value, JiandanDataManager::DONENESS_OFFSET_MIN_PERCENT, JiandanDataManager::DONENESS_OFFSET_MAX_PERCENT)) jiandan->setDonenessOffsetPercent(value);
        else if (*key == "max_temp_c" && inRange(value, JiandanDataManager::MAX_POT_TEMP_LIMIT_MIN, JiandanDataManager::MAX_POT_TEMP_LIMIT_MAX)) jiandan->setMaxPotTempLimit(value);
        else if (*key == "power_offset" && inRange(value, JiandanDataManager::POWER_OFFSET_MIN_PERCENT, JiandanDataManager::POWER_OFFSET_MAX_PERCENT)) jiandan->setPowerOffsetPercent(value);
        else if (*key == "power_offset_first" && inRange(value, JiandanDataManager::POWER_OFFSET_MIN_PERCENT, JiandanDataManager::POWER_OFFSET_MAX_PERCENT)) jiandan->setFirstPowerOffsetPercent(value);
        else if (*key == "power_offset_second" && inRange(value, JiandanDataManager::POWER_OFFSET_MIN_PERCENT, JiandanDataManager::POWER_OFFSET_MAX_PERCENT)) jiandan->setSecondPowerOffsetPercent(value);
        else if (*key == "init_dw" && inRange(value, JiandanDataManager::INIT_DW_MIN, JiandanDataManager::INIT_DW_MAX)) jiandan->setInitDw(value);
        else if (*key == "laser_check" && isBooleanValue(value)) jiandan->setLaserCheckEnabled(value != 0);
        else if (*key == "second_oil" && isBooleanValue(value)) jiandan->setSecondOilEnabled(value != 0);
        else valid = false;
        sendResponse(request, valid ? "ok" : "rejected", valid ? "none" : "invalid_argument");
        return;
    }

    if (request.operation == "debug.get")
    {
        if (!hasExactParams(request, {}))
        {
            sendResponse(request, "rejected", "invalid_argument");
            return;
        }
        DebugRunControl *debug = DebugRunControl::getInstance();
        String fields = "bypass=" + String(debug->isBypassEnabled() ? 1 : 0);
        fields += " mask=" + String(debug->getBypassMask());
        fields += " no_heat=" + String(debug->isHeatingDisabled() ? 1 : 0);
        fields += " no_heat_a=" + String(debug->isBypassEnabled() && !debug->canHeatPotA() ? 1 : 0);
        fields += " no_heat_b=" + String(debug->isBypassEnabled() && !debug->canHeatPotB() ? 1 : 0);
        sendResponse(request, "ok", "none", fields);
        return;
    }

    if (request.operation == "debug.set")
    {
        int32_t value;
        const String *key = request.find("key");
        if (!hasExactParams(request, {"key", "value"}) || key == nullptr || !request.getInt("value", value))
        {
            sendResponse(request, "rejected", "invalid_argument");
            return;
        }
        if (jiandan->getStart() || sun->getStart())
        {
            sendResponse(request, "rejected", "busy");
            return;
        }
        DebugRunControl *debug = DebugRunControl::getInstance();
        bool valid = true;
        if (*key == "mask" && inRange(value, 0, DebugRunControl::ALL_BYPASS_MASK)) debug->setBypassMask(static_cast<uint8_t>(value));
        else if (*key == "bypass" && isBooleanValue(value)) debug->setBypassEnabled(value != 0);
        else if (*key == "no_heat" && isBooleanValue(value)) debug->setHeatingDisabled(value != 0);
        else if (*key == "all" && isBooleanValue(value))
        {
            debug->setBypassEnabled(value != 0);
            debug->setHeatingDisabled(value != 0);
        }
        else valid = false;
        sendResponse(request, valid ? "ok" : "rejected", valid ? "none" : "invalid_argument");
        return;
    }

    if (request.operation == "system.restart")
    {
        if (!hasExactParams(request, {}))
        {
            sendResponse(request, "rejected", "invalid_argument");
            return;
        }
        sendResponse(request, "accepted", "none");
        m_serial->flush();
        delay(20);
        esp_restart();
        return;
    }

    if (request.operation == "debug.stress")
    {
        const String *actionValue = request.find("action");
        if (!hasExactParams(request, {"action"}) || actionValue == nullptr)
        {
            sendResponse(request, "failed", "invalid_argument");
            return;
        }
        String action = *actionValue;
        action.toUpperCase();
        bool known = true;
        bool success = false;
        if (action == "GIVEEGG") success = EggControl::getInstance()->runSelfTest();
        else if (action == "BEAT") success = PushMotor::getInstance()->runSelfTest();
        else if (action == "TRANS") success = TransMotor::getInstance()->runSelfTest();
        else if (action == "TURN_POTB") success = PotBMotor::getInstance()->runSelfTest();
        else if (action == "TURN_POTA") success = PotAMotor::getInstance()->runSelfTest();
        else if (action == "GIVE_OIL") success = OilControl::getInstance()->runSelfTest();
        else known = false;
        sendResponse(request, success ? "ok" : "failed", known ? (success ? "none" : "action_failed") : "unsupported_action");
        return;
    }

    if (request.operation == "error_log.get")
    {
        if (!hasExactParams(request, {}))
        {
            sendResponse(request, "rejected", "invalid_argument");
            return;
        }
        ErrorLogDataManager *log = ErrorLogDataManager::getInstance();
        const int count = log->getCount();
        sendResponse(request, "ok", "none", "count=" + String(count));
        for (int index = 0; index < count; ++index)
        {
            ErrorLogEntry entry;
            if (log->getEntry(index, entry))
            {
                String fields = "request_id=" + String(request.id);
                fields += " index=" + String(index);
                fields += " seq=" + String(entry.seq);
                fields += " code=" + String(entry.code);
                fields += " boot_id=" + String(entry.bootId);
                fields += " uptime_s=" + String(entry.uptimeSec);
                sendEvent("error_log.entry", fields);
            }
        }
        return;
    }

    if (request.operation == "error_log.clear")
    {
        if (!hasExactParams(request, {}))
        {
            sendResponse(request, "rejected", "invalid_argument");
            return;
        }
        ErrorLogDataManager::getInstance()->clear();
        sendResponse(request, "ok", "none");
        return;
    }

    if (request.operation == "sun.state.get")
    {
        if (!hasExactParams(request, {}))
        {
            sendResponse(request, "rejected", "invalid_argument");
            return;
        }
        String fields = "start=" + String(sun->getStart() ? 1 : 0);
        fields += " err=" + String(static_cast<int>(sun->getErrorCode()));
        fields += " quantity=" + String(sun->getQuantity());
        fields += " maker_state=" + String(static_cast<int>(sun->getMakerState()));
        fields += " remaining_s=" + String(sun->getRemainingTime());
        fields += " total_make_s=" + String(sun->getTotalMakeTime());
        fields += " pot_a_temp_c=" + String(pot->getTempA());
        fields += " pot_b_temp_c=" + String(pot->getTempB());
        fields += " pot_a_dw=" + String(pot->getPotADW());
        fields += " pot_b_dw=" + String(pot->getPotBDW());
        fields += " pot_a_heat_remaining_s=" + String(sun->getPotARemainingHeatTime());
        fields += " pot_b_heat_remaining_s=" + String(sun->getPotBRemainingHeatTime());
        sendResponse(request, "ok", "none", fields);
        return;
    }

    if (request.operation == "sun.config.get")
    {
        if (!hasExactParams(request, {}))
        {
            sendResponse(request, "rejected", "invalid_argument");
            return;
        }
        String fields = "oil=" + String(sun->getOilLevel());
        fields += " doneness=" + String(sun->getDoneness());
        fields += " oil_offset=" + String(sun->getOilLevelOffsetPercent());
        fields += " doneness_offset=" + String(sun->getDonenessOffsetPercent());
        fields += " max_temp_c=" + String(sun->getMaxPotTempLimit());
        fields += " power_offset=" + String(sun->getPowerOffsetPercent());
        fields += " max_dw=" + String(sun->getMaxDwLimit());
        fields += " power_dw_fall_gap_s=" + String(sun->getPowerDwFallGap());
        sendResponse(request, "ok", "none", fields);
        return;
    }

    if (request.operation == "sun.clear")
    {
        if (!hasExactParams(request, {}))
        {
            sendResponse(request, "rejected", "invalid_argument");
            return;
        }
        if (sun->getStart())
        {
            sendResponse(request, "rejected", "running");
            return;
        }
        sun->clearNeed();
        sun->setErrorCode(ALL_OK);
        sendResponse(request, "ok", "none");
        return;
    }

    if (request.operation == "sun.quantity.set")
    {
        int32_t value;
        if (!hasExactParams(request, {"value"}) || !request.getInt("value", value) ||
            !inRange(value, 1, TaiyangdanDataManager::MAX_MAKE_QUANTITY))
        {
            sendResponse(request, "rejected", "invalid_argument");
            return;
        }
        if (sun->getStart())
        {
            sendResponse(request, "rejected", "running");
            return;
        }
        sun->setQuantity(static_cast<uint8_t>(value));
        sendResponse(request, "ok", "none", "quantity=" + String(value));
        return;
    }

    if (request.operation == "sun.start" || request.operation == "sun.stop")
    {
        if (!hasExactParams(request, {}))
        {
            sendResponse(request, "rejected", "invalid_argument");
            return;
        }
        if (request.operation == "sun.stop")
        {
            TaiyangdanMaker::stop();
            sendResponse(request, "accepted", "none");
            return;
        }
        if (!sun->hasNeed())
        {
            sendResponse(request, "rejected", "no_quantity");
            return;
        }
        if (jiandan->getStart() || jiandan->getNeedMakeEggs() > 0.0f)
        {
            sendResponse(request, "rejected", "busy_jiandan");
            return;
        }
        sun->setErrorCode(ALL_OK);
        sun->setStart(true);
        sendResponse(request, "accepted", "none");
        return;
    }

    if (request.operation == "sun.config.set")
    {
        int32_t value;
        const String *key = request.find("key");
        if (!hasExactParams(request, {"key", "value"}) || key == nullptr || !request.getInt("value", value))
        {
            sendResponse(request, "rejected", "invalid_argument");
            return;
        }
        if (sun->getStart())
        {
            sendResponse(request, "rejected", "running");
            return;
        }
        bool valid = true;
        if (*key == "oil" && inRange(value, 0, 2)) sun->setOilLevel(value);
        else if (*key == "doneness" && inRange(value, 0, 3)) sun->setDoneness(value);
        else if (*key == "oil_offset" && inRange(value, TaiyangdanDataManager::OIL_LEVEL_OFFSET_MIN_PERCENT, TaiyangdanDataManager::OIL_LEVEL_OFFSET_MAX_PERCENT)) sun->setOilLevelOffsetPercent(value);
        else if (*key == "doneness_offset" && inRange(value, TaiyangdanDataManager::DONENESS_OFFSET_MIN_PERCENT, TaiyangdanDataManager::DONENESS_OFFSET_MAX_PERCENT)) sun->setDonenessOffsetPercent(value);
        else if (*key == "max_temp_c" && inRange(value, TaiyangdanDataManager::MAX_POT_TEMP_LIMIT_MIN, TaiyangdanDataManager::MAX_POT_TEMP_LIMIT_MAX)) sun->setMaxPotTempLimit(value);
        else if (*key == "power_offset" && inRange(value, TaiyangdanDataManager::POWER_OFFSET_MIN_PERCENT, TaiyangdanDataManager::POWER_OFFSET_MAX_PERCENT)) sun->setPowerOffsetPercent(value);
        else valid = false;
        sendResponse(request, valid ? "ok" : "rejected", valid ? "none" : "invalid_argument");
        return;
    }

    if (request.operation == "pot.debug.get")
    {
        if (!hasExactParams(request, {}))
        {
            sendResponse(request, "rejected", "invalid_argument");
            return;
        }
        int aAddr, aTempAd, aIgbtAd, aVol, aCur, aPpg, aState;
        int bAddr, bTempAd, bIgbtAd, bVol, bCur, bPpg, bState;
        pot->getPotAInfo(aAddr, aTempAd, aIgbtAd, aVol, aCur, aPpg, aState);
        pot->getPotBInfo(bAddr, bTempAd, bIgbtAd, bVol, bCur, bPpg, bState);
        String fields = "pot_a_addr=" + String(aAddr);
        fields += " pot_a_temp_ad=" + String(aTempAd);
        fields += " pot_a_temp_c=" + String(pot->getTempA());
        fields += " pot_a_igbt_ad=" + String(aIgbtAd);
        fields += " pot_a_igbt_c=" + String(pot->getIgbtTempA());
        fields += " pot_a_voltage=" + String(aVol);
        fields += " pot_a_current=" + String(aCur);
        fields += " pot_a_ppg=" + String(aPpg);
        fields += " pot_a_state=" + String(aState);
        fields += " pot_b_addr=" + String(bAddr);
        fields += " pot_b_temp_ad=" + String(bTempAd);
        fields += " pot_b_temp_c=" + String(pot->getTempB());
        fields += " pot_b_igbt_ad=" + String(bIgbtAd);
        fields += " pot_b_igbt_c=" + String(pot->getIgbtTempB());
        fields += " pot_b_voltage=" + String(bVol);
        fields += " pot_b_current=" + String(bCur);
        fields += " pot_b_ppg=" + String(bPpg);
        fields += " pot_b_state=" + String(bState);
        sendResponse(request, "ok", "none", fields);
        return;
    }

    if (request.operation == "debug.run")
    {
        const String *actionValue = request.find("action");
        if (actionValue == nullptr || request.paramCount < 1 || request.paramCount > 3)
        {
            sendResponse(request, "rejected", "invalid_argument");
            return;
        }
        for (size_t i = 0; i < request.paramCount; ++i)
        {
            if (request.params[i].key != "action" && request.params[i].key != "arg1" && request.params[i].key != "arg2")
            {
                sendResponse(request, "rejected", "invalid_argument");
                return;
            }
        }
        String action = *actionValue;
        action.toUpperCase();
        const String *arg1 = request.find("arg1");
        const String *arg2 = request.find("arg2");
        int32_t value1 = 0;
        int32_t value2 = 0;
        bool success = true;
        bool known = true;
        bool valid = true;

        if (action == "HOME" && arg1 == nullptr && arg2 == nullptr) home();
        else if ((action == "DWA" || action == "DWB") && arg1 != nullptr && arg2 != nullptr &&
                 parseStrictInt32(*arg1, value1) && parseStrictInt32(*arg2, value2) &&
                 inRange(value1, 0, 8) && value2 >= 0)
            setPotDw(action, value1, value2);
        else if (action == "GIVEOIL" && arg2 == nullptr)
        {
            int milliseconds = JiandanMaker::calcOilTime(jiandan->getOilLevel());
            if (arg1 != nullptr && (!parseStrictInt32(*arg1, value1) || value1 <= 0)) valid = false;
            else
            {
                if (arg1 != nullptr) milliseconds = value1;
                success = giveOil(milliseconds);
            }
        }
        else if (action == "BACKOIL" && arg1 == nullptr && arg2 == nullptr) backOil();
        else if (action == "GIVEEGG" && arg1 == nullptr && arg2 == nullptr) giveEgg();
        else if (action == "GIVEEGGOPEN" && arg1 == nullptr && arg2 == nullptr) giveEggOpen();
        else if (action == "GIVEEGGCLOSE" && arg1 == nullptr && arg2 == nullptr) giveEggClose();
        else if (action == "BEAT" && arg1 == nullptr && arg2 == nullptr) beat();
        else if ((action == "PUBS" || action == "TRANSS" || action == "OILS") && arg1 == nullptr && arg2 == nullptr)
        {
            const int pin = action == "PUBS" ? PUSH_POS_SENSOR : (action == "TRANSS" ? TRANS_POS_SENSOR : OIL_CHECK);
            sendEvent("debug.status", "name=" + action + " value=" + String(digitalRead(pin) == HIGH ? 1 : 0));
        }
        else if ((action == "TRANS" || action == "PUSH" || action == "POTA" || action == "POTB") &&
                 arg1 != nullptr && arg2 == nullptr && parseStrictInt32(*arg1, value1)) moveMotor(action, value1);
        else if (action == "GO_POTB" && arg1 == nullptr && arg2 == nullptr) TransMotor::getInstance()->goPotB();
        else if (action == "GO_POTA" && arg1 == nullptr && arg2 == nullptr) TransMotor::getInstance()->goPotA();
        else if (action == "SERVO_A" && arg1 == nullptr && arg2 == nullptr) OilServo::getInstance()->goPotA();
        else if (action == "SERVO_B" && arg1 == nullptr && arg2 == nullptr) OilServo::getInstance()->goPotB();
        else if (action == "SERVO_ZERO" && arg1 == nullptr && arg2 == nullptr)
        {
            const int current = OilServo::getInstance()->getCurrentAngle();
            if (current >= (OIL_SERVO_POTA_ANGLE + OIL_SERVO_POTB_ANGLE) / 2) OilServo::getInstance()->goZeroFromA();
            else OilServo::getInstance()->goZeroFromB();
        }
        else if (action == "TRANS_DUMP" && arg1 == nullptr && arg2 == nullptr) TransMotor::getInstance()->dumpMock();
        else if (action == "TRANS_DUMP_RETRY" && arg1 == nullptr && arg2 == nullptr) TransMotor::getInstance()->dumpRetry();
        else if (action == "SHAKE_EGG" && arg1 == nullptr && arg2 == nullptr) TransMotor::getInstance()->shakeSideways();
        else if (action == "PUSHGO" && arg1 == nullptr && arg2 == nullptr) PushMotor::getInstance()->openForClear();
        else if (action == "PUSHSTEP" && arg1 == nullptr && arg2 == nullptr) PushMotor::getInstance()->stepOpenForDebug();
        else if (action == "PUSHBACK" && arg1 == nullptr && arg2 == nullptr) PushMotor::getInstance()->close();
        else if (action == "PUSHSHAKE" && arg1 == nullptr && arg2 == nullptr) PushMotor::getInstance()->pushShakeshell(1);
        else if (action == "TURNA" && arg1 == nullptr && arg2 == nullptr) PotAMotor::getInstance()->takeOut();
        else if (action == "TURNB" && arg1 == nullptr && arg2 == nullptr) PotBMotor::getInstance()->turn();
        else if ((action == "DIST" || action == "ENV_TMP" || action == "MENV_TMP") && arg1 == nullptr && arg2 == nullptr)
        {
            int measured = 0;
            String name;
            if (action == "DIST")
            {
                name = "TRAY";
                measured = CydTof::getInstance()->getHaveDistance(true);
                Serial.printf("CYD3S1612(Have) debug distance=%d mm\n", measured);
            }
            else if (action == "ENV_TMP")
            {
                name = "ENVTMP";
                measured = static_cast<int>(CommonDataManager::getInstance()->getLocalMinTemp24h());
            }
            else
            {
                name = "MENVTMP";
                measured = static_cast<int>(CommonDataManager::getInstance()->getMachineEnvTemp());
            }
            sendEvent("debug.measure", "name=" + name + " value=" + String(measured));
        }
        else if (action == "FAN" && arg2 == nullptr)
        {
            if (arg1 == nullptr) Fan::getInstance()->toggle();
            else if (!parseStrictInt32(*arg1, value1) || value1 < 0) valid = false;
            else if (value1 == 0) Fan::getInstance()->turnOff();
            else Fan::getInstance()->turnOn(value1);
        }
        else if ((action == "CUR" || action == "MST") && arg1 != nullptr && arg2 != nullptr && parseStrictInt32(*arg2, value2))
        {
            TmcMotor motor;
            String motorName = *arg1;
            motorName.toUpperCase();
            if (!parseMotor(motorName, motor)) valid = false;
            else if (action == "CUR" && inRange(value2, 0, UINT16_MAX)) TMCConfig::getInstance()->setCurrent(motor, static_cast<uint16_t>(value2));
            else if (action == "MST" && inRange(value2, 8, 256) && (value2 & (value2 - 1)) == 0) TMCConfig::getInstance()->setMicrosteps(motor, static_cast<uint16_t>(value2));
            else valid = false;
        }
        else
        {
            static const char *knownActions[] = {
                "HOME", "DWA", "DWB", "GIVEOIL", "BACKOIL", "GIVEEGG", "GIVEEGGOPEN", "GIVEEGGCLOSE",
                "BEAT", "PUBS", "TRANS", "GO_POTB", "GO_POTA", "SERVO_A", "SERVO_B", "SERVO_ZERO",
                "TRANS_DUMP", "TRANS_DUMP_RETRY", "SHAKE_EGG", "TRANSS", "OILS", "PUSH", "PUSHGO", "PUSHSTEP",
                "PUSHBACK", "PUSHSHAKE", "TURNA", "TURNB", "POTA", "POTB", "DIST", "ENV_TMP", "MENV_TMP",
                "FAN", "CUR", "MST"};
            known = false;
            for (const char *candidate : knownActions)
            {
                if (action == candidate)
                {
                    known = true;
                    break;
                }
            }
            valid = false;
        }

        if (!known) sendResponse(request, "rejected", "unsupported_action");
        else if (!valid) sendResponse(request, "rejected", "invalid_argument");
        else sendResponse(request, success ? "ok" : "failed", success ? "none" : "action_failed");
        return;
    }

    sendResponse(request, "rejected", "unsupported_operation");
}
